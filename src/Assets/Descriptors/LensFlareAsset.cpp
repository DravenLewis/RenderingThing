/**
 * @file src/Assets/Descriptors/LensFlareAsset.cpp
 * @brief Implementation for LensFlareAsset.
 */

#include "Assets/Descriptors/LensFlareAsset.h"

#include "Assets/Core/AssetDescriptorUtils.h"
#include "Foundation/Util/StringUtils.h"

#include <algorithm>
#include <cctype>
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

    bool isLensFlareAssetPathInternal(const std::filesystem::path& path){
        const std::string lower = toLowerCopy(path.generic_string());
        return StringUtils::EndsWith(lower, ".flare.asset");
    }

    float parseFloat(const std::string& value, float fallback){
        try{
            return std::stof(trimCopy(value));
        }catch(...){
            return fallback;
        }
    }

    int parseInt(const std::string& value, int fallback){
        try{
            return std::stoi(trimCopy(value));
        }catch(...){
            return fallback;
        }
    }

    Math3D::Vec3 parseVec3(const std::string& value, const Math3D::Vec3& fallback){
        auto parts = StringUtils::Split(value, ",");
        if(parts.size() < 3){
            return fallback;
        }
        try{
            return Math3D::Vec3(
                std::stof(trimCopy(parts[0])),
                std::stof(trimCopy(parts[1])),
                std::stof(trimCopy(parts[2]))
            );
        }catch(...){
            return fallback;
        }
    }

    std::string vec3ToString(const Math3D::Vec3& value){
        return StringUtils::Format("%.6f,%.6f,%.6f", value.x, value.y, value.z);
    }

    LensFlareElementType parseElementType(const std::string& value){
        const std::string lower = toLowerCopy(trimCopy(value));
        if(lower == "polygon"){
            return LensFlareElementType::Polygon;
        }
        if(lower == "circle"){
            return LensFlareElementType::Circle;
        }
        return LensFlareElementType::Image;
    }

    std::string elementTypeToString(LensFlareElementType type){
        switch(type){
            case LensFlareElementType::Polygon: return "polygon";
            case LensFlareElementType::Circle: return "circle";
            case LensFlareElementType::Image:
            default:
                return "image";
        }
    }

    bool tryParseElementKey(const std::string& key, size_t& outIndex, std::string& outField){
        if(!StringUtils::BeginsWith(key, "element")){
            return false;
        }

        size_t cursor = 7;
        size_t digitsStart = cursor;
        while(cursor < key.size() && std::isdigit(static_cast<unsigned char>(key[cursor]))){
            cursor++;
        }
        if(cursor == digitsStart || cursor >= key.size() || key[cursor] != '_'){
            return false;
        }

        try{
            outIndex = static_cast<size_t>(std::stoul(key.substr(digitsStart, cursor - digitsStart)));
        }catch(...){
            return false;
        }
        outField = key.substr(cursor + 1);
        return !outField.empty();
    }

    void buildLegacyElementsIfNeeded(LensFlareAssetData& data){
        if(!data.elements.empty()){
            return;
        }

        const std::string legacyTextureRef = data.textureRef;

        LensFlareElementData source;
        source.type = LensFlareElementType::Image;
        source.textureRef = legacyTextureRef;
        source.tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
        source.intensity = 1.0f;
        source.axisPosition = 1.0f;
        source.sizeScale = 1.0f;
        data.elements.push_back(source);

        LensFlareElementData ghostA;
        ghostA.type = LensFlareElementType::Image;
        ghostA.textureRef = legacyTextureRef;
        ghostA.tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
        ghostA.intensity = Math3D::Clamp(data.ghostIntensity * 0.75f, 0.0f, 4.0f);
        ghostA.axisPosition = 1.0f - data.ghostSpacing;
        ghostA.sizeScale = 0.58f;
        data.elements.push_back(ghostA);

        LensFlareElementData ghostB;
        ghostB.type = LensFlareElementType::Image;
        ghostB.textureRef = legacyTextureRef;
        ghostB.tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
        ghostB.intensity = Math3D::Clamp(data.ghostIntensity, 0.0f, 4.0f);
        ghostB.axisPosition = -(0.30f + data.ghostSpacing);
        ghostB.sizeScale = 0.82f;
        data.elements.push_back(ghostB);

        LensFlareElementData ghostC;
        ghostC.type = LensFlareElementType::Image;
        ghostC.textureRef = legacyTextureRef;
        ghostC.tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
        ghostC.intensity = Math3D::Clamp(data.ghostIntensity * 0.60f, 0.0f, 4.0f);
        ghostC.axisPosition = -(1.00f + data.ghostSpacing * 0.70f);
        ghostC.sizeScale = 0.44f;
        data.elements.push_back(ghostC);

        LensFlareElementData halo;
        halo.type = LensFlareElementType::Circle;
        halo.tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
        halo.intensity = Math3D::Clamp(data.haloIntensity * 0.38f, 0.0f, 4.0f);
        halo.axisPosition = 1.0f;
        halo.sizeScale = Math3D::Clamp(data.haloScale, 0.1f, 6.0f);
        halo.polygonSides = 64;
        data.elements.push_back(halo);
    }

    bool parseLensFlareAssetText(const std::string& text, LensFlareAssetData& outData){
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
            size_t elementIndex = 0;
            std::string elementField;
            if(key == "name"){
                outData.name = value;
            }else if(key == "texture" || key == "texture_ref" || key == "flare_texture"){
                outData.textureRef = value;
            }else if(key == "tint" || key == "color"){
                outData.tint = parseVec3(value, outData.tint);
            }else if(key == "intensity"){
                outData.intensity = parseFloat(value, outData.intensity);
            }else if(key == "sprite_scale" || key == "size" || key == "sprite_size"){
                outData.spriteScale = parseFloat(value, outData.spriteScale);
            }else if(key == "ghost_intensity"){
                outData.ghostIntensity = parseFloat(value, outData.ghostIntensity);
            }else if(key == "ghost_spacing"){
                outData.ghostSpacing = parseFloat(value, outData.ghostSpacing);
            }else if(key == "halo_intensity"){
                outData.haloIntensity = parseFloat(value, outData.haloIntensity);
            }else if(key == "halo_scale"){
                outData.haloScale = parseFloat(value, outData.haloScale);
            }else if(key == "glare_threshold"){
                outData.glareThreshold = parseFloat(value, outData.glareThreshold);
            }else if(key == "glare_intensity"){
                outData.glareIntensity = parseFloat(value, outData.glareIntensity);
            }else if(key == "glare_length_px" || key == "glare_length"){
                outData.glareLengthPx = parseFloat(value, outData.glareLengthPx);
            }else if(key == "glare_falloff"){
                outData.glareFalloff = parseFloat(value, outData.glareFalloff);
            }else if(key == "element_count"){
                int count = std::max(parseInt(value, static_cast<int>(outData.elements.size())), 0);
                outData.elements.resize(static_cast<size_t>(count));
            }else if(tryParseElementKey(key, elementIndex, elementField)){
                if(outData.elements.size() <= elementIndex){
                    outData.elements.resize(elementIndex + 1);
                }

                LensFlareElementData& element = outData.elements[elementIndex];
                if(elementField == "type"){
                    element.type = parseElementType(value);
                    if(element.type == LensFlareElementType::Circle){
                        element.polygonSides = 64;
                    }
                }else if(elementField == "texture" || elementField == "texture_ref"){
                    element.textureRef = value;
                }else if(elementField == "tint" || elementField == "color"){
                    element.tint = parseVec3(value, element.tint);
                }else if(elementField == "intensity"){
                    element.intensity = parseFloat(value, element.intensity);
                }else if(elementField == "axis_position" || elementField == "position" || elementField == "axis"){
                    element.axisPosition = parseFloat(value, element.axisPosition);
                }else if(elementField == "size_scale" || elementField == "scale" || elementField == "size"){
                    element.sizeScale = parseFloat(value, element.sizeScale);
                }else if(elementField == "polygon_sides" || elementField == "sides"){
                    element.polygonSides = std::max(parseInt(value, element.polygonSides), 3);
                }
            }
        }
        buildLegacyElementsIfNeeded(outData);
        return true;
    }
}

