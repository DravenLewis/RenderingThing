/**
 * @file src/Assets/Bundles/AssetBundle.cpp
 * @brief Implementation for AssetBundle.
 */

#include "Assets/Bundles/AssetBundle.h"

#include "Assets/Core/AssetDescriptorUtils.h"
#include "Foundation/Logging/Logbot.h"
#include "Foundation/Util/StringUtils.h"

#include <set>
#include <sstream>

namespace {

inline static Logbot assetBundleLogger = Logbot::CreateInstance("AssetBundle");

void setAssetBundleError(std::string* outError, const std::string& message){
    if(outError){
        *outError = message;
    }
}

std::string stripBundleSuffix(const std::filesystem::path& path){
    std::string name = path.filename().string();
    const std::string lower = StringUtils::ToLowerCase(name);
    static const std::string kSuffix = ".bundle.asset";
    if(StringUtils::EndsWith(lower, kSuffix) && name.size() > kSuffix.size()){
        return name.substr(0, name.size() - kSuffix.size());
    }
    return path.stem().string();
}

std::string crcToHexString(const BinaryBuffer& data){
    std::ostringstream stream;
    stream << "crc32:";
    stream.setf(std::ios::hex, std::ios::basefield);
    stream.setf(std::ios::uppercase);
    stream.width(8);
    stream.fill('0');
    stream << CompressedFile::ComputeCRC32(data);
    return stream.str();
}

std::string compressionNameFromMethod(std::uint16_t method){
    switch(method){
        case 0: return "stored";
        case 8: return "deflate";
        default: return "zip-" + std::to_string(method);
    }
}

} // namespace

bool AssetBundle::IsBundlePath(const std::filesystem::path& path){
    return StringUtils::EndsWith(StringUtils::ToLowerCase(path.generic_string()), ".bundle.asset");
}

std::string AssetBundle::NormalizeAlias(const std::string& alias){
    std::string normalized = StringUtils::Trim(alias);
    if(!normalized.empty() && normalized[0] == '@'){
        normalized.erase(normalized.begin());
    }

    normalized = StringUtils::ReplaceAll(normalized.c_str(), "\\", "/");
    const size_t slashPos = normalized.find('/');
    if(slashPos != std::string::npos){
        normalized = normalized.substr(0, slashPos);
    }

    for(char& c : normalized){
        const bool keep =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' ||
            c == '-';
        if(!keep){
            c = '_';
        }
    }

    return StringUtils::Trim(normalized);
}

void AssetBundle::clear(){
    bundlePath.clear();
    bundleAlias.clear();
    manifest.Clear();
    archive = CompressedFile{};
    cachedEntryData.clear();
}

bool AssetBundle::open(const std::filesystem::path& path, std::string* outError){
    clear();

    if(!IsBundlePath(path)){
        setAssetBundleError(outError, "Asset bundles must use the .bundle.asset extension.");
        return false;
    }

    CompressedFile nextArchive;
    if(!nextArchive.open(path, outError)){
        return false;
    }

    BinaryBuffer manifestBytes;
    if(!nextArchive.readEntry(ManifestEntryPath, manifestBytes, outError)){
        setAssetBundleError(outError, "Asset bundle is missing the /.MANIFEST entry.");
        return false;
    }

    JsonSchema::AssetManifestSchema nextManifest;
    const std::string manifestText(manifestBytes.begin(), manifestBytes.end());
    if(!nextManifest.LoadFromText(manifestText, outError)){
        return false;
    }

    std::string nextAlias = NormalizeAlias(nextManifest.bundleAlias);
    if(nextAlias.empty()){
        nextAlias = NormalizeAlias(stripBundleSuffix(path));
    }
    if(nextAlias.empty()){
        setAssetBundleError(outError, "Asset bundle alias resolved to an empty value.");
        return false;
    }
    nextManifest.bundleAlias = nextAlias;
    nextManifest.rootEntry = CompressedFile::NormalizeEntryPath(nextManifest.rootEntry);

    bundlePath = path;
    bundleAlias = nextAlias;
    manifest = nextManifest;
    archive = nextArchive;

    if(!validateArchiveEntries(outError)){
        clear();
        return false;
    }

    return true;
}

