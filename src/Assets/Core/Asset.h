
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


class Asset{
    private:
        std::unique_ptr<File> fileHandle; // This file owns the pointer.
        FileBlob fblob;
        bool isLoaded = false;
        std::string cacheKey;

    public:
        Asset() = delete;
        Asset(std::unique_ptr<File> currentFileHandle);
        Asset(std::string path);
        ~Asset();
        std::unique_ptr<File>& getFileHandle();
        bool load();

        std::string asString();
        BinaryBuffer asRaw();
        void setCacheKey(const std::string& key);
        const std::string& getCacheKey() const;
        
        inline bool loaded() const {return isLoaded;};

        typedef std::vector<std::shared_ptr<Asset>> AssetPack;
        static void LoadAssets(AssetPack &pack);
        static Asset CopyAsset(const Asset &asset);
};

class AssetManager{
    public:
        struct ResolvedRequest {
            std::string cacheKey;
            std::function<std::shared_ptr<Asset>()> loader;
        };

        using AliasResolver = std::function<bool(const std::string& request, ResolvedRequest& outResolved, std::string* outError)>;

    private:
        std::map<std::string, std::shared_ptr<Asset>> assetMap;
        std::map<std::string, AliasResolver> aliasResolvers;

        static std::string NormalizeAliasToken(const std::string& alias);
        static bool ExtractAliasToken(const std::string& request, std::string& outAlias, std::string& outRemainder);
        static std::string NormalizeFileSystemKey(const std::string& name);
        bool resolveRequest(const std::string& name, ResolvedRequest& outResolved, std::string* outError = nullptr) const;
        void registerBuiltInProviders();

    public:
        AssetManager();
        void manageAsset(std::shared_ptr<Asset> assetPtr);
        void unmanageAsset(const std::string& name);
        void unmanageAliasAssets(const std::string& alias);
        bool hasAsset(const std::string& name);
        std::shared_ptr<Asset> getOrLoad(const std::string& name);
        bool registerAliasProvider(const std::string& alias, const AliasResolver& resolver);
        void unregisterAliasProvider(const std::string& alias);
        bool hasAliasProvider(const std::string& alias) const;

        static AssetManager Instance;
        
};

typedef std::shared_ptr<Asset> PAsset;


#endif // ASSET_H