#include "AssetDescriptorUtils.h"

#include "Asset.h"
#include "File.h"
#include "StringUtils.h"

#include <cstring>

namespace AssetDescriptorUtils {

std::filesystem::path GetAssetRootPath(){
    return std::filesystem::path(File::GetCWD()) / "res";
}

bool IsAssetRef(const std::string& value){
    return StringUtils::BeginsWith(value, ASSET_DELIMITER);
}

std::string MakeAssetRefFromRelative(const std::string& relative){
    if(relative.empty()){
        return std::string(ASSET_DELIMITER);
    }
    return std::string(ASSET_DELIMITER) + "/" + relative;
}

bool AssetRefToAbsolutePath(const std::string& assetRef, std::filesystem::path& outPath){
    if(assetRef.empty()){
        return false;
    }
    if(IsAssetRef(assetRef)){
        std::string rel = assetRef.substr(std::strlen(ASSET_DELIMITER));
        if(!rel.empty() && (rel[0] == '/' || rel[0] == '\\')){
            rel.erase(rel.begin());
        }
        outPath = GetAssetRootPath() / rel;
        return true;
    }
    outPath = std::filesystem::path(assetRef);
    return true;
}

std::string AbsolutePathToAssetRef(const std::filesystem::path& absolutePath){
    std::error_code ec;
    std::filesystem::path assetRoot = std::filesystem::weakly_canonical(GetAssetRootPath(), ec);
    if(ec){
        assetRoot = GetAssetRootPath().lexically_normal();
    }

    std::filesystem::path absolute = std::filesystem::weakly_canonical(absolutePath, ec);
    if(ec){
        absolute = absolutePath.lexically_normal();
    }

    std::filesystem::path rel = absolute.lexically_relative(assetRoot);
    if(rel.empty()){
        return absolute.generic_string();
    }
    if(StringUtils::BeginsWith(rel.generic_string(), "..")){
        return absolute.generic_string();
    }
    return MakeAssetRefFromRelative(rel.generic_string());
}

bool ReadTextAsset(const std::string& assetRef, std::string& outText, std::string* outError){
    auto asset = AssetManager::Instance.getOrLoad(assetRef);
    if(!asset){
        if(outError){
            *outError = "Failed to load asset: " + assetRef;
        }
        return false;
    }

    outText = asset->asString();
    if(outText.empty() && !asset->asRaw().empty()){
        outText.assign(asset->asRaw().begin(), asset->asRaw().end());
    }
    return true;
}

bool ReadTextPath(const std::filesystem::path& path, std::string& outText, std::string* outError){
    std::error_code ec;
    const std::filesystem::path assetRoot = std::filesystem::weakly_canonical(GetAssetRootPath(), ec);
    std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, ec);
    if(ec){
        normalizedPath = path.lexically_normal();
    }
    if(!std::filesystem::exists(normalizedPath, ec) || std::filesystem::is_directory(normalizedPath, ec)){
        if(outError){
            *outError = "Failed to load file: " + normalizedPath.generic_string();
        }
        return false;
    }

    std::filesystem::path rel = normalizedPath.lexically_relative(assetRoot);
    if(!rel.empty() && !StringUtils::BeginsWith(rel.generic_string(), "..")){
        return ReadTextAsset(MakeAssetRefFromRelative(rel.generic_string()), outText, outError);
    }

    auto asset = std::make_shared<Asset>(normalizedPath.string());
    if(!asset || !asset->load()){
        if(outError){
            *outError = "Failed to load file: " + normalizedPath.generic_string();
        }
        return false;
    }

    outText = asset->asString();
    if(outText.empty() && !asset->asRaw().empty()){
        outText.assign(asset->asRaw().begin(), asset->asRaw().end());
    }
    return true;
}

bool ReadTextRefOrPath(const std::string& refOrPath, std::string& outText, std::string* outError){
    if(refOrPath.empty()){
        outText.clear();
        return true;
    }
    if(IsAssetRef(refOrPath)){
        return ReadTextAsset(refOrPath, outText, outError);
    }
    return ReadTextPath(std::filesystem::path(refOrPath), outText, outError);
}

} // namespace AssetDescriptorUtils
