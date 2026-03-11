/**
 * @file src/Assets/Bundles/AssetBundleRegistry.h
 * @brief Declarations for AssetBundleRegistry.
 */

#ifndef ASSET_BUNDLE_REGISTRY_H
#define ASSET_BUNDLE_REGISTRY_H

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "Assets/Bundles/AssetBundle.h"

/// @brief Represents the AssetBundleRegistry type.
class AssetBundleRegistry {
    private:
        std::map<std::string, std::shared_ptr<AssetBundle>> bundlesByAliasKey;
        std::map<std::string, std::string> aliasKeyByBundlePath;

        /**
         * @brief Normalizes a bundle alias key.
         * @param alias Value for alias.
         * @return Resulting string value.
         */
        static std::string NormalizeAliasKey(const std::string& alias);
        /**
         * @brief Normalizes a bundle path key.
         * @param path Filesystem path for path.
         * @return Resulting string value.
         */
        static std::string NormalizeBundlePathKey(const std::filesystem::path& path);
        /**
         * @brief Returns the virtual-entry prefix.
         * @return Resulting string value.
         */
        static std::string VirtualEntryPrefix();

    public:
        /**
         * @brief Clears the current state.
         */
        void clear();
        /**
         * @brief Checks whether mount bundle.
         * @param path Filesystem path for path.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool mountBundle(const std::filesystem::path& path, std::string* outError = nullptr);
        /**
         * @brief Unmounts a bundle and removes its virtual entries.
         * @param path Filesystem path for path.
         */
        void unmountBundle(const std::filesystem::path& path);
        /**
         * @brief Returns the bundle.
         * @param alias Value for alias.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<AssetBundle> getBundle(const std::string& alias) const;
        /**
         * @brief Returns the bundle by path.
         * @param path Filesystem path for path.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<AssetBundle> getBundleByPath(const std::filesystem::path& path) const;
        /**
         * @brief Returns the bundle for asset ref.
         * @param assetRef Reference to asset.
         * @param outEntryPath Filesystem path for entry path.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<AssetBundle> getBundleForAssetRef(const std::string& assetRef, std::string* outEntryPath = nullptr) const;
        /**
         * @brief Loads asset.
         * @param assetRef Reference to asset.
         * @param outAsset Output value for asset.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool loadAsset(const std::string& assetRef, std::shared_ptr<Asset>& outAsset, std::string* outError = nullptr) const;
        /**
         * @brief Scans known bundle locations and mounts valid bundles.
         * @param outError Output value for error.
         * @return Computed numeric result.
         */
        size_t scanKnownLocations(std::string* outError = nullptr);

        /**
         * @brief Creates virtual entry path.
         * @param bundlePath Filesystem path for bundle path.
         * @param entryPath Filesystem path for entry path.
         * @param isDirectory Filesystem path for is directory.
         * @return Result of this operation.
         */
        static std::filesystem::path MakeVirtualEntryPath(
            const std::filesystem::path& bundlePath,
            const std::string& entryPath,
            bool isDirectory = false);
        /**
         * @brief Checks whether decode virtual entry path.
         * @param virtualPath Filesystem path for virtual path.
         * @param outBundlePath Filesystem path for bundle path.
         * @param outEntryPath Filesystem path for entry path.
         * @param outIsDirectory Filesystem path for is directory.
         * @return True when the operation succeeds; otherwise false.
         */
        static bool DecodeVirtualEntryPath(
            const std::filesystem::path& virtualPath,
            std::filesystem::path& outBundlePath,
            std::string& outEntryPath,
            bool* outIsDirectory = nullptr);
        /**
         * @brief Checks whether virtual entry path.
         * @param path Filesystem path for path.
         * @return True when the condition is satisfied; otherwise false.
         */
        static bool IsVirtualEntryPath(const std::filesystem::path& path);

        static AssetBundleRegistry Instance;
};

#endif // ASSET_BUNDLE_REGISTRY_H
