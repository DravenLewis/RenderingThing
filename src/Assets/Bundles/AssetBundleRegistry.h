#ifndef ASSET_BUNDLE_REGISTRY_H
#define ASSET_BUNDLE_REGISTRY_H

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "Assets/Bundles/AssetBundle.h"

class AssetBundleRegistry {
    private:
        std::map<std::string, std::shared_ptr<AssetBundle>> bundlesByAliasKey;
        std::map<std::string, std::string> aliasKeyByBundlePath;

        static std::string NormalizeAliasKey(const std::string& alias);
        static std::string NormalizeBundlePathKey(const std::filesystem::path& path);
        static std::string VirtualEntryPrefix();

    public:
        void clear();
        bool mountBundle(const std::filesystem::path& path, std::string* outError = nullptr);
        void unmountBundle(const std::filesystem::path& path);
        std::shared_ptr<AssetBundle> getBundle(const std::string& alias) const;
        std::shared_ptr<AssetBundle> getBundleByPath(const std::filesystem::path& path) const;
        std::shared_ptr<AssetBundle> getBundleForAssetRef(const std::string& assetRef, std::string* outEntryPath = nullptr) const;
        bool loadAsset(const std::string& assetRef, std::shared_ptr<Asset>& outAsset, std::string* outError = nullptr) const;
        size_t scanKnownLocations(std::string* outError = nullptr);

        static std::filesystem::path MakeVirtualEntryPath(
            const std::filesystem::path& bundlePath,
            const std::string& entryPath,
            bool isDirectory = false);
        static bool DecodeVirtualEntryPath(
            const std::filesystem::path& virtualPath,
            std::filesystem::path& outBundlePath,
            std::string& outEntryPath,
            bool* outIsDirectory = nullptr);
        static bool IsVirtualEntryPath(const std::filesystem::path& path);

        static AssetBundleRegistry Instance;
};

#endif // ASSET_BUNDLE_REGISTRY_H