bool AssetBundle::createEmpty(const std::filesystem::path& path, const std::string& alias, std::string* outError){
    clear();

    if(!IsBundlePath(path)){
        setAssetBundleError(outError, "Asset bundles must use the .bundle.asset extension.");
        return false;
    }

    bundlePath = path;
    if(!setAlias(alias.empty() ? stripBundleSuffix(path) : alias, outError)){
        clear();
        return false;
    }
    manifest.rootEntry.clear();
    manifest.entries.clear();

    if(!rebuildArchive(outError)){
        clear();
        return false;
    }

    return open(path, outError);
}

bool AssetBundle::save(std::string* outError){
    if(bundlePath.empty()){
        setAssetBundleError(outError, "Asset bundle has not been opened yet.");
        return false;
    }
    if(!rebuildArchive(outError)){
        return false;
    }

    // Copy the path before re-opening because open() clears object state first.
    const std::filesystem::path reopenPath = bundlePath;
    return open(reopenPath, outError);
}

std::string AssetBundle::aliasToken() const{
    if(bundleAlias.empty()){
        return std::string();
    }
    return "@" + bundleAlias;
}

bool AssetBundle::setAlias(const std::string& alias, std::string* outError){
    const std::string normalized = NormalizeAlias(alias);
    if(normalized.empty()){
        setAssetBundleError(outError, "Asset bundle alias must contain at least one valid character.");
        return false;
    }

    bundleAlias = normalized;
    manifest.bundleAlias = normalized;
    return true;
}

bool AssetBundle::setRootEntry(const std::string& entryPath, std::string* outError){
    if(entryPath.empty()){
        manifest.rootEntry.clear();
        return true;
    }

    const std::string normalized = CompressedFile::NormalizeEntryPath(entryPath);
    if(normalized.empty()){
        setAssetBundleError(outError, "Root entry path is invalid.");
        return false;
    }
    if(!findManifestEntry(normalized)){
        setAssetBundleError(outError, "Root entry was not found in the bundle manifest: " + normalized);
        return false;
    }

    manifest.rootEntry = normalized;
    return true;
}

bool AssetBundle::readEntryBytes(const std::string& entryPath, BinaryBuffer& outData, std::string* outError){
    outData.clear();

    const std::string normalized = CompressedFile::NormalizeEntryPath(entryPath);
    if(normalized.empty()){
        setAssetBundleError(outError, "Bundle entry path is invalid.");
        return false;
    }

    if(normalized == ManifestEntryPath){
        std::string manifestText;
        if(!manifest.WriteToString(manifestText, outError, true)){
            return false;
        }
        outData.assign(manifestText.begin(), manifestText.end());
        return true;
    }

    const JsonSchema::AssetManifestSchema::Entry* manifestEntry = findManifestEntry(normalized);
    if(!manifestEntry){
        setAssetBundleError(outError, "Bundle entry was not found in the manifest: " + normalized);
        return false;
    }
    if(manifestEntry->kind == "directory"){
        return true;
    }

    auto cached = cachedEntryData.find(normalized);
    if(cached != cachedEntryData.end()){
        outData = cached->second;
        return true;
    }

    if(!archive.readEntry(normalized, outData, outError)){
        return false;
    }
    cachedEntryData[normalized] = outData;
    return true;
}

bool AssetBundle::readEntryText(const std::string& entryPath, std::string& outText, std::string* outError){
    BinaryBuffer data;
    if(!readEntryBytes(entryPath, data, outError)){
        return false;
    }
    outText.assign(data.begin(), data.end());
    return true;
}

