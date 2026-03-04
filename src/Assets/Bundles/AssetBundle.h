#ifndef ASSET_BUNDLE_H
#define ASSET_BUNDLE_H

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Assets/Core/Asset.h"
#include "Foundation/Compression/CompressedFile.h"
#include "Serialization/Schema/ManifestSchemas.h"

class AssetBundle {
    private:
        std::filesystem::path bundlePath;
        std::string bundleAlias;
        JsonSchema::AssetManifestSchema manifest;
        CompressedFile archive;
        std::map<std::string, BinaryBuffer> cachedEntryData;

        JsonSchema::AssetManifestSchema::Entry* findManifestEntry(const std::string& entryPath);
        const JsonSchema::AssetManifestSchema::Entry* findManifestEntry(const std::string& entryPath) const;
        void ensureParentDirectories(const std::string& entryPath);
        void pruneEmptyDirectories();
        bool validateArchiveEntries(std::string* outError);
        bool rebuildArchive(std::string* outError);

    public:
        static constexpr const char* ManifestEntryPath = ".MANIFEST";

        static bool IsBundlePath(const std::filesystem::path& path);
        static std::string NormalizeAlias(const std::string& alias);

        void clear();
        bool open(const std::filesystem::path& path, std::string* outError = nullptr);
        bool createEmpty(const std::filesystem::path& path, const std::string& alias, std::string* outError = nullptr);
        bool save(std::string* outError = nullptr);

        bool isLoaded() const { return !bundlePath.empty(); }
        const std::filesystem::path& path() const { return bundlePath; }
        const std::string& alias() const { return bundleAlias; }
        std::string aliasToken() const;
        const JsonSchema::AssetManifestSchema& getManifest() const { return manifest; }
        const std::vector<JsonSchema::AssetManifestSchema::Entry>& getEntries() const { return manifest.entries; }
        const std::string& rootEntry() const { return manifest.rootEntry; }

        bool setAlias(const std::string& alias, std::string* outError = nullptr);
        bool setRootEntry(const std::string& entryPath, std::string* outError = nullptr);

        bool readEntryBytes(const std::string& entryPath, BinaryBuffer& outData, std::string* outError = nullptr);
        bool readEntryText(const std::string& entryPath, std::string& outText, std::string* outError = nullptr);
        std::shared_ptr<Asset> loadAsset(const std::string& entryPath, std::string* outError = nullptr);

        bool addOrUpdateFileFromBuffer(
            const std::string& entryPath,
            const BinaryBuffer& data,
            const std::string& sourceRef = std::string(),
            std::string* outError = nullptr);
        bool addOrUpdateFileFromAssetRef(
            const std::string& entryPath,
            const std::string& assetRef,
            std::string* outError = nullptr);
        bool addOrUpdateFileFromAbsolutePath(
            const std::string& entryPath,
            const std::filesystem::path& sourcePath,
            std::string* outError = nullptr);
        bool ensureDirectory(const std::string& entryPath, std::string* outError = nullptr);
        bool renameEntry(
            const std::string& entryPath,
            const std::string& newEntryPath,
            std::string* outError = nullptr);
        bool removeEntry(const std::string& entryPath, std::string* outError = nullptr);
    };

#endif // ASSET_BUNDLE_H
