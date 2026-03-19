/**
 * @file src/Assets/Descriptors/ImageAsset.cpp
 * @brief Implementation for ImageAsset.
 */

#include "Assets/Descriptors/ImageAsset.h"

#include "Assets/Core/Asset.h"
#include "Assets/Core/AssetDescriptorUtils.h"
#include "Foundation/Util/StringUtils.h"
#include "Rendering/Core/Graphics.h"

#include <sstream>

namespace {
    std::string trimCopy(const std::string& value){
        return StringUtils::Trim(value);
    }

    std::string toLowerCopy(const std::string& value){
        return StringUtils::ToLowerCase(value);
    }

    bool readTextPath(const std::filesystem::path& path, std::string& outText, std::string* outError){
        return AssetDescriptorUtils::ReadTextPath(path, outText, outError);
    }

    bool readTextRefOrPath(const std::string& refOrPath, std::string& outText, std::string* outError){
        return AssetDescriptorUtils::ReadTextRefOrPath(refOrPath, outText, outError);
    }

    bool writeTextPath(const std::filesystem::path& path, const std::string& text, std::string* outError){
        return AssetDescriptorUtils::WriteTextPath(path, text, outError);
    }

    bool writeTextAsset(const std::string& assetRef, const std::string& text, std::string* outError){
        return AssetDescriptorUtils::WriteTextAsset(assetRef, text, outError);
    }

    bool toAbsolutePathFromAssetRef(const std::string& assetRef, std::filesystem::path& outPath){
        if(AssetDescriptorUtils::AssetRefToAbsolutePath(assetRef, outPath)){
            return true;
        }

        std::filesystem::path fallbackPath = std::filesystem::path(assetRef);
        if(fallbackPath.empty()){
            return false;
        }

        if(fallbackPath.is_absolute()){
            outPath = fallbackPath.lexically_normal();
            return true;
        }

        outPath = (AssetDescriptorUtils::GetAssetRootPath() / fallbackPath).lexically_normal();
        return true;
    }

    std::string toAssetRefFromAbsolutePath(const std::filesystem::path& absolutePath){
        return AssetDescriptorUtils::AbsolutePathToAssetRef(absolutePath);
    }

    bool pathExists(const std::filesystem::path& path, bool* outIsDirectory = nullptr){
        return AssetDescriptorUtils::PathExists(path, outIsDirectory);
    }

    bool isImageAssetPathInternal(const std::filesystem::path& path){
        const std::string lower = toLowerCopy(path.generic_string());
        return StringUtils::EndsWith(lower, ".image.asset");
    }

    bool isRawImagePathInternal(const std::filesystem::path& path){
        const std::string ext = toLowerCopy(path.extension().string());
        return ext == ".png" ||
               ext == ".jpg" ||
               ext == ".jpeg" ||
               ext == ".bmp" ||
               ext == ".tga" ||
               ext == ".dds" ||
               ext == ".hdr";
    }

    int parseInt(const std::string& value, int fallback){
        try{
            return std::stoi(trimCopy(value));
        }catch(...){
            return fallback;
        }
    }

