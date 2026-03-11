/**
 * @file src/Assets/Descriptors/SkyboxAsset.cpp
 * @brief Implementation for SkyboxAsset.
 */

#include "Assets/Descriptors/SkyboxAsset.h"

#include "Assets/Core/Asset.h"
#include "Assets/Core/AssetDescriptorUtils.h"
#include "Foundation/IO/File.h"
#include "Foundation/Util/StringUtils.h"
#include "Rendering/Textures/SkyBox.h"

#include <sstream>

namespace {
    std::string trimCopy(const std::string& value){
        return StringUtils::Trim(value);
    }

    std::string toLowerCopy(const std::string& value){
        return StringUtils::ToLowerCase(value);
    }

    bool readTextRefOrPath(const std::string& refOrPath, std::string& outText, std::string* outError){
        return AssetDescriptorUtils::ReadTextRefOrPath(refOrPath, outText, outError);
    }

    bool readTextPath(const std::filesystem::path& path, std::string& outText, std::string* outError){
        return AssetDescriptorUtils::ReadTextPath(path, outText, outError);
    }

    bool writeTextPath(const std::filesystem::path& path, const std::string& text, std::string* outError){
        return AssetDescriptorUtils::WriteTextPath(path, text, outError);
    }

    bool writeTextAsset(const std::string& assetRef, const std::string& text, std::string* outError){
        return AssetDescriptorUtils::WriteTextAsset(assetRef, text, outError);
    }

    bool toAbsolutePathFromAssetRef(const std::string& assetRef, std::filesystem::path& outPath){
        return AssetDescriptorUtils::AssetRefToAbsolutePath(assetRef, outPath);
    }

    bool pathExists(const std::filesystem::path& path, bool* outIsDirectory = nullptr){
        return AssetDescriptorUtils::PathExists(path, outIsDirectory);
    }

    bool isSkyboxAssetPathInternal(const std::filesystem::path& path){
        const std::string lower = toLowerCopy(path.generic_string());
        return StringUtils::EndsWith(lower, ".skybox.asset");
    }

    bool parseSkyboxAssetText(const std::string& text, SkyboxAssetData& outData){
        std::istringstream stream(text);
        std::string line;
        while(std::getline(stream, line)){
            const std::string trimmed = trimCopy(line);
            if(trimmed.empty()){
                continue;
            }
            if(StringUtils::BeginsWith(trimmed, "#") ||
               StringUtils::BeginsWith(trimmed, "//") ||
               StringUtils::BeginsWith(trimmed, ";")){
                continue;
            }

            const size_t eq = trimmed.find('=');
            if(eq == std::string::npos){
                continue;
            }

            const std::string key = toLowerCopy(trimCopy(trimmed.substr(0, eq)));
            const std::string value = trimCopy(trimmed.substr(eq + 1));

            if(key == "name"){
                outData.name = value;
            }else if(key == "right" || key == "posx" || key == "positive_x"){
                outData.rightFaceRef = value;
            }else if(key == "left" || key == "negx" || key == "negative_x"){
                outData.leftFaceRef = value;
            }else if(key == "top" || key == "posy" || key == "positive_y"){
                outData.topFaceRef = value;
            }else if(key == "bottom" || key == "negy" || key == "negative_y"){
                outData.bottomFaceRef = value;
            }else if(key == "front" || key == "posz" || key == "positive_z"){
                outData.frontFaceRef = value;
            }else if(key == "back" || key == "negz" || key == "negative_z"){
                outData.backFaceRef = value;
            }
        }

        return true;
    }

    bool loadFaceAsset(const std::string& assetRef, PAsset& outAsset, std::string* outError){
        if(assetRef.empty()){
            if(outError){
                *outError = "Skybox asset is missing one or more cubemap face references.";
            }
            return false;
        }

        outAsset = AssetManager::Instance.getOrLoad(assetRef);
        if(!outAsset){
            if(outError){
                *outError = "Failed to load skybox face asset: " + assetRef;
            }
            return false;
        }
        return true;
    }
}

