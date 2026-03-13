/**
 * @file src/Assets/Core/Asset.cpp
 * @brief Implementation for Asset.
 */


#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "Assets/Core/Asset.h"
#include "Foundation/Logging/Logbot.h"
#include "Foundation/Util/StringUtils.h"

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

    // Avoid noisy logging in this hot path; editor panels may request assets frequently.
    std::unique_ptr<File> fileHandle = std::unique_ptr<File>(new File(interpretedString));
    this->fileHandle = std::move(fileHandle); // take ownership of the pointer.
}

std::unique_ptr<File>& Asset::getFileHandle(){
    return this->fileHandle;
}

bool Asset::load(){
    if(this->isLoaded) return true;
    try{
        this->fblob = FileReader::Read(this->fileHandle.get());
        this->isLoaded = true;
    }catch(const std::exception& e){
        assetLogger.LogVerbose(LOG_ERRO, "Failed to load asset '%s': %s",
                               this->fileHandle ? this->fileHandle->getPath().c_str() : "<null>",
                               e.what());
        this->isLoaded = false;
        this->fblob = FileBlob{};
    }catch(...){
        assetLogger.LogVerbose(LOG_ERRO, "Failed to load asset '%s': unknown exception",
                               this->fileHandle ? this->fileHandle->getPath().c_str() : "<null>");
        this->isLoaded = false;
        this->fblob = FileBlob{};
    }
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

void Asset::setCacheKey(const std::string& key){
    cacheKey = key;
}

const std::string& Asset::getCacheKey() const{
    return cacheKey;
}

AssetManager::AssetManager(){
    registerBuiltInProviders();
}

std::string AssetManager::NormalizeAliasToken(const std::string& alias){
    if(alias.empty()){
        return std::string();
    }

    std::string normalized = alias;
    normalized = StringUtils::ReplaceAll(normalized.c_str(), "\\", "/");
    const size_t slashPos = normalized.find('/');
    if(slashPos != std::string::npos){
        normalized = normalized.substr(0, slashPos);
    }
    if(normalized.empty()){
        return std::string();
    }
    if(normalized[0] != '@'){
        normalized = "@" + normalized;
    }
    return StringUtils::ToLowerCase(normalized);
}

bool AssetManager::ExtractAliasToken(const std::string& request, std::string& outAlias, std::string& outRemainder){
    outAlias.clear();
    outRemainder.clear();

    if(request.empty() || request[0] != '@'){
        return false;
    }

    std::string normalized = StringUtils::ReplaceAll(request.c_str(), "\\", "/");
    size_t slashPos = normalized.find('/');
    if(slashPos == std::string::npos){
        outAlias = NormalizeAliasToken(normalized);
        return !outAlias.empty();
    }

    outAlias = NormalizeAliasToken(normalized.substr(0, slashPos));
    outRemainder = normalized.substr(slashPos + 1);
    return !outAlias.empty();
}

std::string AssetManager::NormalizeFileSystemKey(const std::string& name){
    if(name.empty()){
        return std::string();
    }

    std::string interpreted = StringUtils::ReplaceAll(name.c_str(), "/", "\\");
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

bool AssetManager::resolveRequest(const std::string& name, ResolvedRequest& outResolved, std::string* outError) const{
    outResolved = ResolvedRequest{};
    if(name.empty()){
        if(outError){
            *outError = "Asset request was empty.";
        }
        return false;
    }

    std::string alias;
    std::string remainder;
    if(ExtractAliasToken(name, alias, remainder)){
        auto it = aliasResolvers.find(alias);
        if(it == aliasResolvers.end()){
            if(outError){
                *outError = "No asset alias provider is registered for '" + alias + "'.";
            }
            return false;
        }
        return it->second(name, outResolved, outError);
    }

    outResolved.cacheKey = NormalizeFileSystemKey(name);
    if(outResolved.cacheKey.empty()){
        if(outError){
            *outError = "Failed to normalize asset path '" + name + "'.";
        }
        return false;
    }

    outResolved.loader = [name]() -> std::shared_ptr<Asset> {
        return std::make_shared<Asset>(name);
    };
    return true;
}

void AssetManager::registerBuiltInProviders(){
    registerAliasProvider(
        ASSET_DELIMITER,
        [](const std::string& request, ResolvedRequest& outResolved, std::string* outError) -> bool {
            std::string alias;
            std::string remainder;
            if(!AssetManager::ExtractAliasToken(request, alias, remainder)){
                if(outError){
                    *outError = "Invalid @assets request: " + request;
                }
                return false;
            }

            std::string cwd = File::GetCWD();
            std::filesystem::path absolutePath = std::filesystem::path(cwd) / "res" / std::filesystem::path(remainder);
            std::string resolvedPath = absolutePath.string();
            outResolved.cacheKey = AssetManager::NormalizeFileSystemKey(resolvedPath);
            if(outResolved.cacheKey.empty()){
                if(outError){
                    *outError = "Failed to normalize @assets request: " + request;
                }
                return false;
            }

            outResolved.loader = [resolvedPath]() -> std::shared_ptr<Asset> {
                return std::make_shared<Asset>(resolvedPath);
            };
            return true;
        }
    );
}

void AssetManager::manageAsset(std::shared_ptr<Asset> assetPtr){
    if(!assetPtr){
        return;
    }

    std::string key = assetPtr->getCacheKey();
    if(key.empty() && assetPtr->getFileHandle()){
        key = NormalizeFileSystemKey(assetPtr->getFileHandle()->getPath());
    }
    if(key.empty()){
        return;
    }

    assetPtr->setCacheKey(key);
    assetMap[key] = assetPtr;
}

void AssetManager::unmanageAsset(const std::string& name){
    ResolvedRequest resolved;
    if(!resolveRequest(name, resolved, nullptr)){
        resolved.cacheKey = NormalizeFileSystemKey(name);
    }
    if(resolved.cacheKey.empty()){
        return;
    }

    auto it = assetMap.find(resolved.cacheKey);
    if(it != assetMap.end()){
        assetMap.erase(it);
    }
}

void AssetManager::unmanageAliasAssets(const std::string& alias){
    const std::string normalizedAlias = NormalizeAliasToken(alias);
    if(normalizedAlias.empty()){
        return;
    }

    const std::string prefix = normalizedAlias + "/";
    for(auto it = assetMap.begin(); it != assetMap.end();){
        if(it->first == normalizedAlias || StringUtils::BeginsWith(it->first, prefix)){
            it = assetMap.erase(it);
        }else{
            ++it;
        }
    }
}

bool AssetManager::hasAsset(const std::string& name){
    ResolvedRequest resolved;
    if(!resolveRequest(name, resolved, nullptr)){
        return false;
    }

    auto it = assetMap.find(resolved.cacheKey);
    return it != assetMap.end() && static_cast<bool>(it->second);
}

std::shared_ptr<Asset> AssetManager::getOrLoad(const std::string& name){
    ResolvedRequest resolved;
    if(!resolveRequest(name, resolved, nullptr) || resolved.cacheKey.empty() || !resolved.loader){
        return nullptr;
    }

    auto cached = assetMap.find(resolved.cacheKey);
    if(cached != assetMap.end()){
        if(cached->second){
            return cached->second;
        }
        assetMap.erase(cached);
    }

    auto assetPtr = resolved.loader();
    if(!assetPtr){
        return nullptr;
    }

    assetPtr->setCacheKey(resolved.cacheKey);
    if(assetPtr->load()){
        manageAsset(assetPtr);
        return assetPtr;
    }

    return nullptr;
}

bool AssetManager::registerAliasProvider(const std::string& alias, const AliasResolver& resolver){
    const std::string normalizedAlias = NormalizeAliasToken(alias);
    if(normalizedAlias.empty() || !resolver){
        return false;
    }

    aliasResolvers[normalizedAlias] = resolver;
    return true;
}

void AssetManager::unregisterAliasProvider(const std::string& alias){
    const std::string normalizedAlias = NormalizeAliasToken(alias);
    if(normalizedAlias.empty()){
        return;
    }

    aliasResolvers.erase(normalizedAlias);
}

bool AssetManager::hasAliasProvider(const std::string& alias) const{
    const std::string normalizedAlias = NormalizeAliasToken(alias);
    if(normalizedAlias.empty()){
        return false;
    }

    return aliasResolvers.find(normalizedAlias) != aliasResolvers.end();
}

bool AssetManager::tryResolveCacheKey(const std::string& name, std::string& outCacheKey) const{
    outCacheKey.clear();

    ResolvedRequest resolved;
    if(resolveRequest(name, resolved, nullptr) && !resolved.cacheKey.empty()){
        outCacheKey = resolved.cacheKey;
        return true;
    }

    outCacheKey = NormalizeFileSystemKey(name);
    return !outCacheKey.empty();
}

bool AssetManager::isSameAsset(const std::string& a, const std::string& b) const{
    std::string aKey;
    std::string bKey;
    if(!tryResolveCacheKey(a, aKey) || !tryResolveCacheKey(b, bKey)){
        return false;
    }
    return aKey == bKey;
}

std::uint64_t AssetManager::getRevision(const std::string& name) const{
    std::string cacheKey;
    if(!tryResolveCacheKey(name, cacheKey)){
        return 0;
    }

    auto it = assetRevisions.find(cacheKey);
    if(it == assetRevisions.end()){
        return 0;
    }
    return it->second;
}

int AssetManager::addChangeListener(const AssetChangeListener& listener){
    if(!listener){
        return -1;
    }

    const int handle = nextChangeListenerHandle++;
    changeListeners[handle] = listener;
    return handle;
}

void AssetManager::removeChangeListener(int handle){
    if(handle < 0){
        return;
    }

    changeListeners.erase(handle);
}

void AssetManager::notifyAssetChanged(const std::string& name){
    std::string cacheKey;
    if(!tryResolveCacheKey(name, cacheKey)){
        return;
    }

    assetMap.erase(cacheKey);
    const std::uint64_t revision = ++assetRevisions[cacheKey];

    AssetChangeEvent event;
    event.request = name;
    event.cacheKey = cacheKey;
    event.revision = revision;

    std::vector<AssetChangeListener> listeners;
    listeners.reserve(changeListeners.size());
    for(const auto& kv : changeListeners){
        if(kv.second){
            listeners.push_back(kv.second);
        }
    }

    for(const auto& listener : listeners){
        listener(event);
    }
}

