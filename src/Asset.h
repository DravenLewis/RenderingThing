
#ifndef ASSET_H
#define ASSET_H

#include "LogBot.h"

#include "File.h"
#include "StringUtils.h"

#include <memory>
#include <map>


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
    public:
        void manageAsset(std::shared_ptr<Asset> assetPtr){
            if(!assetPtr) return;
            std::string name = assetPtr->getFileHandle()->getFileName();
            this->assetMap[name] = assetPtr;
        }

        void unmanageAsset(const std::string& name){
            if(assetMap[name]){
                std::shared_ptr<Asset> asset = assetMap[name];  
                assetMap.erase(name);
            }
        }

        bool hasAsset(const std::string& name){
            return assetMap.find(name) != assetMap.end();
        }

        std::shared_ptr<Asset> getOrLoad(const std::string& name){
            std::string fileName = StringUtils::Replace(name,"\\", "/");
            auto namePath = StringUtils::Split(fileName, "/");
            fileName = namePath.back();

            if(hasAsset(fileName)){
                return assetMap[fileName];
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