namespace SkyboxAssetIO {

bool IsSkyboxAssetPath(const std::filesystem::path& path){
    return isSkyboxAssetPathInternal(path);
}

bool LoadFromAbsolutePath(const std::filesystem::path& path, SkyboxAssetData& outData, std::string* outError){
    if(!IsSkyboxAssetPath(path)){
        if(outError){
            *outError = "Skybox asset files must use .skybox.asset extension.";
        }
        return false;
    }

    bool isDirectory = false;
    if(!pathExists(path, &isDirectory) || isDirectory){
        if(outError){
            *outError = "Skybox asset does not exist: " + path.generic_string();
        }
        return false;
    }

    outData = SkyboxAssetData{};
    std::string text;
    if(!readTextPath(path, text, outError)){
        return false;
    }

    parseSkyboxAssetText(text, outData);
    if(outData.name.empty()){
        outData.name = path.filename().string();
    }
    return true;
}

bool LoadFromAssetRef(const std::string& assetRef, SkyboxAssetData& outData, std::string* outError){
    outData = SkyboxAssetData{};

    std::string text;
    if(!readTextRefOrPath(assetRef, text, outError)){
        return false;
    }

    parseSkyboxAssetText(text, outData);
    if(outData.name.empty()){
        std::filesystem::path resolvedPath;
        if(toAbsolutePathFromAssetRef(assetRef, resolvedPath)){
            outData.name = resolvedPath.filename().string();
        }else{
            outData.name = std::filesystem::path(assetRef).filename().string();
        }
    }
    return true;
}

bool SaveToAbsolutePath(const std::filesystem::path& path, const SkyboxAssetData& data, std::string* outError){
    if(!IsSkyboxAssetPath(path)){
        if(outError){
            *outError = "Skybox asset files must use .skybox.asset extension.";
        }
        return false;
    }

    const std::string name = data.name.empty() ? path.filename().string() : data.name;
    std::string text;
    text += StringUtils::Format("name=%s\n", name.c_str());
    text += StringUtils::Format("right=%s\n", data.rightFaceRef.c_str());
    text += StringUtils::Format("left=%s\n", data.leftFaceRef.c_str());
    text += StringUtils::Format("top=%s\n", data.topFaceRef.c_str());
    text += StringUtils::Format("bottom=%s\n", data.bottomFaceRef.c_str());
    text += StringUtils::Format("front=%s\n", data.frontFaceRef.c_str());
    text += StringUtils::Format("back=%s\n", data.backFaceRef.c_str());
    return writeTextPath(path, text, outError);
}

bool SaveToAssetRef(const std::string& assetRef, const SkyboxAssetData& data, std::string* outError){
    const std::string name = data.name.empty() ? std::filesystem::path(assetRef).filename().string() : data.name;
    std::string text;
    text += StringUtils::Format("name=%s\n", name.c_str());
    text += StringUtils::Format("right=%s\n", data.rightFaceRef.c_str());
    text += StringUtils::Format("left=%s\n", data.leftFaceRef.c_str());
    text += StringUtils::Format("top=%s\n", data.topFaceRef.c_str());
    text += StringUtils::Format("bottom=%s\n", data.bottomFaceRef.c_str());
    text += StringUtils::Format("front=%s\n", data.frontFaceRef.c_str());
    text += StringUtils::Format("back=%s\n", data.backFaceRef.c_str());
    return writeTextAsset(assetRef, text, outError);
}

bool HasRequiredFaces(const SkyboxAssetData& data){
    return !trimCopy(data.rightFaceRef).empty() &&
           !trimCopy(data.leftFaceRef).empty() &&
           !trimCopy(data.topFaceRef).empty() &&
           !trimCopy(data.bottomFaceRef).empty() &&
           !trimCopy(data.frontFaceRef).empty() &&
           !trimCopy(data.backFaceRef).empty();
}

std::shared_ptr<SkyBox> InstantiateSkyBox(const SkyboxAssetData& data, std::string* outError){
    if(!HasRequiredFaces(data)){
        if(outError){
            *outError = "Skybox asset is missing one or more cubemap face references.";
        }
        return nullptr;
    }

    SkyBox6Face faces;
    if(!loadFaceAsset(trimCopy(data.rightFaceRef), faces.rightFaceAsset, outError) ||
       !loadFaceAsset(trimCopy(data.leftFaceRef), faces.leftFaceAsset, outError) ||
       !loadFaceAsset(trimCopy(data.topFaceRef), faces.topFaceAsset, outError) ||
       !loadFaceAsset(trimCopy(data.bottomFaceRef), faces.bottomFaceAsset, outError) ||
       !loadFaceAsset(trimCopy(data.frontFaceRef), faces.frontFaceAsset, outError) ||
       !loadFaceAsset(trimCopy(data.backFaceRef), faces.backFaceAsset, outError)){
        return nullptr;
    }

    auto skybox = std::make_shared<SkyBox>(faces);
    if(!skybox || !skybox->getCubeMap()){
        if(outError){
            *outError = "Failed to create runtime skybox cubemap.";
        }
        return nullptr;
    }

    return skybox;
}

std::shared_ptr<SkyBox> InstantiateSkyBoxFromRef(const std::string& assetRef, std::string* outError){
    SkyboxAssetData data;
    if(!LoadFromAssetRef(assetRef, data, outError)){
        return nullptr;
    }
    return InstantiateSkyBox(data, outError);
}

} // namespace SkyboxAssetIO
