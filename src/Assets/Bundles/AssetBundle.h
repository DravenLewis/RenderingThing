/**
 * @file src/Assets/Bundles/AssetBundle.h
 * @brief Declarations for AssetBundle.
 */

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

/// @brief Represents the AssetBundle type.
class AssetBundle {
    private:
        std::filesystem::path bundlePath;
        std::string bundleAlias;
        JsonSchema::AssetManifestSchema manifest;
        CompressedFile archive;
        std::map<std::string, BinaryBuffer> cachedEntryData;

        /**
         * @brief Finds a manifest entry by key.
         * @param entryPath Filesystem path for entry path.
         * @return Pointer to the resulting object.
         */
        JsonSchema::AssetManifestSchema::Entry* findManifestEntry(const std::string& entryPath);
        /**
         * @brief Finds a manifest entry by key.
         * @param entryPath Filesystem path for entry path.
         * @return Pointer to the resulting object.
         */
        const JsonSchema::AssetManifestSchema::Entry* findManifestEntry(const std::string& entryPath) const;
        /**
         * @brief Ensures parent directories.
         * @param entryPath Filesystem path for entry path.
         */
        void ensureParentDirectories(const std::string& entryPath);
        /**
         * @brief Removes empty directories from a path list.
         */
        void pruneEmptyDirectories();
        /**
         * @brief Checks whether validate archive entries.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool validateArchiveEntries(std::string* outError);
        /**
         * @brief Checks whether rebuild archive.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool rebuildArchive(std::string* outError);

    public:
        static constexpr const char* ManifestEntryPath = ".MANIFEST";

        /**
         * @brief Checks whether bundle path.
         * @param path Filesystem path for path.
         * @return True when the condition is satisfied; otherwise false.
         */
        static bool IsBundlePath(const std::filesystem::path& path);
        /**
         * @brief Normalizes a bundle alias.
         * @param alias Value for alias.
         * @return Resulting string value.
         */
        static std::string NormalizeAlias(const std::string& alias);

        /**
         * @brief Clears the current state.
         */
        void clear();
        /**
         * @brief Opens this object.
         * @param path Filesystem path for path.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool open(const std::filesystem::path& path, std::string* outError = nullptr);
        /**
         * @brief Creates empty.
         * @param path Filesystem path for path.
         * @param alias Value for alias.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool createEmpty(const std::filesystem::path& path, const std::string& alias, std::string* outError = nullptr);
        /**
         * @brief Saves data to storage.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool save(std::string* outError = nullptr);

        /**
         * @brief Checks whether loaded.
         * @return True when the condition is satisfied; otherwise false.
         */
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