std::shared_ptr<Asset> AssetBundle::loadAsset(const std::string& entryPath, std::string* outError){
    const std::string normalized = CompressedFile::NormalizeEntryPath(entryPath);
    const JsonSchema::AssetManifestSchema::Entry* manifestEntry = findManifestEntry(normalized);
    if(manifestEntry && manifestEntry->kind == "directory"){
        setAssetBundleError(outError, "Bundle directories cannot be loaded as file assets: " + normalized);
        return nullptr;
    }

    BinaryBuffer data;
    if(!readEntryBytes(normalized, data, outError)){
        return nullptr;
    }
    auto inMemoryFile = std::make_unique<File>(aliasToken() + "/" + normalized, data);
    if(!inMemoryFile){
        setAssetBundleError(outError, "Failed to create in-memory file for bundle entry.");
        return nullptr;
    }

    return std::make_shared<Asset>(std::move(inMemoryFile));
}

bool AssetBundle::addOrUpdateFileFromBuffer(
    const std::string& entryPath,
    const BinaryBuffer& data,
    const std::string& sourceRef,
    std::string* outError)
{
    const std::string normalized = CompressedFile::NormalizeEntryPath(entryPath);
    if(normalized.empty()){
        setAssetBundleError(outError, "Bundle entry path is invalid.");
        return false;
    }
    if(normalized == ManifestEntryPath){
        setAssetBundleError(outError, "The /.MANIFEST entry is reserved.");
        return false;
    }

    ensureParentDirectories(normalized);

    JsonSchema::AssetManifestSchema::Entry* entry = findManifestEntry(normalized);
    if(!entry){
        manifest.entries.push_back(JsonSchema::AssetManifestSchema::Entry{});
        entry = &manifest.entries.back();
    }

    entry->path = normalized;
    entry->kind = "file";
    entry->sourceRef = sourceRef;
    entry->size = static_cast<std::uint64_t>(data.size());
    entry->hash = crcToHexString(data);
    entry->compression.clear();

    cachedEntryData[normalized] = data;
    if(manifest.rootEntry.empty()){
        manifest.rootEntry = normalized;
    }
    return true;
}

bool AssetBundle::addOrUpdateFileFromAssetRef(const std::string& entryPath, const std::string& assetRef, std::string* outError){
    auto asset = AssetManager::Instance.getOrLoad(assetRef);
    if(!asset){
        setAssetBundleError(outError, "Failed to load source asset: " + assetRef);
        return false;
    }
    return addOrUpdateFileFromBuffer(entryPath, asset->asRaw(), assetRef, outError);
}

bool AssetBundle::addOrUpdateFileFromAbsolutePath(
    const std::string& entryPath,
    const std::filesystem::path& sourcePath,
    std::string* outError)
{
    auto asset = std::make_shared<Asset>(sourcePath.string());
    if(!asset || !asset->load()){
        setAssetBundleError(outError, "Failed to load source file: " + sourcePath.generic_string());
        return false;
    }
    return addOrUpdateFileFromBuffer(
        entryPath,
        asset->asRaw(),
        AssetDescriptorUtils::AbsolutePathToAssetRef(sourcePath),
        outError
    );
}

bool AssetBundle::ensureDirectory(const std::string& entryPath, std::string* outError){
    const std::string normalized = CompressedFile::NormalizeEntryPath(entryPath, true);
    if(normalized.empty()){
        setAssetBundleError(outError, "Bundle directory path is invalid.");
        return false;
    }
    if(normalized == ManifestEntryPath){
        setAssetBundleError(outError, "The /.MANIFEST entry is reserved.");
        return false;
    }

    ensureParentDirectories(normalized);

    JsonSchema::AssetManifestSchema::Entry* entry = findManifestEntry(normalized);
    if(!entry){
        manifest.entries.push_back(JsonSchema::AssetManifestSchema::Entry{});
        entry = &manifest.entries.back();
    }

    entry->path = normalized;
    entry->kind = "directory";
    entry->sourceRef.clear();
    entry->size = 0;
    entry->hash.clear();
    entry->compression.clear();
    cachedEntryData.erase(normalized);
    return true;
}