    bool parseImageAssetText(const std::string& text, ImageAssetData& outData){
        std::istringstream stream(text);
        std::string line;
        while(std::getline(stream, line)){
            std::string trimmed = trimCopy(line);
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
            }else if(key == "@link-parent" || key == "link-parent" || key == "link_parent" || key == "linkparent"){
                outData.linkParentRef = value;
            }else if(key == "source_image" || key == "source" || key == "source_asset" || key == "image"){
                outData.sourceImageRef = value;
            }else if(key == "filter" || key == "filter_mode"){
                outData.filterMode = ImageAssetIO::FilterModeFromString(value);
            }else if(key == "wrap" || key == "wrap_mode"){
                outData.wrapMode = ImageAssetIO::WrapModeFromString(value);
            }else if(key == "map_type" || key == "usage" || key == "image_type"){
                outData.mapType = ImageAssetIO::MapTypeFromString(value);
            }else if(key == "supports_alpha" || key == "alpha"){
                outData.supportsAlpha = parseInt(value, outData.supportsAlpha) != 0 ? 1 : 0;
            }else if(key == "flip_vertical" || key == "flip_vertically" || key == "flip"){
                outData.flipVertical = parseInt(value, outData.flipVertical) != 0 ? 1 : 0;
            }
        }
        return true;
    }

    TextureFilterMode toTextureFilterMode(ImageAssetFilterMode mode){
        switch(mode){
            case ImageAssetFilterMode::Nearest: return TextureFilterMode::NEAREST;
            case ImageAssetFilterMode::Linear: return TextureFilterMode::LINEAR;
            case ImageAssetFilterMode::Trilinear:
            default: return TextureFilterMode::TRILINEAR;
        }
    }

    TextureWrapMode toTextureWrapMode(ImageAssetWrapMode mode){
        switch(mode){
            case ImageAssetWrapMode::ClampEdge: return TextureWrapMode::CLAMP_EDGE;
            case ImageAssetWrapMode::ClampBorder: return TextureWrapMode::CLAMP_BORDER;
            case ImageAssetWrapMode::Repeat:
            default: return TextureWrapMode::REPEAT;
        }
    }

    std::uint64_t mixRevision(std::uint64_t seed, std::uint64_t value){
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        return seed;
    }
}

