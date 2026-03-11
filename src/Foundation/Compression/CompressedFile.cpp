/**
 * @file src/Foundation/Compression/CompressedFile.cpp
 * @brief Implementation for CompressedFile.
 */

#include "Foundation/Compression/CompressedFile.h"

#include "Foundation/Logging/Logbot.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>

#include "STB/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "STB/stb_image_write.h"

namespace {

inline static Logbot compressedFileLogger = Logbot::CreateInstance("CompressedFile");

constexpr std::uint32_t kZipLocalHeaderSignature = 0x04034b50u;
constexpr std::uint32_t kZipCentralDirectorySignature = 0x02014b50u;
constexpr std::uint32_t kZipEndOfCentralDirectorySignature = 0x06054b50u;
constexpr std::uint16_t kZipMethodStore = 0;
constexpr std::uint16_t kZipMethodDeflate = 8;
constexpr std::uint16_t kZipUtf8Flag = 0x0800u;

void setCompressedFileError(std::string* outError, const std::string& message){
    if(outError){
        *outError = message;
    }
}

std::uint16_t readLE16(const BinaryBuffer& data, size_t offset){
    return static_cast<std::uint16_t>(data[offset]) |
           (static_cast<std::uint16_t>(data[offset + 1]) << 8);
}

std::uint32_t readLE32(const BinaryBuffer& data, size_t offset){
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

void appendLE16(BinaryBuffer& data, std::uint16_t value){
    data.push_back(static_cast<std::uint8_t>(value & 0xffu));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
}

void appendLE32(BinaryBuffer& data, std::uint32_t value){
    data.push_back(static_cast<std::uint8_t>(value & 0xffu));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    data.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    data.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

bool readFileBytes(const std::filesystem::path& path, BinaryBuffer& outData, std::string* outError){
    outData.clear();

    std::ifstream stream(path, std::ios::binary);
    if(!stream.is_open()){
        setCompressedFileError(outError, "Failed to open archive: " + path.generic_string());
        return false;
    }

    stream.seekg(0, std::ios::end);
    std::streamoff fileSize = stream.tellg();
    if(fileSize < 0){
        setCompressedFileError(outError, "Failed to read archive size: " + path.generic_string());
        return false;
    }
    stream.seekg(0, std::ios::beg);

    outData.resize(static_cast<size_t>(fileSize));
    if(fileSize > 0){
        stream.read(reinterpret_cast<char*>(outData.data()), fileSize);
        if(!stream.good() && !stream.eof()){
            setCompressedFileError(outError, "Failed to read archive bytes: " + path.generic_string());
            outData.clear();
            return false;
        }
    }

    return true;
}

bool writeFileBytes(const std::filesystem::path& path, const BinaryBuffer& data, std::string* outError){
    std::error_code ec;
    std::filesystem::path parent = path.parent_path();
    if(!parent.empty() && !std::filesystem::exists(parent, ec)){
        if(!std::filesystem::create_directories(parent, ec)){
            setCompressedFileError(outError, "Failed to create archive directory: " + parent.generic_string());
            return false;
        }
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if(!stream.is_open()){
        setCompressedFileError(outError, "Failed to open archive for write: " + path.generic_string());
        return false;
    }

    if(!data.empty()){
        stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if(!stream.good()){
        setCompressedFileError(outError, "Failed to write archive: " + path.generic_string());
        return false;
    }
    return true;
}

/// @brief Represents Prepared Zip Write Entry data.
struct PreparedZipWriteEntry {
    std::string path;
    BinaryBuffer payload;
    std::uint16_t compressionMethod = kZipMethodStore;
    std::uint32_t crc32 = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t uncompressedSize = 0;
    std::uint32_t localHeaderOffset = 0;
    bool isDirectory = false;
};

bool prepareWriteEntry(const CompressedFile::WriteEntry& source, PreparedZipWriteEntry& outEntry, std::string* outError){
    outEntry = PreparedZipWriteEntry{};
    outEntry.isDirectory = source.isDirectory;
    outEntry.path = CompressedFile::NormalizeEntryPath(source.path, source.isDirectory);
    if(outEntry.path.empty() && !source.isDirectory){
        setCompressedFileError(outError, "Archive entry path must not be empty.");
        return false;
    }
    if(outEntry.path.empty() && source.isDirectory){
        setCompressedFileError(outError, "Archive directory entry path must not be empty.");
        return false;
    }

    if(source.isDirectory){
        outEntry.payload.clear();
        return true;
    }

    if(source.data.size() > static_cast<size_t>(std::numeric_limits<std::uint32_t>::max())){
        setCompressedFileError(outError, "ZIP64 archives are not supported yet (entry too large): " + outEntry.path);
        return false;
    }

    outEntry.uncompressedSize = static_cast<std::uint32_t>(source.data.size());
    outEntry.crc32 = CompressedFile::ComputeCRC32(source.data);
    outEntry.payload = source.data;
    outEntry.compressedSize = static_cast<std::uint32_t>(outEntry.payload.size());

    if(source.preferCompression &&
       !source.data.empty() &&
       source.data.size() <= static_cast<size_t>(std::numeric_limits<int>::max())){
        int compressedSizeWithWrapper = 0;
        unsigned char* compressedWithWrapper = stbi_zlib_compress(
            const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(source.data.data())),
            static_cast<int>(source.data.size()),
            &compressedSizeWithWrapper,
            8
        );

        if(compressedWithWrapper){
            const bool hasWrapper = compressedSizeWithWrapper > 6;
            if(hasWrapper){
                const int rawDeflateSize = compressedSizeWithWrapper - 6;
                if(rawDeflateSize > 0 && static_cast<size_t>(rawDeflateSize) < source.data.size()){
                    outEntry.payload.assign(
                        compressedWithWrapper + 2,
                        compressedWithWrapper + 2 + rawDeflateSize
                    );
                    outEntry.compressionMethod = kZipMethodDeflate;
                    outEntry.compressedSize = static_cast<std::uint32_t>(outEntry.payload.size());
                }
            }
            STBIW_FREE(compressedWithWrapper);
        }
    }

    return true;
}

} // namespace

bool CompressedFile::open(const std::filesystem::path& path, std::string* outError){
    currentFormat = Format::Unknown;
    archivePath.clear();
    archiveBytes.clear();
    entryList.clear();
    entryIndexByPath.clear();

    if(path.empty()){
        setCompressedFileError(outError, "Archive path was empty.");
        return false;
    }

    if(!readFileBytes(path, archiveBytes, outError)){
        return false;
    }

    archivePath = path;
    if(!parseZip(outError)){
        currentFormat = Format::Unknown;
        archivePath.clear();
        archiveBytes.clear();
        entryList.clear();
        entryIndexByPath.clear();
        return false;
    }

    currentFormat = Format::Zip;
    return true;
}

bool CompressedFile::write(const std::vector<WriteEntry>& entriesToWrite, std::string* outError){
    if(archivePath.empty()){
        setCompressedFileError(outError, "CompressedFile::write() requires an existing archive path.");
        return false;
    }
    return writeToPath(archivePath, entriesToWrite, outError);
}

bool CompressedFile::writeToPath(const std::filesystem::path& path, const std::vector<WriteEntry>& entriesToWrite, std::string* outError){
    if(path.empty()){
        setCompressedFileError(outError, "Archive path was empty.");
        return false;
    }
    if(entriesToWrite.size() > static_cast<size_t>(std::numeric_limits<std::uint16_t>::max())){
        setCompressedFileError(outError, "ZIP64 archives are not supported yet (too many entries).");
        return false;
    }

    std::vector<PreparedZipWriteEntry> preparedEntries;
    preparedEntries.reserve(entriesToWrite.size());
    std::set<std::string> seenPaths;

    for(size_t i = 0; i < entriesToWrite.size(); ++i){
        PreparedZipWriteEntry prepared;
        if(!prepareWriteEntry(entriesToWrite[i], prepared, outError)){
            return false;
        }
        if(!seenPaths.insert(prepared.path).second){
            setCompressedFileError(outError, "Duplicate archive entry path: " + prepared.path);
            return false;
        }
        preparedEntries.push_back(prepared);
    }

    BinaryBuffer outBytes;
    outBytes.reserve(1024);

    for(auto& entry : preparedEntries){
        if(outBytes.size() > static_cast<size_t>(std::numeric_limits<std::uint32_t>::max())){
            setCompressedFileError(outError, "ZIP64 archives are not supported yet (archive too large).");
            return false;
        }

        entry.localHeaderOffset = static_cast<std::uint32_t>(outBytes.size());

        appendLE32(outBytes, kZipLocalHeaderSignature);
        appendLE16(outBytes, 20);
        appendLE16(outBytes, kZipUtf8Flag);
        appendLE16(outBytes, entry.compressionMethod);
        appendLE16(outBytes, 0);
        appendLE16(outBytes, 0);
        appendLE32(outBytes, entry.crc32);
        appendLE32(outBytes, entry.compressedSize);
        appendLE32(outBytes, entry.uncompressedSize);
        appendLE16(outBytes, static_cast<std::uint16_t>(entry.path.size()));
        appendLE16(outBytes, 0);
        outBytes.insert(outBytes.end(), entry.path.begin(), entry.path.end());
        outBytes.insert(outBytes.end(), entry.payload.begin(), entry.payload.end());
    }

    if(outBytes.size() > static_cast<size_t>(std::numeric_limits<std::uint32_t>::max())){
        setCompressedFileError(outError, "ZIP64 archives are not supported yet (archive too large).");
        return false;
    }

    const std::uint32_t centralDirectoryOffset = static_cast<std::uint32_t>(outBytes.size());
    for(const auto& entry : preparedEntries){
        appendLE32(outBytes, kZipCentralDirectorySignature);
        appendLE16(outBytes, 20);
        appendLE16(outBytes, 20);
        appendLE16(outBytes, kZipUtf8Flag);
        appendLE16(outBytes, entry.compressionMethod);
        appendLE16(outBytes, 0);
        appendLE16(outBytes, 0);
        appendLE32(outBytes, entry.crc32);
        appendLE32(outBytes, entry.compressedSize);
        appendLE32(outBytes, entry.uncompressedSize);
        appendLE16(outBytes, static_cast<std::uint16_t>(entry.path.size()));
        appendLE16(outBytes, 0);
        appendLE16(outBytes, 0);
        appendLE16(outBytes, 0);
        appendLE16(outBytes, 0);
        appendLE32(outBytes, entry.isDirectory ? 0x10u : 0u);
        appendLE32(outBytes, entry.localHeaderOffset);
        outBytes.insert(outBytes.end(), entry.path.begin(), entry.path.end());
    }

    if(outBytes.size() > static_cast<size_t>(std::numeric_limits<std::uint32_t>::max())){
        setCompressedFileError(outError, "ZIP64 archives are not supported yet (archive too large).");
        return false;
    }

    const std::uint32_t centralDirectorySize = static_cast<std::uint32_t>(outBytes.size() - centralDirectoryOffset);

    appendLE32(outBytes, kZipEndOfCentralDirectorySignature);
    appendLE16(outBytes, 0);
    appendLE16(outBytes, 0);
    appendLE16(outBytes, static_cast<std::uint16_t>(preparedEntries.size()));
    appendLE16(outBytes, static_cast<std::uint16_t>(preparedEntries.size()));
    appendLE32(outBytes, centralDirectorySize);
    appendLE32(outBytes, centralDirectoryOffset);
    appendLE16(outBytes, 0);

    if(!writeFileBytes(path, outBytes, outError)){
        return false;
    }

    archivePath = path;
    archiveBytes = outBytes;
    currentFormat = Format::Zip;
    entryList.clear();
    entryIndexByPath.clear();
    return parseZip(outError);
}

bool CompressedFile::hasEntry(const std::string& entryPath) const{
    return findEntry(entryPath) != nullptr;
}

const CompressedFile::Entry* CompressedFile::findEntry(const std::string& entryPath) const{
    const std::string normalized = NormalizeEntryPath(entryPath);
    if(normalized.empty()){
        return nullptr;
    }

    auto it = entryIndexByPath.find(normalized);
    if(it == entryIndexByPath.end() || it->second >= entryList.size()){
        return nullptr;
    }
    return &entryList[it->second];
}

bool CompressedFile::readEntry(const std::string& entryPath, BinaryBuffer& outData, std::string* outError) const{
    outData.clear();

    const Entry* entry = findEntry(entryPath);
    if(!entry){
        setCompressedFileError(outError, "Archive entry was not found: " + NormalizeEntryPath(entryPath));
        return false;
    }
    if(entry->isDirectory){
        return true;
    }

    const uint8_t* payloadData = nullptr;
    size_t payloadSize = 0;
    if(!readLocalFilePayload(*entry, payloadData, payloadSize, outError)){
        return false;
    }

    if(entry->compressionMethod == kZipMethodStore){
        outData.assign(payloadData, payloadData + payloadSize);
    }else if(entry->compressionMethod == kZipMethodDeflate){
        if(entry->uncompressedSize == 0){
            outData.clear();
        }else{
            if(payloadSize > static_cast<size_t>(std::numeric_limits<int>::max())){
                setCompressedFileError(outError, "Compressed ZIP entry is too large to decode: " + entry->path);
                return false;
            }
            int decodedSize = 0;
            char* decoded = stbi_zlib_decode_noheader_malloc(
                reinterpret_cast<const char*>(payloadData),
                static_cast<int>(payloadSize),
                &decodedSize
            );
            if(!decoded){
                setCompressedFileError(outError, "Failed to decompress archive entry: " + entry->path);
                return false;
            }

            outData.assign(
                reinterpret_cast<const std::uint8_t*>(decoded),
                reinterpret_cast<const std::uint8_t*>(decoded) + decodedSize
            );
            stbi_image_free(decoded);
        }
    }else{
        setCompressedFileError(
            outError,
            "Unsupported ZIP compression method " + std::to_string(entry->compressionMethod) + " for entry: " + entry->path
        );
        return false;
    }

    if(outData.size() != entry->uncompressedSize){
        setCompressedFileError(outError, "Archive entry size mismatch after decode: " + entry->path);
        return false;
    }

    const std::uint32_t actualCrc = ComputeCRC32(outData);
    if(actualCrc != entry->crc32){
        setCompressedFileError(outError, "Archive entry CRC mismatch: " + entry->path);
        return false;
    }

    return true;
}

std::string CompressedFile::NormalizeEntryPath(const std::string& path, bool forceDirectory){
    if(path.empty()){
        return forceDirectory ? std::string() : std::string();
    }

    std::string normalized = path;
    for(char& c : normalized){
        if(c == '\\'){
            c = '/';
        }
    }

    bool isDirectory = forceDirectory;
    if(!normalized.empty() && normalized.back() == '/'){
        isDirectory = true;
    }

    while(!normalized.empty() && normalized.front() == '/'){
        normalized.erase(normalized.begin());
    }

    std::filesystem::path normalizedPath = std::filesystem::path(normalized).lexically_normal();
    normalized = normalizedPath.generic_string();
    for(char& c : normalized){
        if(c == '\\'){
            c = '/';
        }
    }

    if(normalized == "."){
        normalized.clear();
    }
    while(normalized.size() >= 2 && normalized[0] == '.' && normalized[1] == '/'){
        normalized.erase(0, 2);
    }
    if(normalized == ".." || normalized.rfind("../", 0) == 0){
        return std::string();
    }

    if(isDirectory){
        if(!normalized.empty() && normalized.back() != '/'){
            normalized.push_back('/');
        }
    }else{
        while(!normalized.empty() && normalized.back() == '/'){
            normalized.pop_back();
        }
    }

    return normalized;
}

std::uint32_t CompressedFile::ComputeCRC32(const uint8_t* data, size_t size){
    static const std::array<std::uint32_t, 256> kTable = []() {
        std::array<std::uint32_t, 256> table{};
        for(std::uint32_t i = 0; i < table.size(); ++i){
            std::uint32_t value = i;
            for(int bit = 0; bit < 8; ++bit){
                if((value & 1u) != 0u){
                    value = 0xedb88320u ^ (value >> 1);
                }else{
                    value >>= 1;
                }
            }
            table[i] = value;
        }
        return table;
    }();

    std::uint32_t crc = 0xffffffffu;
    for(size_t i = 0; i < size; ++i){
        crc = kTable[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

std::uint32_t CompressedFile::ComputeCRC32(const BinaryBuffer& data){
    if(data.empty()){
        return ComputeCRC32(nullptr, 0);
    }
    return ComputeCRC32(data.data(), data.size());
}

bool CompressedFile::parseZip(std::string* outError){
    size_t eocdOffset = 0;
    if(!locateZipEndOfCentralDirectory(eocdOffset, outError)){
        return false;
    }

    if((eocdOffset + 22) > archiveBytes.size()){
        setCompressedFileError(outError, "ZIP end-of-central-directory record is truncated.");
        return false;
    }

    const std::uint16_t diskNumber = readLE16(archiveBytes, eocdOffset + 4);
    const std::uint16_t centralDirectoryDisk = readLE16(archiveBytes, eocdOffset + 6);
    if(diskNumber != 0 || centralDirectoryDisk != 0){
        setCompressedFileError(outError, "Multi-disk ZIP archives are not supported.");
        return false;
    }

    const std::uint16_t entryCount = readLE16(archiveBytes, eocdOffset + 10);
    const std::uint32_t centralDirectorySize = readLE32(archiveBytes, eocdOffset + 12);
    const std::uint32_t centralDirectoryOffset = readLE32(archiveBytes, eocdOffset + 16);

    if(static_cast<size_t>(centralDirectoryOffset) > archiveBytes.size() ||
       static_cast<size_t>(centralDirectoryOffset) + static_cast<size_t>(centralDirectorySize) > archiveBytes.size()){
        setCompressedFileError(outError, "ZIP central directory points outside the archive.");
        return false;
    }

    entryList.clear();
    entryIndexByPath.clear();
    entryList.reserve(entryCount);

    size_t cursor = static_cast<size_t>(centralDirectoryOffset);
    for(std::uint16_t i = 0; i < entryCount; ++i){
        Entry entry;
        size_t nextOffset = cursor;
        if(!parseZipEntry(cursor, entry, nextOffset, outError)){
            return false;
        }
        if(!entry.path.empty()){
            if(entryIndexByPath.find(entry.path) != entryIndexByPath.end()){
                setCompressedFileError(outError, "Duplicate ZIP entry path: " + entry.path);
                return false;
            }
            entryIndexByPath[entry.path] = entryList.size();
            entryList.push_back(entry);
        }
        cursor = nextOffset;
    }

    return true;
}

bool CompressedFile::locateZipEndOfCentralDirectory(size_t& outOffset, std::string* outError) const{
    outOffset = 0;
    if(archiveBytes.size() < 22){
        setCompressedFileError(outError, "Archive is too small to be a valid ZIP file.");
        return false;
    }

    const size_t searchSpan = std::min<size_t>(archiveBytes.size(), 0xffffu + 22u);
    const size_t searchStart = archiveBytes.size() - searchSpan;
    const size_t initial = archiveBytes.size() - 22;

    for(size_t cursor = initial + 1; cursor-- > searchStart; ){
        if(readLE32(archiveBytes, cursor) == kZipEndOfCentralDirectorySignature){
            const std::uint16_t commentLength = readLE16(archiveBytes, cursor + 20);
            const size_t expectedEnd = cursor + 22 + static_cast<size_t>(commentLength);
            if(expectedEnd == archiveBytes.size()){
                outOffset = cursor;
                return true;
            }
        }
        if(cursor == 0){
            break;
        }
    }

    setCompressedFileError(outError, "ZIP end-of-central-directory record was not found.");
    return false;
}

bool CompressedFile::parseZipEntry(size_t centralDirectoryOffset, Entry& outEntry, size_t& outNextOffset, std::string* outError) const{
    if((centralDirectoryOffset + 46) > archiveBytes.size()){
        setCompressedFileError(outError, "ZIP central directory entry is truncated.");
        return false;
    }
    if(readLE32(archiveBytes, centralDirectoryOffset) != kZipCentralDirectorySignature){
        setCompressedFileError(outError, "ZIP central directory signature mismatch.");
        return false;
    }

    const std::uint16_t generalPurposeFlags = readLE16(archiveBytes, centralDirectoryOffset + 8);
    if((generalPurposeFlags & 0x0001u) != 0u){
        setCompressedFileError(outError, "Encrypted ZIP entries are not supported.");
        return false;
    }

    const std::uint16_t fileNameLength = readLE16(archiveBytes, centralDirectoryOffset + 28);
    const std::uint16_t extraLength = readLE16(archiveBytes, centralDirectoryOffset + 30);
    const std::uint16_t commentLength = readLE16(archiveBytes, centralDirectoryOffset + 32);
    const size_t dataOffset = centralDirectoryOffset + 46;
    const size_t totalLength = 46 + static_cast<size_t>(fileNameLength) + static_cast<size_t>(extraLength) + static_cast<size_t>(commentLength);
    if((centralDirectoryOffset + totalLength) > archiveBytes.size()){
        setCompressedFileError(outError, "ZIP central directory entry overflows the archive.");
        return false;
    }

    std::string rawPath(
        reinterpret_cast<const char*>(archiveBytes.data() + dataOffset),
        reinterpret_cast<const char*>(archiveBytes.data() + dataOffset + fileNameLength)
    );

    outEntry = Entry{};
    outEntry.isDirectory = !rawPath.empty() && (rawPath.back() == '/' || rawPath.back() == '\\');
    outEntry.path = NormalizeEntryPath(rawPath, outEntry.isDirectory);
    outEntry.compressionMethod = readLE16(archiveBytes, centralDirectoryOffset + 10);
    outEntry.crc32 = readLE32(archiveBytes, centralDirectoryOffset + 16);
    outEntry.compressedSize = readLE32(archiveBytes, centralDirectoryOffset + 20);
    outEntry.uncompressedSize = readLE32(archiveBytes, centralDirectoryOffset + 24);
    outEntry.localHeaderOffset = readLE32(archiveBytes, centralDirectoryOffset + 42);

    outNextOffset = centralDirectoryOffset + totalLength;
    return true;
}

bool CompressedFile::readLocalFilePayload(const Entry& entry, const uint8_t*& outData, size_t& outSize, std::string* outError) const{
    outData = nullptr;
    outSize = 0;

    const size_t offset = static_cast<size_t>(entry.localHeaderOffset);
    if((offset + 30) > archiveBytes.size()){
        setCompressedFileError(outError, "ZIP local header is truncated for entry: " + entry.path);
        return false;
    }
    if(readLE32(archiveBytes, offset) != kZipLocalHeaderSignature){
        setCompressedFileError(outError, "ZIP local header signature mismatch for entry: " + entry.path);
        return false;
    }

    const std::uint16_t generalPurposeFlags = readLE16(archiveBytes, offset + 6);
    if((generalPurposeFlags & 0x0001u) != 0u){
        setCompressedFileError(outError, "Encrypted ZIP entries are not supported: " + entry.path);
        return false;
    }

    const std::uint16_t fileNameLength = readLE16(archiveBytes, offset + 26);
    const std::uint16_t extraLength = readLE16(archiveBytes, offset + 28);
    const size_t payloadOffset = offset + 30 + static_cast<size_t>(fileNameLength) + static_cast<size_t>(extraLength);
    const size_t payloadEnd = payloadOffset + static_cast<size_t>(entry.compressedSize);
    if(payloadEnd > archiveBytes.size()){
        setCompressedFileError(outError, "ZIP payload exceeds archive bounds for entry: " + entry.path);
        return false;
    }

    outData = archiveBytes.data() + payloadOffset;
    outSize = static_cast<size_t>(entry.compressedSize);
    return true;
}