namespace LensFlareAssetIO {

bool IsLensFlareAssetPath(const std::filesystem::path& path){
    return isLensFlareAssetPathInternal(path);
}

bool LoadFromAbsolutePath(const std::filesystem::path& path, LensFlareAssetData& outData, std::string* outError){
    if(!IsLensFlareAssetPath(path)){
        if(outError){
            *outError = "Lens flare asset files must use .flare.asset extension.";
        }
        return false;
    }

    bool isDirectory = false;
    if(!pathExists(path, &isDirectory) || isDirectory){
        if(outError){
            *outError = "Lens flare asset does not exist: " + path.generic_string();
        }
        return false;
    }

    outData = LensFlareAssetData{};
    std::string text;
    if(!readTextPath(path, text, outError)){
        return false;
    }

    parseLensFlareAssetText(text, outData);
    if(outData.name.empty()){
        outData.name = path.filename().string();
    }
    return true;
}

bool LoadFromAssetRef(const std::string& assetRef, LensFlareAssetData& outData, std::string* outError){
    outData = LensFlareAssetData{};

    std::string text;
    if(!readTextRefOrPath(assetRef, text, outError)){
        return false;
    }

    parseLensFlareAssetText(text, outData);
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

bool SaveToAbsolutePath(const std::filesystem::path& path, const LensFlareAssetData& data, std::string* outError){
    if(!IsLensFlareAssetPath(path)){
        if(outError){
            *outError = "Lens flare asset files must use .flare.asset extension.";
        }
        return false;
    }

    const std::string name = data.name.empty() ? path.filename().string() : data.name;
    std::string text;
    text += StringUtils::Format("name=%s\n", name.c_str());
    text += StringUtils::Format("texture=%s\n", data.textureRef.c_str());
    text += StringUtils::Format("tint=%s\n", vec3ToString(data.tint).c_str());
    text += StringUtils::Format("intensity=%.6f\n", data.intensity);
    text += StringUtils::Format("sprite_scale=%.6f\n", data.spriteScale);
    text += StringUtils::Format("ghost_intensity=%.6f\n", data.ghostIntensity);
    text += StringUtils::Format("ghost_spacing=%.6f\n", data.ghostSpacing);
    text += StringUtils::Format("halo_intensity=%.6f\n", data.haloIntensity);
    text += StringUtils::Format("halo_scale=%.6f\n", data.haloScale);
    text += StringUtils::Format("glare_threshold=%.6f\n", data.glareThreshold);
    text += StringUtils::Format("glare_intensity=%.6f\n", data.glareIntensity);
    text += StringUtils::Format("glare_length_px=%.6f\n", data.glareLengthPx);
    text += StringUtils::Format("glare_falloff=%.6f\n", data.glareFalloff);
    text += StringUtils::Format("element_count=%d\n", static_cast<int>(data.elements.size()));
    for(size_t i = 0; i < data.elements.size(); ++i){
        const LensFlareElementData& element = data.elements[i];
        text += StringUtils::Format("element%d_type=%s\n", static_cast<int>(i), elementTypeToString(element.type).c_str());
        text += StringUtils::Format("element%d_texture=%s\n", static_cast<int>(i), element.textureRef.c_str());
        text += StringUtils::Format("element%d_tint=%s\n", static_cast<int>(i), vec3ToString(element.tint).c_str());
        text += StringUtils::Format("element%d_intensity=%.6f\n", static_cast<int>(i), element.intensity);
        text += StringUtils::Format("element%d_axis_position=%.6f\n", static_cast<int>(i), element.axisPosition);
        text += StringUtils::Format("element%d_size_scale=%.6f\n", static_cast<int>(i), element.sizeScale);
        text += StringUtils::Format("element%d_polygon_sides=%d\n", static_cast<int>(i), element.polygonSides);
    }
    return writeTextPath(path, text, outError);
}

bool SaveToAssetRef(const std::string& assetRef, const LensFlareAssetData& data, std::string* outError){
    const std::string name = data.name.empty() ? std::filesystem::path(assetRef).filename().string() : data.name;
    std::string text;
    text += StringUtils::Format("name=%s\n", name.c_str());
    text += StringUtils::Format("texture=%s\n", data.textureRef.c_str());
    text += StringUtils::Format("tint=%s\n", vec3ToString(data.tint).c_str());
    text += StringUtils::Format("intensity=%.6f\n", data.intensity);
    text += StringUtils::Format("sprite_scale=%.6f\n", data.spriteScale);
    text += StringUtils::Format("ghost_intensity=%.6f\n", data.ghostIntensity);
    text += StringUtils::Format("ghost_spacing=%.6f\n", data.ghostSpacing);
    text += StringUtils::Format("halo_intensity=%.6f\n", data.haloIntensity);
    text += StringUtils::Format("halo_scale=%.6f\n", data.haloScale);
    text += StringUtils::Format("glare_threshold=%.6f\n", data.glareThreshold);
    text += StringUtils::Format("glare_intensity=%.6f\n", data.glareIntensity);
    text += StringUtils::Format("glare_length_px=%.6f\n", data.glareLengthPx);
    text += StringUtils::Format("glare_falloff=%.6f\n", data.glareFalloff);
    text += StringUtils::Format("element_count=%d\n", static_cast<int>(data.elements.size()));
    for(size_t i = 0; i < data.elements.size(); ++i){
        const LensFlareElementData& element = data.elements[i];
        text += StringUtils::Format("element%d_type=%s\n", static_cast<int>(i), elementTypeToString(element.type).c_str());
        text += StringUtils::Format("element%d_texture=%s\n", static_cast<int>(i), element.textureRef.c_str());
        text += StringUtils::Format("element%d_tint=%s\n", static_cast<int>(i), vec3ToString(element.tint).c_str());
        text += StringUtils::Format("element%d_intensity=%.6f\n", static_cast<int>(i), element.intensity);
        text += StringUtils::Format("element%d_axis_position=%.6f\n", static_cast<int>(i), element.axisPosition);
        text += StringUtils::Format("element%d_size_scale=%.6f\n", static_cast<int>(i), element.sizeScale);
        text += StringUtils::Format("element%d_polygon_sides=%d\n", static_cast<int>(i), element.polygonSides);
    }
    return writeTextAsset(assetRef, text, outError);
}

} // namespace LensFlareAssetIO
