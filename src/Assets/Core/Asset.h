/**
 * @file src/Assets/Core/Asset.h
 * @brief Declarations for Asset.
 */


#ifndef ASSET_H
#define ASSET_H

#include "Foundation/Logging/Logbot.h"

#include "Foundation/IO/File.h"
#include "Foundation/Util/StringUtils.h"

#include <memory>
#include <map>
#include <filesystem>
#include <functional>


#define ASSET_DELIMITER "@assets"

#define VERBOSE false


/// @brief Represents the Asset type.
class Asset{
    private:
        std::unique_ptr<File> fileHandle; // This file owns the pointer.
        FileBlob fblob;
        bool isLoaded = false;
        std::string cacheKey;

    public:
        /**
         * @brief Constructs a new Asset instance.
         */
        Asset() = delete;
        /**
         * @brief Constructs a new Asset instance.
         * @param currentFileHandle Owned file handle backing this asset.
         */
        Asset(std::unique_ptr<File> currentFileHandle);
        /**
         * @brief Constructs a new Asset instance.
         * @param path Filesystem path for path.
         */
        Asset(std::string path);
        /**
         * @brief Destroys this Asset instance.
         */
        ~Asset();
        /**
         * @brief Returns the owned file handle.
         * @return Reference to the internal unique file handle.
         */
        std::unique_ptr<File>& getFileHandle();
        /**
         * @brief Loads data from storage.
         * @return True when the operation succeeds; otherwise false.
         */
        bool load();

        /**
         * @brief Returns loaded asset bytes as a text string.
         * @return Asset contents interpreted as a string.
         */
        std::string asString();
        /**
         * @brief Returns loaded asset bytes.
         * @return Raw binary asset buffer.
         */
        BinaryBuffer asRaw();
        /**
         * @brief Sets the cache key.
         * @param key Value for key.
         */
        void setCacheKey(const std::string& key);
        /**
         * @brief Returns the normalized cache key for this asset.
         * @return Cache key string.
         */
        const std::string& getCacheKey() const;
        
        /**
         * @brief Returns whether this asset payload is loaded.
         * @return True when the operation succeeds; otherwise false.
         */
        inline bool loaded() const {return isLoaded;};

        /**
         * @brief Loads all assets in a pack.
         * @param pack Asset pack to load.
         */
        typedef std::vector<std::shared_ptr<Asset>> AssetPack;
        static void LoadAssets(AssetPack &pack);
        /**
         * @brief Creates a deep copy of an asset value.
         * @param asset Asset to copy.
         * @return Copied asset value.
         */
        static Asset CopyAsset(const Asset &asset);
};

/// @brief Represents the AssetManager type.
class AssetManager{
    public:
        /// @brief Holds data for ResolvedRequest.
        struct ResolvedRequest {
            std::string cacheKey;
            std::function<std::shared_ptr<Asset>()> loader;
        };

        using AliasResolver = std::function<bool(const std::string& request, ResolvedRequest& outResolved, std::string* outError)>;

    private:
        std::map<std::string, std::shared_ptr<Asset>> assetMap;
        std::map<std::string, AliasResolver> aliasResolvers;

        /**
         * @brief Normalizes an alias token (`@assets`, `@bundle`, etc.) for map lookup.
         * @param alias Value for alias.
         * @return Resulting string value.
         */
        static std::string NormalizeAliasToken(const std::string& alias);
        /**
         * @brief Extracts alias and remainder segments from an asset request.
         * @param request Value for request.
         * @param outAlias Output value for alias.
         * @param outRemainder Output value for remainder.
         * @return True when the operation succeeds; otherwise false.
         */
        static bool ExtractAliasToken(const std::string& request, std::string& outAlias, std::string& outRemainder);
        /**
         * @brief Normalizes a filesystem path for deterministic cache keys.
         * @param name Name used for name.
         * @return Resulting string value.
         */
        static std::string NormalizeFileSystemKey(const std::string& name);
        /**
         * @brief Resolves an asset request into a loader and cache key.
         * @param name Name used for name.
         * @param outResolved Output value for resolved.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool resolveRequest(const std::string& name, ResolvedRequest& outResolved, std::string* outError = nullptr) const;
        /**
         * @brief Registers built in providers.
         */
        void registerBuiltInProviders();

    public:
        /**
         * @brief Constructs a new AssetManager instance.
         */
        AssetManager();
        /**
         * @brief Registers an asset in the manager cache.
         * @param assetPtr Asset to cache.
         */
        void manageAsset(std::shared_ptr<Asset> assetPtr);
        /**
         * @brief Removes an asset cache entry by key.
         * @param name Asset cache key or request name.
         */
        void unmanageAsset(const std::string& name);
        /**
         * @brief Removes cached assets resolved through a specific alias provider.
         * @param alias Alias token to evict.
         */
        void unmanageAliasAssets(const std::string& alias);
        /**
         * @brief Returns whether an asset is already cached.
         * @param name Asset cache key or request name.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool hasAsset(const std::string& name);
        /**
         * @brief Returns a cached asset, loading and caching it on demand.
         * @param name Asset cache key or request name.
         * @return Shared pointer to the resolved asset.
         */
        std::shared_ptr<Asset> getOrLoad(const std::string& name);
        /**
         * @brief Registers alias provider.
         * @param alias Value for alias.
         * @param resolver Value for resolver.
         * @return True when the operation succeeds; otherwise false.
         */
        bool registerAliasProvider(const std::string& alias, const AliasResolver& resolver);
        /**
         * @brief Unregisters alias provider.
         * @param alias Value for alias.
         */
        void unregisterAliasProvider(const std::string& alias);
        /**
         * @brief Returns whether an alias provider is registered.
         * @param alias Alias token to query.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool hasAliasProvider(const std::string& alias) const;

        static AssetManager Instance;
        
};

typedef std::shared_ptr<Asset> PAsset;


#endif // ASSET_H