namespace ImageAssetIO {

bool IsImageAssetPath(const std::filesystem::path& path){
    return isImageAssetPathInternal(path);
}

bool IsRawImagePath(const std::filesystem::path& path){
    return isRawImagePathInternal(path);
}

const char* FilterModeToString(ImageAssetFilterMode mode){
    switch(mode){
        case ImageAssetFilterMode::Nearest: return "Nearest";
        case ImageAssetFilterMode::Linear: return "Linear";
        case ImageAssetFilterMode::Trilinear:
        default: return "Trilinear";
    }
}

ImageAssetFilterMode FilterModeFromString(const std::string& value){
    const std::string lower = toLowerCopy(trimCopy(value));
    if(lower == "nearest"){
        return ImageAssetFilterMode::Nearest;
    }
    if(lower == "linear"){
        return ImageAssetFilterMode::Linear;
    }
    if(lower == "trilinear" || lower == "mipmap" || lower == "mipmapped"){
        return ImageAssetFilterMode::Trilinear;
    }
    return ImageAssetFilterMode::Nearest;
}

const char* WrapModeToString(ImageAssetWrapMode mode){
    switch(mode){
        case ImageAssetWrapMode::ClampEdge: return "ClampEdge";
        case ImageAssetWrapMode::ClampBorder: return "ClampBorder";
        case ImageAssetWrapMode::Repeat:
        default: return "Repeat";
    }
}

ImageAssetWrapMode WrapModeFromString(const std::string& value){
    const std::string lower = toLowerCopy(trimCopy(value));
    if(lower == "clampedge" || lower == "clamp_edge" || lower == "clamp-to-edge"){
        return ImageAssetWrapMode::ClampEdge;
    }
    if(lower == "clampborder" || lower == "clamp_border" || lower == "clamp-to-border"){
        return ImageAssetWrapMode::ClampBorder;
    }
    if(lower == "repeat"){
        return ImageAssetWrapMode::Repeat;
    }
    return ImageAssetWrapMode::Repeat;
}

const char* MapTypeToString(ImageAssetMapType type){
    switch(type){
        case ImageAssetMapType::Normal: return "Normal";
        case ImageAssetMapType::Height: return "Height";
        case ImageAssetMapType::Roughness: return "Roughness";
        case ImageAssetMapType::Metallic: return "Metallic";
        case ImageAssetMapType::Occlusion: return "Occlusion";
        case ImageAssetMapType::Emissive: return "Emissive";
        case ImageAssetMapType::Opacity: return "Opacity";
        case ImageAssetMapType::Data: return "Data";
        case ImageAssetMapType::Color:
        default: return "Color";
    }
}

ImageAssetMapType MapTypeFromString(const std::string& value){
    const std::string lower = toLowerCopy(trimCopy(value));
    if(lower == "normal" || lower == "normalmap" || lower == "normal_map"){
        return ImageAssetMapType::Normal;
    }
    if(lower == "height" || lower == "heightmap" || lower == "height_map"){
        return ImageAssetMapType::Height;
    }
    if(lower == "roughness" || lower == "roughnessmap" || lower == "roughness_map"){
        return ImageAssetMapType::Roughness;
    }
    if(lower == "metallic" || lower == "metalness"){
        return ImageAssetMapType::Metallic;
    }
    if(lower == "occlusion" || lower == "ao" || lower == "ambientocclusion"){
        return ImageAssetMapType::Occlusion;
    }
    if(lower == "emissive" || lower == "emission"){
        return ImageAssetMapType::Emissive;
    }
    if(lower == "opacity" || lower == "alpha"){
        return ImageAssetMapType::Opacity;
    }
    if(lower == "data" || lower == "mask"){
        return ImageAssetMapType::Data;
    }
    return ImageAssetMapType::Color;
}

bool ResolveImageAssetRef(const std::string& imageAssetRefOrPath, std::string& outAssetRef, std::string* outError){
    const std::string trimmed = trimCopy(imageAssetRefOrPath);
    if(trimmed.empty()){
        if(outError){
            *outError = "Image asset reference is empty.";
        }
        return false;
    }

    std::filesystem::path absolutePath;
    if(!toAbsolutePathFromAssetRef(trimmed, absolutePath)){
        if(outError){
            *outError = "Invalid image asset reference: " + trimmed;
        }
        return false;
    }

    if(!IsImageAssetPath(absolutePath)){
        if(outError){
            *outError = "Unsupported image asset path (expected .image.asset): " + absolutePath.generic_string();
        }
        return false;
    }

    bool isDirectory = false;
    if(!pathExists(absolutePath, &isDirectory) || isDirectory){
        if(outError){
            *outError = "Image asset does not exist: " + absolutePath.generic_string();
        }
        return false;
    }

    if(AssetDescriptorUtils::IsAssetRef(trimmed)){
        outAssetRef = trimmed;
    }else{
        const std::string convertedRef = toAssetRefFromAbsolutePath(absolutePath);
        outAssetRef = convertedRef.empty() ? trimmed : convertedRef;
    }
    return true;
}

bool ResolveTextureSourceAssetRef(const std::string& imageOrAssetRef,
                                  std::string& outSourceAssetRef,
                                  std::string* outResolvedImageAssetRef,
                                  std::string* outError){
    outSourceAssetRef.clear();
    if(outResolvedImageAssetRef){
        outResolvedImageAssetRef->clear();
    }

    const std::string trimmed = trimCopy(imageOrAssetRef);
    if(trimmed.empty()){
        if(outError){
            *outError = "Texture reference is empty.";
        }
        return false;
    }

    std::filesystem::path absolutePath;
    if(!toAbsolutePathFromAssetRef(trimmed, absolutePath)){
        if(outError){
            *outError = "Invalid texture reference: " + trimmed;
        }
        return false;
    }

    if(!IsImageAssetPath(absolutePath)){
        outSourceAssetRef = trimmed;
        return true;
    }

    std::string resolvedImageAssetRef;
    if(!ResolveImageAssetRef(trimmed, resolvedImageAssetRef, outError)){
        return false;
    }

    ImageAssetData imageData;
    if(!LoadFromAssetRef(resolvedImageAssetRef, imageData, outError)){
        return false;
    }

    const std::string sourceRef = trimCopy(imageData.sourceImageRef);
    if(sourceRef.empty()){
        if(outError){
            *outError = "Image asset has no source_image: " + resolvedImageAssetRef;
        }
        return false;
    }

    outSourceAssetRef = sourceRef;
    if(outResolvedImageAssetRef){
        *outResolvedImageAssetRef = resolvedImageAssetRef;
    }
    return true;
}

bool RefDependsOnAsset(const std::string& imageOrAssetRef, const std::string& changedAsset){
    if(imageOrAssetRef.empty() || changedAsset.empty()){
        return false;
    }

    if(AssetManager::Instance.isSameAsset(imageOrAssetRef, changedAsset)){
        return true;
    }

    std::string sourceRef;
    std::string imageAssetRef;
    if(!ResolveTextureSourceAssetRef(imageOrAssetRef, sourceRef, &imageAssetRef, nullptr)){
        return false;
    }

    if(!imageAssetRef.empty() && AssetManager::Instance.isSameAsset(imageAssetRef, changedAsset)){
        return true;
    }
    if(!sourceRef.empty() && AssetManager::Instance.isSameAsset(sourceRef, changedAsset)){
        return true;
    }
    return false;
}

std::uint64_t GetTextureRefRevision(const std::string& imageOrAssetRef){
    if(imageOrAssetRef.empty()){
        return 0;
    }

    std::uint64_t revision = 0;
    std::string sourceRef;
    std::string imageAssetRef;
    if(ResolveTextureSourceAssetRef(imageOrAssetRef, sourceRef, &imageAssetRef, nullptr)){
        if(!imageAssetRef.empty()){
            revision = mixRevision(revision, AssetManager::Instance.getRevision(imageAssetRef));
        }
        if(!sourceRef.empty()){
            revision = mixRevision(revision, AssetManager::Instance.getRevision(sourceRef));
        }
    }

    if(revision == 0){
        revision = AssetManager::Instance.getRevision(imageOrAssetRef);
    }
    return revision;
}

bool LoadFromAbsolutePath(const std::filesystem::path& path, ImageAssetData& outData, std::string* outError){
    if(!IsImageAssetPath(path)){
        if(outError){
            *outError = "Image asset files must use .image.asset extension.";
        }
        return false;
    }

    bool isDirectory = false;
    if(!pathExists(path, &isDirectory) || isDirectory){
        if(outError){
            *outError = "Image asset does not exist: " + path.generic_string();
        }
        return false;
    }

    outData = ImageAssetData{};
    std::string text;
    if(!readTextPath(path, text, outError)){
        return false;
    }
    parseImageAssetText(text, outData);
    if(outData.name.empty()){
        outData.name = path.filename().string();
    }
    return true;
}

bool LoadFromAssetRef(const std::string& assetRef, ImageAssetData& outData, std::string* outError){
    std::string resolvedAssetRef;
    if(!ResolveImageAssetRef(assetRef, resolvedAssetRef, outError)){
        return false;
    }

    std::filesystem::path resolvedPath;
    if(!toAbsolutePathFromAssetRef(resolvedAssetRef, resolvedPath)){
        if(outError){
            *outError = "Invalid resolved image asset path: " + resolvedAssetRef;
        }
        return false;
    }

    outData = ImageAssetData{};
    std::string text;
    if(!readTextRefOrPath(resolvedAssetRef, text, outError)){
        return false;
    }
    parseImageAssetText(text, outData);
    if(outData.name.empty()){
        outData.name = resolvedPath.filename().string();
    }
    return true;
}

bool SaveToAbsolutePath(const std::filesystem::path& path, const ImageAssetData& data, std::string* outError){
    if(!IsImageAssetPath(path)){
        if(outError){
            *outError = "Image asset files must use .image.asset extension.";
        }
        return false;
    }

    const std::string imageName = data.name.empty() ? path.filename().string() : data.name;
    std::string text;
    text += StringUtils::Format("name=%s\n", imageName.c_str());
    text += StringUtils::Format("@link-parent=%s\n", data.linkParentRef.c_str());
    text += StringUtils::Format("source_image=%s\n", data.sourceImageRef.c_str());
    text += StringUtils::Format("filter=%s\n", FilterModeToString(data.filterMode));
    text += StringUtils::Format("wrap=%s\n", WrapModeToString(data.wrapMode));
    text += StringUtils::Format("map_type=%s\n", MapTypeToString(data.mapType));
    text += StringUtils::Format("supports_alpha=%d\n", data.supportsAlpha != 0 ? 1 : 0);
    text += StringUtils::Format("flip_vertical=%d\n", data.flipVertical != 0 ? 1 : 0);
    return writeTextPath(path, text, outError);
}

bool SaveToAssetRef(const std::string& assetRef, const ImageAssetData& data, std::string* outError){
    const std::string imageName = data.name.empty() ? std::filesystem::path(assetRef).filename().string() : data.name;
    std::string text;
    text += StringUtils::Format("name=%s\n", imageName.c_str());
    text += StringUtils::Format("@link-parent=%s\n", data.linkParentRef.c_str());
    text += StringUtils::Format("source_image=%s\n", data.sourceImageRef.c_str());
    text += StringUtils::Format("filter=%s\n", FilterModeToString(data.filterMode));
    text += StringUtils::Format("wrap=%s\n", WrapModeToString(data.wrapMode));
    text += StringUtils::Format("map_type=%s\n", MapTypeToString(data.mapType));
    text += StringUtils::Format("supports_alpha=%d\n", data.supportsAlpha != 0 ? 1 : 0);
    text += StringUtils::Format("flip_vertical=%d\n", data.flipVertical != 0 ? 1 : 0);
    return writeTextAsset(assetRef, text, outError);
}

std::shared_ptr<Texture> InstantiateTexture(const ImageAssetData& data, std::string* outError){
    const std::string sourceRef = trimCopy(data.sourceImageRef);
    if(sourceRef.empty()){
        if(outError){
            *outError = "Image asset has no source_image.";
        }
        return nullptr;
    }

    auto sourceAsset = AssetManager::Instance.getOrLoad(sourceRef);
    if(!sourceAsset){
        if(outError){
            *outError = "Failed to load image source asset: " + sourceRef;
        }
        return nullptr;
    }

    const bool flipVertical = (data.flipVertical != 0);
    std::shared_ptr<Texture> texture;
    if(data.supportsAlpha == 0){
        auto sourceImage = Texture::LoadImage(sourceAsset, flipVertical);
        if(sourceImage){
            for(size_t i = 3; i < sourceImage->pixelData.size(); i += 4){
                sourceImage->pixelData[i] = 255;
            }
            texture = std::make_shared<Texture>(sourceImage, GL_TEXTURE_2D);
        }
    }else{
        texture = Texture::Load(sourceAsset, GL_TEXTURE_2D, flipVertical);
    }

    if(!texture || texture->getID() == 0){
        if(outError){
            *outError = "Failed to create texture from source image: " + sourceRef;
        }
        return nullptr;
    }

    texture->setFilterMode(
        toTextureFilterMode(data.filterMode),
        data.filterMode == ImageAssetFilterMode::Trilinear
    );
    texture->setWrapMode(toTextureWrapMode(data.wrapMode));
    return texture;
}

std::shared_ptr<Texture> InstantiateTextureFromRef(const std::string& imageOrAssetRef,
                                                   std::string* outResolvedImageAssetRef,
                                                   std::string* outError){
    if(outResolvedImageAssetRef){
        outResolvedImageAssetRef->clear();
    }

    const std::string trimmed = trimCopy(imageOrAssetRef);
    if(trimmed.empty()){
        if(outError){
            *outError = "Texture reference is empty.";
        }
        return nullptr;
    }

    std::filesystem::path absolutePath;
    if(!toAbsolutePathFromAssetRef(trimmed, absolutePath)){
        if(outError){
            *outError = "Invalid texture reference: " + trimmed;
        }
        return nullptr;
    }

    if(IsImageAssetPath(absolutePath)){
        std::string resolvedImageAssetRef;
        if(!ResolveImageAssetRef(trimmed, resolvedImageAssetRef, outError)){
            return nullptr;
        }

        ImageAssetData data;
        if(!LoadFromAssetRef(resolvedImageAssetRef, data, outError)){
            return nullptr;
        }

        auto texture = InstantiateTexture(data, outError);
        if(texture){
            texture->setSourceAssetRef(resolvedImageAssetRef);
        }
        if(outResolvedImageAssetRef){
            *outResolvedImageAssetRef = resolvedImageAssetRef;
        }
        return texture;
    }

    auto sourceAsset = AssetManager::Instance.getOrLoad(trimmed);
    if(!sourceAsset){
        if(outError){
            *outError = "Failed to load texture asset: " + trimmed;
        }
        return nullptr;
    }

    auto texture = Texture::Load(sourceAsset);
    if(!texture && outError){
        *outError = "Failed to decode texture asset: " + trimmed;
    }
    if(texture){
        texture->setSourceAssetRef(trimmed);
    }
    return texture;
}

} // namespace ImageAssetIO
