#ifndef COMPRESSED_FILE_H
#define COMPRESSED_FILE_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Foundation/Util/Types.h"

class CompressedFile {
    public:
        enum class Format : int {
            Unknown = 0,
            Zip
        };

        struct Entry {
            std::string path;
            std::uint16_t compressionMethod = 0;
            std::uint32_t crc32 = 0;
            std::uint32_t compressedSize = 0;
            std::uint32_t uncompressedSize = 0;
            std::uint32_t localHeaderOffset = 0;
            bool isDirectory = false;
        };

        struct WriteEntry {
            std::string path;
            BinaryBuffer data;
            bool isDirectory = false;
            bool preferCompression = true;
        };

    private:
        Format currentFormat = Format::Unknown;
        std::filesystem::path archivePath;
        BinaryBuffer archiveBytes;
        std::vector<Entry> entryList;
        std::map<std::string, size_t> entryIndexByPath;

        bool parseZip(std::string* outError);
        bool locateZipEndOfCentralDirectory(size_t& outOffset, std::string* outError) const;
        bool parseZipEntry(size_t centralDirectoryOffset, Entry& outEntry, size_t& outNextOffset, std::string* outError) const;
        bool readLocalFilePayload(const Entry& entry, const uint8_t*& outData, size_t& outSize, std::string* outError) const;

    public:
        CompressedFile() = default;

        bool open(const std::filesystem::path& path, std::string* outError = nullptr);
        bool write(const std::vector<WriteEntry>& entries, std::string* outError = nullptr);
        bool writeToPath(const std::filesystem::path& path, const std::vector<WriteEntry>& entries, std::string* outError = nullptr);

        bool isOpen() const { return !archivePath.empty(); }
        Format format() const { return currentFormat; }
        const std::filesystem::path& path() const { return archivePath; }
        const std::vector<Entry>& entries() const { return entryList; }
        bool hasEntry(const std::string& entryPath) const;
        const Entry* findEntry(const std::string& entryPath) const;
        bool readEntry(const std::string& entryPath, BinaryBuffer& outData, std::string* outError = nullptr) const;

        static std::string NormalizeEntryPath(const std::string& path, bool forceDirectory = false);
        static std::uint32_t ComputeCRC32(const uint8_t* data, size_t size);
        static std::uint32_t ComputeCRC32(const BinaryBuffer& data);
    };

#endif // COMPRESSED_FILE_H
