
#include <cstdlib>
#include <cstdint>

#include "Asset.h"
#include "Logbot.h"
#include "StringUtils.h"

inline static Logbot assetLogger = Logbot::CreateInstance("Asset Loader");

AssetManager AssetManager::Instance;

Asset::Asset(std::unique_ptr<File> currentFileHandle){
    this->fileHandle = std::move(currentFileHandle); // take ownership of the pointer.
}

Asset::Asset(std::string path){
    std::string interpretedString = path;
    size_t pos = interpretedString.find(ASSET_DELIMITER);
    if(pos != std::string::npos){
        std::string cwd = File::GetCWD();
        //assetLogger.LogBasic("CWD resolved to: %s", cwd.c_str());
        interpretedString = StringUtils::ReplaceAll(interpretedString.c_str(),ASSET_DELIMITER, StringUtils::Format("%s\\res", cwd.c_str()));
    }
    interpretedString = StringUtils::ReplaceAll(interpretedString.c_str(), "/", "\\");

    assetLogger.LogBasic("Attempting to Load File: %s", interpretedString.c_str());
    std::unique_ptr<File> fileHandle = std::unique_ptr<File>(new File(interpretedString));
    this->fileHandle = std::move(fileHandle); // take ownership of the pointer.
}

std::unique_ptr<File>& Asset::getFileHandle(){
    return this->fileHandle;
}

bool Asset::load(){
    if(this->isLoaded) return true;
    this->fblob = FileReader::Read(this->fileHandle.get());
    this->isLoaded = this->fblob.size() > 0;
    return this->isLoaded;
}

void Asset::LoadAssets(AssetPack &pack){
    for(auto i = 0; i < pack.size(); i++){
        pack[i]->load();
    }
}

Asset::~Asset(){
    this->fileHandle->close();
}

std::string Asset::asString(){
    if(!this->isLoaded) return std::string("");
    return this->fblob.asString();
}

BinaryBuffer Asset::asRaw(){
    if(!this->isLoaded) return BinaryBuffer();
    return this->fblob.data;
}
