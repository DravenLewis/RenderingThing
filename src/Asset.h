
#ifndef ASSET_H
#define ASSET_H

#include "LogBot.h"

#include "File.h"
#include "StringUtils.h"

#include <memory>
#include <map>
#include <filesystem>


#define ASSET_DELIMITER "@assets"

#define VERBOSE false


class Asset{
    private:
        std::unique_ptr<File> fileHandle; // This file owns the pointer.
        FileBlob fblob;
        bool isLoaded = false;

    public:
        Asset() = delete;
        Asset(std::unique_ptr<File> currentFileHandle);
        Asset(std::string path);
        ~Asset();
        std::unique_ptr<File>& getFileHandle();
        bool load();

        std::string asString();
        BinaryBuffer asRaw();
        
        inline bool loaded() const {return isLoaded;};

        typedef std::vector<std::shared_ptr<Asset>> AssetPack;
        static void LoadAssets(AssetPack &pack);
        static Asset CopyAsset(const Asset &asset);
};

class AssetManager{
    private:
        std::map<std::string, std::shared_ptr<Asset>> assetMap;
        std::string makeAssetKey(const std::string& name){
            if(name.empty()){
                return std::string();
            }

            std::string interpreted = name;
            size_t pos = interpreted.find(ASSET_DELIMITER);
            if(pos != std::string::npos){
                std::string cwd = File::GetCWD();
                interpreted = StringUtils::ReplaceAll(interpreted.c_str(), ASSET_DELIMITER, StringUtils::Format("%s\\res", cwd.c_str()));
            }
            interpreted = StringUtils::ReplaceAll(interpreted.c_str(), "/", "\\");

            std::error_code ec;
            std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(std::filesystem::path(interpreted), ec);
            if(ec){
                normalizedPath = std::filesystem::path(interpreted).lexically_normal();
            }

            std::string key = normalizedPath.generic_string();
            #ifdef _WIN32
                key = StringUtils::ToLowerCase(key);
            #endif
            return key;
        }
    public:
        void manageAsset(std::shared_ptr<Asset> assetPtr){
            if(!assetPtr) return;
            std::string name = assetPtr->getFileHandle()->getPath();
            std::string key = makeAssetKey(name);
            if(key.empty()){
                return;
            }
            this->assetMap[key] = assetPtr;
        }

        void unmanageAsset(const std::string& name){
            std::string key = makeAssetKey(name);
            if(key.empty()){
                return;
            }
            auto it = assetMap.find(key);
            if(it != assetMap.end()){
                assetMap.erase(it);
            }
        }

        bool hasAsset(const std::string& name){
            std::string key = makeAssetKey(name);
            if(key.empty()){
                return false;
            }
            auto it = assetMap.find(key);
            return it != assetMap.end() && static_cast<bool>(it->second);
        }

        std::shared_ptr<Asset> getOrLoad(const std::string& name){
            std::string key = makeAssetKey(name);
            if(key.empty()){
                return nullptr;
            }

            auto cached = assetMap.find(key);
            if(cached != assetMap.end()){
                if(cached->second){
                    return cached->second;
                }
                assetMap.erase(cached);
            }

            auto assetPtr = std::make_shared<Asset>(name);
            if(assetPtr->load()){
                manageAsset(assetPtr);
                return assetPtr;
            }

            return nullptr;
        }

        static AssetManager Instance;
        
};

typedef std::shared_ptr<Asset> PAsset;


#endif // ASSET_H