bool AssetBundle::renameEntry(const std::string& entryPath, const std::string& newEntryPath, std::string* outError){
    const std::string normalizedFile = CompressedFile::NormalizeEntryPath(entryPath);
    const std::string normalizedDir = CompressedFile::NormalizeEntryPath(entryPath, true);
    const JsonSchema::AssetManifestSchema::Entry* oldDirEntry = normalizedDir.empty() ? nullptr : findManifestEntry(normalizedDir);
    const bool isDirectory = (oldDirEntry && oldDirEntry->kind == "directory");

    const std::string oldPath = isDirectory ? normalizedDir : normalizedFile;
    if(oldPath.empty() || !findManifestEntry(oldPath)){
        setAssetBundleError(outError, "Bundle entry was not found: " + entryPath);
        return false;
    }

    const std::string newPath = isDirectory
        ? CompressedFile::NormalizeEntryPath(newEntryPath, true)
        : CompressedFile::NormalizeEntryPath(newEntryPath);
    if(newPath.empty()){
        setAssetBundleError(outError, "Bundle entry path is invalid.");
        return false;
    }
    if(newPath == ManifestEntryPath){
        setAssetBundleError(outError, "The /.MANIFEST entry is reserved.");
        return false;
    }
    if(newPath == oldPath){
        return true;
    }

    const std::string newFilePath = CompressedFile::NormalizeEntryPath(newEntryPath);
    const std::string newDirPath = CompressedFile::NormalizeEntryPath(newEntryPath, true);
    if(isDirectory){
        if(StringUtils::BeginsWith(newPath, oldPath)){
            setAssetBundleError(outError, "Cannot rename a directory into itself.");
            return false;
        }

        for(const auto& entry : manifest.entries){
            const bool isMovedEntry = (entry.path == oldPath) || StringUtils::BeginsWith(entry.path, oldPath);
            if(isMovedEntry){
                continue;
            }
            if(entry.path == newPath ||
               (!newFilePath.empty() && entry.path == newFilePath) ||
               StringUtils::BeginsWith(entry.path, newPath)){
                setAssetBundleError(outError, "Rename cancelled: target already exists (" + newPath + ").");
                return false;
            }
        }
    }else{
        if((!newFilePath.empty() && findManifestEntry(newFilePath)) ||
           (!newDirPath.empty() && findManifestEntry(newDirPath))){
            setAssetBundleError(outError, "Rename cancelled: target already exists (" + newPath + ").");
            return false;
        }
    }

    ensureParentDirectories(newPath);

    if(isDirectory){
        for(auto& entry : manifest.entries){
            if(entry.path != oldPath && !StringUtils::BeginsWith(entry.path, oldPath)){
                continue;
            }

            const std::string originalPath = entry.path;
            const std::string suffix = originalPath.substr(oldPath.size());
            entry.path = newPath + suffix;

            auto cachedIt = cachedEntryData.find(originalPath);
            if(cachedIt != cachedEntryData.end()){
                cachedEntryData[entry.path] = cachedIt->second;
                cachedEntryData.erase(cachedIt);
            }
        }

        if(!manifest.rootEntry.empty() &&
           (manifest.rootEntry == oldPath || StringUtils::BeginsWith(manifest.rootEntry, oldPath))){
            manifest.rootEntry = newPath + manifest.rootEntry.substr(oldPath.size());
        }
    }else{
        JsonSchema::AssetManifestSchema::Entry* entry = findManifestEntry(oldPath);
        if(!entry){
            setAssetBundleError(outError, "Bundle entry was not found: " + oldPath);
            return false;
        }

        entry->path = newPath;
        auto cachedIt = cachedEntryData.find(oldPath);
        if(cachedIt != cachedEntryData.end()){
            cachedEntryData[newPath] = cachedIt->second;
            cachedEntryData.erase(cachedIt);
        }

        if(manifest.rootEntry == oldPath){
            manifest.rootEntry = newPath;
        }
    }

    pruneEmptyDirectories();
    return true;
}

bool AssetBundle::removeEntry(const std::string& entryPath, std::string* outError){
    const std::string normalizedFile = CompressedFile::NormalizeEntryPath(entryPath);
    const std::string normalizedDir = CompressedFile::NormalizeEntryPath(entryPath, true);
    if(normalizedFile.empty() && normalizedDir.empty()){
        setAssetBundleError(outError, "Bundle entry path is invalid.");
        return false;
    }

    const bool removeDirectory = (!normalizedDir.empty() && findManifestEntry(normalizedDir) != nullptr);
    const std::string prefix = removeDirectory ? normalizedDir : std::string();

    bool removedAny = false;
    for(auto it = manifest.entries.begin(); it != manifest.entries.end();){
        const bool removeCurrent =
            (it->path == normalizedFile) ||
            (it->path == normalizedDir) ||
            (!prefix.empty() && StringUtils::BeginsWith(it->path, prefix));

        if(removeCurrent){
            cachedEntryData.erase(it->path);
            it = manifest.entries.erase(it);
            removedAny = true;
        }else{
            ++it;
        }
    }

    if(!removedAny){
        setAssetBundleError(outError, "Bundle entry was not found: " + normalizedFile);
        return false;
    }

    if(!manifest.rootEntry.empty()){
        if(manifest.rootEntry == normalizedFile ||
           manifest.rootEntry == normalizedDir ||
           (!prefix.empty() && StringUtils::BeginsWith(manifest.rootEntry, prefix))){
            manifest.rootEntry.clear();
        }
    }

    pruneEmptyDirectories();
    return true;
}

JsonSchema::AssetManifestSchema::Entry* AssetBundle::findManifestEntry(const std::string& entryPath){
    for(auto& entry : manifest.entries){
        if(entry.path == entryPath){
            return &entry;
        }
    }
    return nullptr;
}

const JsonSchema::AssetManifestSchema::Entry* AssetBundle::findManifestEntry(const std::string& entryPath) const{
    for(const auto& entry : manifest.entries){
        if(entry.path == entryPath){
            return &entry;
        }
    }
    return nullptr;
}

void AssetBundle::ensureParentDirectories(const std::string& entryPath){
    std::filesystem::path cursor = std::filesystem::path(entryPath).parent_path();
    while(!cursor.empty() && cursor.generic_string() != "."){
        const std::string normalized = CompressedFile::NormalizeEntryPath(cursor.generic_string(), true);
        if(!normalized.empty() && !findManifestEntry(normalized)){
            JsonSchema::AssetManifestSchema::Entry entry;
            entry.path = normalized;
            entry.kind = "directory";
            manifest.entries.push_back(entry);
        }
        cursor = cursor.parent_path();
    }
}

void AssetBundle::pruneEmptyDirectories(){
    std::set<std::string> requiredDirectories;

    auto collectParents = [&](const std::string& path){
        std::filesystem::path cursor = std::filesystem::path(path).parent_path();
        while(!cursor.empty() && cursor.generic_string() != "."){
            const std::string normalized = CompressedFile::NormalizeEntryPath(cursor.generic_string(), true);
            if(normalized.empty()){
                break;
            }
            requiredDirectories.insert(normalized);
            cursor = cursor.parent_path();
        }
    };

    if(!manifest.rootEntry.empty()){
        const JsonSchema::AssetManifestSchema::Entry* root = findManifestEntry(manifest.rootEntry);
        if(root && root->kind == "directory"){
            requiredDirectories.insert(CompressedFile::NormalizeEntryPath(root->path, true));
        }
        collectParents(manifest.rootEntry);
    }

    for(const auto& entry : manifest.entries){
        collectParents(entry.path);
    }

    for(auto it = manifest.entries.begin(); it != manifest.entries.end();){
        if(it->kind == "directory" && requiredDirectories.find(it->path) == requiredDirectories.end()){
            cachedEntryData.erase(it->path);
            it = manifest.entries.erase(it);
        }else{
            ++it;
        }
    }
}

bool AssetBundle::validateArchiveEntries(std::string* outError){
    std::set<std::string> normalizedPaths;

    for(auto& entry : manifest.entries){
        if(entry.kind == "directory"){
            entry.path = CompressedFile::NormalizeEntryPath(entry.path, true);
            if(entry.path.empty() || !normalizedPaths.insert(entry.path).second){
                setAssetBundleError(outError, "Manifest contains duplicate or invalid entry path: " + entry.path);
                return false;
            }
            continue;
        }

        entry.path = CompressedFile::NormalizeEntryPath(entry.path);
        if(entry.path.empty() || !normalizedPaths.insert(entry.path).second){
            setAssetBundleError(outError, "Manifest contains duplicate or invalid entry path: " + entry.path);
            return false;
        }
        const CompressedFile::Entry* archiveEntry = archive.findEntry(entry.path);
        if(!archiveEntry){
            setAssetBundleError(outError, "Manifest entry is missing from the archive: " + entry.path);
            return false;
        }

        entry.size = archiveEntry->uncompressedSize;
        entry.hash = "crc32:" + StringUtils::ToUpperCase(StringUtils::Format("%08x", archiveEntry->crc32));
        entry.compression = compressionNameFromMethod(archiveEntry->compressionMethod);
    }

    if(!manifest.rootEntry.empty() && !findManifestEntry(manifest.rootEntry)){
        setAssetBundleError(outError, "Bundle rootEntry was not found in the manifest: " + manifest.rootEntry);
        return false;
    }

    return true;
}

bool AssetBundle::rebuildArchive(std::string* outError){
    if(bundlePath.empty()){
        setAssetBundleError(outError, "Asset bundle path is empty.");
        return false;
    }

    JsonSchema::AssetManifestSchema manifestToWrite = manifest;
    manifestToWrite.bundleAlias = bundleAlias;
    manifestToWrite.rootEntry = CompressedFile::NormalizeEntryPath(manifestToWrite.rootEntry);

    std::vector<CompressedFile::WriteEntry> writeEntries;
    writeEntries.reserve(manifestToWrite.entries.size() + 1);

    for(auto& entry : manifestToWrite.entries){
        if(entry.kind == "directory"){
            entry.path = CompressedFile::NormalizeEntryPath(entry.path, true);
            entry.size = 0;
            entry.hash.clear();
            entry.compression.clear();

            CompressedFile::WriteEntry writeEntry;
            writeEntry.path = entry.path;
            writeEntry.isDirectory = true;
            writeEntries.push_back(writeEntry);
            continue;
        }

        entry.path = CompressedFile::NormalizeEntryPath(entry.path);
        BinaryBuffer data;
        if(!readEntryBytes(entry.path, data, outError)){
            return false;
        }

        entry.size = static_cast<std::uint64_t>(data.size());
        entry.hash = crcToHexString(data);
        entry.compression.clear();

        CompressedFile::WriteEntry writeEntry;
        writeEntry.path = entry.path;
        writeEntry.data = data;
        writeEntry.isDirectory = false;
        writeEntry.preferCompression = true;
        writeEntries.push_back(writeEntry);
    }

    std::string manifestJson;
    if(!manifestToWrite.WriteToString(manifestJson, outError, true)){
        return false;
    }

    CompressedFile::WriteEntry manifestWriteEntry;
    manifestWriteEntry.path = ManifestEntryPath;
    manifestWriteEntry.data.assign(manifestJson.begin(), manifestJson.end());
    manifestWriteEntry.preferCompression = true;
    writeEntries.insert(writeEntries.begin(), manifestWriteEntry);

    return archive.writeToPath(bundlePath, writeEntries, outError);
}
