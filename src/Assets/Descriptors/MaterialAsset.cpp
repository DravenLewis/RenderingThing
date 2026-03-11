/**
 * @file src/Assets/Descriptors/MaterialAsset.cpp
 * @brief Implementation for MaterialAsset.
 */

#include "Assets/Descriptors/MaterialAsset.h"

#include "Assets/Core/Asset.h"
#include "Assets/Core/AssetDescriptorUtils.h"
#include "Foundation/IO/File.h"
#include "Rendering/Materials/MaterialDefaults.h"
#include "Rendering/Materials/PBRMaterial.h"
#include "Assets/Descriptors/ShaderAsset.h"
#include "Foundation/Util/StringUtils.h"
#include "Rendering/Textures/Texture.h"

#include <cstring>
#include <sstream>

namespace {
    std::string trimCopy(const std::string& value){
        return StringUtils::Trim(value);
    }

    std::string toLowerCopy(const std::string& value){
        return StringUtils::ToLowerCase(value);
    }

    std::filesystem::path getAssetRootPath(){
        return AssetDescriptorUtils::GetAssetRootPath();
    }

    std::string makeAssetRefFromRelative(const std::string& relative){
        return AssetDescriptorUtils::MakeAssetRefFromRelative(relative);
    }

    bool isAssetRef(const std::string& value){
        return AssetDescriptorUtils::IsAssetRef(value);
    }

    bool readTextAsset(const std::string& assetRef, std::string& outText, std::string* outError){
        return AssetDescriptorUtils::ReadTextAsset(assetRef, outText, outError);
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

    bool isMaterialAssetPathInternal(const std::filesystem::path& path){
        const std::string lower = toLowerCopy(path.generic_string());
        return StringUtils::EndsWith(lower, ".material.asset") || StringUtils::EndsWith(lower, ".mat.asset");
    }

    bool isMaterialObjectPathInternal(const std::filesystem::path& path){
        const std::string lower = toLowerCopy(path.generic_string());
        if(StringUtils::EndsWith(lower, ".material.asset")){
            return false;
        }
        return StringUtils::EndsWith(lower, ".material");
    }

    bool toAbsolutePathFromAssetRef(const std::string& assetRef, std::filesystem::path& outPath){
        return AssetDescriptorUtils::AssetRefToAbsolutePath(assetRef, outPath);
    }

    std::string toAssetRefFromAbsolutePath(const std::filesystem::path& absolutePath){
        return AssetDescriptorUtils::AbsolutePathToAssetRef(absolutePath);
    }

    bool pathExists(const std::filesystem::path& path, bool* outIsDirectory = nullptr){
        return AssetDescriptorUtils::PathExists(path, outIsDirectory);
    }

    Math3D::Vec2 parseVec2(const std::string& value, const Math3D::Vec2& fallback){
        auto parts = StringUtils::Split(value, ",");
        if(parts.size() < 2){
            return fallback;
        }
        try{
            return Math3D::Vec2(
                std::stof(trimCopy(parts[0])),
                std::stof(trimCopy(parts[1]))
            );
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

    Math3D::Vec4 parseVec4(const std::string& value, const Math3D::Vec4& fallback){
        auto parts = StringUtils::Split(value, ",");
        if(parts.size() < 4){
            return fallback;
        }
        try{
            return Math3D::Vec4(
                std::stof(trimCopy(parts[0])),
                std::stof(trimCopy(parts[1])),
                std::stof(trimCopy(parts[2])),
                std::stof(trimCopy(parts[3]))
            );
        }catch(...){
            return fallback;
        }
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

    bool parseBool(const std::string& value, bool fallback){
        std::string lower = toLowerCopy(trimCopy(value));
        if(lower == "1" || lower == "true" || lower == "yes" || lower == "on"){
            return true;
        }
        if(lower == "0" || lower == "false" || lower == "no" || lower == "off"){
            return false;
        }
        return fallback;
    }

    std::string vec2ToString(const Math3D::Vec2& value){
        return StringUtils::Format("%.6f,%.6f", value.x, value.y);
    }

    std::string vec3ToString(const Math3D::Vec3& value){
        return StringUtils::Format("%.6f,%.6f,%.6f", value.x, value.y, value.z);
    }

    std::string vec4ToString(const Math3D::Vec4& value){
        return StringUtils::Format("%.6f,%.6f,%.6f,%.6f", value.x, value.y, value.z, value.w);
    }

    std::shared_ptr<Texture> loadTextureFromRef(const std::string& ref){
        if(ref.empty()){
            return nullptr;
        }
        auto asset = AssetManager::Instance.getOrLoad(ref);
        if(!asset){
            return nullptr;
        }
        return Texture::Load(asset);
    }

    std::shared_ptr<Texture> defaultPreviewTexture(){
        static std::shared_ptr<Texture> cached;
        if(cached){
            return cached;
        }
        auto asset = AssetManager::Instance.getOrLoad(std::string(ASSET_DELIMITER) + "/images/uv.png");
        if(asset){
            cached = Texture::Load(asset);
        }
        if(!cached){
            cached = Texture::CreateEmpty(1, 1);
        }
        return cached;
    }

    bool parseMaterialAssetText(const std::string& text, MaterialAssetData& outData){
        std::istringstream stream(text);
        std::string line;
        while(std::getline(stream, line)){
            std::string trimmed = trimCopy(line);
            if(trimmed.empty()){
                continue;
            }
            if(StringUtils::BeginsWith(trimmed, "#") || StringUtils::BeginsWith(trimmed, "//") || StringUtils::BeginsWith(trimmed, ";")){
                continue;
            }
            size_t eq = trimmed.find('=');
            if(eq == std::string::npos){
                continue;
            }
            std::string key = toLowerCopy(trimCopy(trimmed.substr(0, eq)));
            std::string value = trimCopy(trimmed.substr(eq + 1));

            if(key == "name"){
                outData.name = value;
            }else if(key == "@link-parent" || key == "link-parent" || key == "link_parent" || key == "linkparent"){
                outData.linkParentRef = value;
            }else if(key == "type" || key == "material_type"){
                outData.type = MaterialAssetIO::TypeFromString(value);
            }else if(key == "shader_asset" || key == "shader"){
                outData.shaderAssetRef = value;
            }else if(key == "color"){
                outData.color = parseVec4(value, outData.color);
            }else if(key == "uv"){
                outData.uv = parseVec2(value, outData.uv);
            }else if(key == "texture" || key == "texture_ref"){
                outData.textureRef = value;
            }else if(key == "metallic"){
                outData.metallic = parseFloat(value, outData.metallic);
            }else if(key == "roughness"){
                outData.roughness = parseFloat(value, outData.roughness);
            }else if(key == "normal_scale"){
                outData.normalScale = parseFloat(value, outData.normalScale);
            }else if(key == "height_scale"){
                outData.heightScale = parseFloat(value, outData.heightScale);
            }else if(key == "emissive_color"){
                outData.emissiveColor = parseVec3(value, outData.emissiveColor);
            }else if(key == "emissive_strength"){
                outData.emissiveStrength = parseFloat(value, outData.emissiveStrength);
            }else if(key == "occlusion_strength"){
                outData.occlusionStrength = parseFloat(value, outData.occlusionStrength);
            }else if(key == "env_strength"){
                outData.envStrength = parseFloat(value, outData.envStrength);
            }else if(key == "use_env_map"){
                outData.useEnvMap = parseInt(value, outData.useEnvMap);
            }else if(key == "uv_scale"){
                outData.uvScale = parseVec2(value, outData.uvScale);
            }else if(key == "uv_offset"){
                outData.uvOffset = parseVec2(value, outData.uvOffset);
            }else if(key == "alpha_cutoff"){
                outData.alphaCutoff = parseFloat(value, outData.alphaCutoff);
            }else if(key == "use_alpha_clip"){
                outData.useAlphaClip = parseInt(value, outData.useAlphaClip);
            }else if(key == "base_color_tex"){
                outData.baseColorTexRef = value;
            }else if(key == "roughness_tex"){
                outData.roughnessTexRef = value;
            }else if(key == "metallic_roughness_tex"){
                outData.metallicRoughnessTexRef = value;
            }else if(key == "normal_tex"){
                outData.normalTexRef = value;
            }else if(key == "height_tex"){
                outData.heightTexRef = value;
            }else if(key == "emissive_tex"){
                outData.emissiveTexRef = value;
            }else if(key == "occlusion_tex"){
                outData.occlusionTexRef = value;
            }else if(key == "casts_shadows"){
                outData.castsShadows = parseBool(value, outData.castsShadows);
            }else if(key == "receives_shadows"){
                outData.receivesShadows = parseBool(value, outData.receivesShadows);
            }
        }
        return true;
    }

    bool parseMaterialObjectText(const std::string& text, MaterialObjectData& outData){
        std::istringstream stream(text);
        std::string line;
        while(std::getline(stream, line)){
            std::string trimmed = trimCopy(line);
            if(trimmed.empty()){
                continue;
            }
            if(StringUtils::BeginsWith(trimmed, "#") || StringUtils::BeginsWith(trimmed, "//") || StringUtils::BeginsWith(trimmed, ";")){
                continue;
            }
            size_t eq = trimmed.find('=');
            if(eq == std::string::npos){
                continue;
            }

            std::string key = toLowerCopy(trimCopy(trimmed.substr(0, eq)));
            std::string value = trimCopy(trimmed.substr(eq + 1));
            if(key == "name"){
                outData.name = value;
            }else if(key == "material_asset" || key == "asset" || key == "source_asset" || key == "material_asset_ref"){
                outData.materialAssetRef = value;
            }
        }
        return true;
    }
}

namespace MaterialAssetIO {

bool IsMaterialAssetPath(const std::filesystem::path& path){
    return isMaterialAssetPathInternal(path);
}

bool IsMaterialObjectPath(const std::filesystem::path& path){
    return isMaterialObjectPathInternal(path);
}

bool IsMaterialPath(const std::filesystem::path& path){
    return IsMaterialAssetPath(path) || IsMaterialObjectPath(path);
}

const char* TypeToString(MaterialAssetType type){
    switch(type){
        case MaterialAssetType::PBR: return "PBR";
        case MaterialAssetType::Color: return "Color";
        case MaterialAssetType::Image: return "Image";
        case MaterialAssetType::LitColor: return "LitColor";
        case MaterialAssetType::LitImage: return "LitImage";
        case MaterialAssetType::FlatColor: return "FlatColor";
        case MaterialAssetType::FlatImage: return "FlatImage";
        default: return "PBR";
    }
}

MaterialAssetType TypeFromString(const std::string& value){
    std::string lower = toLowerCopy(trimCopy(value));
    if(lower == "color") return MaterialAssetType::Color;
    if(lower == "image") return MaterialAssetType::Image;
    if(lower == "litcolor" || lower == "lit_color") return MaterialAssetType::LitColor;
    if(lower == "litimage" || lower == "lit_image") return MaterialAssetType::LitImage;
    if(lower == "flatcolor" || lower == "flat_color") return MaterialAssetType::FlatColor;
    if(lower == "flatimage" || lower == "flat_image") return MaterialAssetType::FlatImage;
    return MaterialAssetType::PBR;
}

bool LoadFromAbsolutePath(const std::filesystem::path& path, MaterialAssetData& outData, std::string* outError){
    std::string resolvedAssetRef;
    if(!ResolveMaterialAssetRef(path.generic_string(), resolvedAssetRef, outError)){
        return false;
    }

    std::filesystem::path resolvedPath;
    if(!toAbsolutePathFromAssetRef(resolvedAssetRef, resolvedPath)){
        if(outError){
            *outError = "Invalid resolved material asset path: " + resolvedAssetRef;
        }
        return false;
    }

    outData = MaterialAssetData{};
    std::string text;
    if(!readTextPath(resolvedPath, text, outError)){
        return false;
    }
    parseMaterialAssetText(text, outData);
    if(outData.name.empty()){
        outData.name = resolvedPath.filename().string();
    }
    return true;
}

bool LoadFromAssetRef(const std::string& assetRef, MaterialAssetData& outData, std::string* outError){
    std::string resolvedAssetRef;
    if(!ResolveMaterialAssetRef(assetRef, resolvedAssetRef, outError)){
        return false;
    }

    std::filesystem::path resolvedPath;
    if(!toAbsolutePathFromAssetRef(resolvedAssetRef, resolvedPath)){
        if(outError){
            *outError = "Invalid resolved material asset path: " + resolvedAssetRef;
        }
        return false;
    }
    bool isDirectory = false;
    if(!pathExists(resolvedPath, &isDirectory) || isDirectory){
        if(outError){
            *outError = "Material asset does not exist: " + resolvedPath.generic_string();
        }
        return false;
    }

    outData = MaterialAssetData{};
    std::string text;
    if(!readTextRefOrPath(resolvedAssetRef, text, outError)){
        return false;
    }
    parseMaterialAssetText(text, outData);
    if(outData.name.empty()){
        outData.name = resolvedPath.filename().string();
    }
    return true;
}

bool SaveToAbsolutePath(const std::filesystem::path& path, const MaterialAssetData& data, std::string* outError){
    std::string text;
    text += StringUtils::Format("name=%s\n", data.name.c_str());
    text += StringUtils::Format("@link-parent=%s\n", data.linkParentRef.c_str());
    text += StringUtils::Format("type=%s\n", TypeToString(data.type));
    text += StringUtils::Format("shader_asset=%s\n", data.shaderAssetRef.c_str());
    text += StringUtils::Format("color=%s\n", vec4ToString(data.color).c_str());
    text += StringUtils::Format("uv=%s\n", vec2ToString(data.uv).c_str());
    text += StringUtils::Format("texture=%s\n", data.textureRef.c_str());
    text += StringUtils::Format("metallic=%.6f\n", data.metallic);
    text += StringUtils::Format("roughness=%.6f\n", data.roughness);
    text += StringUtils::Format("normal_scale=%.6f\n", data.normalScale);
    text += StringUtils::Format("height_scale=%.6f\n", data.heightScale);
    text += StringUtils::Format("emissive_color=%s\n", vec3ToString(data.emissiveColor).c_str());
    text += StringUtils::Format("emissive_strength=%.6f\n", data.emissiveStrength);
    text += StringUtils::Format("occlusion_strength=%.6f\n", data.occlusionStrength);
    text += StringUtils::Format("env_strength=%.6f\n", data.envStrength);
    text += StringUtils::Format("use_env_map=%d\n", data.useEnvMap);
    text += StringUtils::Format("uv_scale=%s\n", vec2ToString(data.uvScale).c_str());
    text += StringUtils::Format("uv_offset=%s\n", vec2ToString(data.uvOffset).c_str());
    text += StringUtils::Format("alpha_cutoff=%.6f\n", data.alphaCutoff);
    text += StringUtils::Format("use_alpha_clip=%d\n", data.useAlphaClip);
    text += StringUtils::Format("base_color_tex=%s\n", data.baseColorTexRef.c_str());
    text += StringUtils::Format("roughness_tex=%s\n", data.roughnessTexRef.c_str());
    text += StringUtils::Format("metallic_roughness_tex=%s\n", data.metallicRoughnessTexRef.c_str());
    text += StringUtils::Format("normal_tex=%s\n", data.normalTexRef.c_str());
    text += StringUtils::Format("height_tex=%s\n", data.heightTexRef.c_str());
    text += StringUtils::Format("emissive_tex=%s\n", data.emissiveTexRef.c_str());
    text += StringUtils::Format("occlusion_tex=%s\n", data.occlusionTexRef.c_str());
    text += StringUtils::Format("casts_shadows=%d\n", data.castsShadows ? 1 : 0);
    text += StringUtils::Format("receives_shadows=%d\n", data.receivesShadows ? 1 : 0);
    return writeTextPath(path, text, outError);
}

bool SaveToAssetRef(const std::string& assetRef, const MaterialAssetData& data, std::string* outError){
    std::string text;
    text += StringUtils::Format("name=%s\n", data.name.c_str());
    text += StringUtils::Format("@link-parent=%s\n", data.linkParentRef.c_str());
    text += StringUtils::Format("type=%s\n", TypeToString(data.type));
    text += StringUtils::Format("shader_asset=%s\n", data.shaderAssetRef.c_str());
    text += StringUtils::Format("color=%s\n", vec4ToString(data.color).c_str());
    text += StringUtils::Format("uv=%s\n", vec2ToString(data.uv).c_str());
    text += StringUtils::Format("texture=%s\n", data.textureRef.c_str());
    text += StringUtils::Format("metallic=%.6f\n", data.metallic);
    text += StringUtils::Format("roughness=%.6f\n", data.roughness);
    text += StringUtils::Format("normal_scale=%.6f\n", data.normalScale);
    text += StringUtils::Format("height_scale=%.6f\n", data.heightScale);
    text += StringUtils::Format("emissive_color=%s\n", vec3ToString(data.emissiveColor).c_str());
    text += StringUtils::Format("emissive_strength=%.6f\n", data.emissiveStrength);
    text += StringUtils::Format("occlusion_strength=%.6f\n", data.occlusionStrength);
    text += StringUtils::Format("env_strength=%.6f\n", data.envStrength);
    text += StringUtils::Format("use_env_map=%d\n", data.useEnvMap);
    text += StringUtils::Format("uv_scale=%s\n", vec2ToString(data.uvScale).c_str());
    text += StringUtils::Format("uv_offset=%s\n", vec2ToString(data.uvOffset).c_str());
    text += StringUtils::Format("alpha_cutoff=%.6f\n", data.alphaCutoff);
    text += StringUtils::Format("use_alpha_clip=%d\n", data.useAlphaClip);
    text += StringUtils::Format("base_color_tex=%s\n", data.baseColorTexRef.c_str());
    text += StringUtils::Format("roughness_tex=%s\n", data.roughnessTexRef.c_str());
    text += StringUtils::Format("metallic_roughness_tex=%s\n", data.metallicRoughnessTexRef.c_str());
    text += StringUtils::Format("normal_tex=%s\n", data.normalTexRef.c_str());
    text += StringUtils::Format("height_tex=%s\n", data.heightTexRef.c_str());
    text += StringUtils::Format("emissive_tex=%s\n", data.emissiveTexRef.c_str());
    text += StringUtils::Format("occlusion_tex=%s\n", data.occlusionTexRef.c_str());
    text += StringUtils::Format("casts_shadows=%d\n", data.castsShadows ? 1 : 0);
    text += StringUtils::Format("receives_shadows=%d\n", data.receivesShadows ? 1 : 0);
    return writeTextAsset(assetRef, text, outError);
}

std::shared_ptr<Material> InstantiateMaterial(const MaterialAssetData& data, std::string* outError){
    std::shared_ptr<Material> material;

    switch(data.type){
        case MaterialAssetType::Color:{
            material = MaterialDefaults::ColorMaterial::Create(data.color);
            break;
        }
        case MaterialAssetType::Image:{
            auto tex = loadTextureFromRef(data.textureRef);
            if(!tex) tex = defaultPreviewTexture();
            material = MaterialDefaults::ImageMaterial::Create(tex, data.color, data.uv);
            break;
        }
        case MaterialAssetType::LitColor:{
            material = MaterialDefaults::LitColorMaterial::Create(data.color);
            break;
        }
        case MaterialAssetType::LitImage:{
            auto tex = loadTextureFromRef(data.textureRef);
            if(!tex) tex = defaultPreviewTexture();
            material = MaterialDefaults::LitImageMaterial::Create(tex, data.color);
            break;
        }
        case MaterialAssetType::FlatColor:{
            material = MaterialDefaults::FlatColorMaterial::Create(data.color);
            break;
        }
        case MaterialAssetType::FlatImage:{
            auto tex = loadTextureFromRef(data.textureRef);
            if(!tex) tex = defaultPreviewTexture();
            material = MaterialDefaults::FlatImageMaterial::Create(tex, data.color);
            break;
        }
        case MaterialAssetType::PBR:
        default:{
            auto pbr = PBRMaterial::Create(data.color);
            if(pbr){
                pbr->Metallic = data.metallic;
                pbr->Roughness = data.roughness;
                pbr->NormalScale = data.normalScale;
                pbr->HeightScale = data.heightScale;
                pbr->EmissiveColor = data.emissiveColor;
                pbr->EmissiveStrength = data.emissiveStrength;
                pbr->OcclusionStrength = data.occlusionStrength;
                pbr->EnvStrength = data.envStrength;
                pbr->UseEnvMap = data.useEnvMap;
                pbr->UVScale = data.uvScale;
                pbr->UVOffset = data.uvOffset;
                pbr->AlphaCutoff = data.alphaCutoff;
                pbr->UseAlphaClip = data.useAlphaClip;
                pbr->BaseColorTex = loadTextureFromRef(data.baseColorTexRef);
                pbr->RoughnessTex = loadTextureFromRef(data.roughnessTexRef);
                pbr->MetallicRoughnessTex = loadTextureFromRef(data.metallicRoughnessTexRef);
                pbr->NormalTex = loadTextureFromRef(data.normalTexRef);
                pbr->HeightTex = loadTextureFromRef(data.heightTexRef);
                pbr->EmissiveTex = loadTextureFromRef(data.emissiveTexRef);
                pbr->OcclusionTex = loadTextureFromRef(data.occlusionTexRef);
            }
            material = pbr;
            break;
        }
    }

    if(!material){
        if(outError){
            *outError = "Failed to create material instance.";
        }
        return nullptr;
    }

    material->setCastsShadows(data.castsShadows);
    material->setReceivesShadows(data.receivesShadows);

    if(!data.shaderAssetRef.empty()){
        ShaderAssetData shaderData;
        std::string shaderError;
        if(ShaderAssetIO::LoadFromAssetRef(data.shaderAssetRef, shaderData, &shaderError)){
            auto program = ShaderAssetIO::CompileProgram(shaderData, shaderData.cacheName, true, &shaderError);
            if(program && program->getID() != 0){
                material->setShader(program);
            }else if(outError){
                *outError = shaderError;
            }
        }else if(outError){
            *outError = shaderError;
        }
    }

    return material;
}

bool LoadMaterialObjectFromAbsolutePath(const std::filesystem::path& path, MaterialObjectData& outData, std::string* outError){
    if(!IsMaterialObjectPath(path)){
        if(outError){
            *outError = "Not a material object path: " + path.generic_string();
        }
        return false;
    }

    outData = MaterialObjectData{};
    std::string text;
    if(!readTextPath(path, text, outError)){
        return false;
    }
    parseMaterialObjectText(text, outData);
    const std::string originalMaterialAssetRef = outData.materialAssetRef;
    if(outData.name.empty()){
        outData.name = path.stem().string();
    }

    const std::filesystem::path parent = path.parent_path();
    const std::string stem = path.stem().string();
    const std::filesystem::path defaultMaterialAsset = parent / (stem + ".material.asset");
    const std::filesystem::path legacyMaterialAsset = parent / (stem + ".mat.asset");

    auto fallbackToSiblingAsset = [&](){
        bool isDirectory = false;
        if(pathExists(defaultMaterialAsset, &isDirectory) && !isDirectory){
            outData.materialAssetRef = toAssetRefFromAbsolutePath(defaultMaterialAsset);
            return true;
        }
        if(pathExists(legacyMaterialAsset, &isDirectory) && !isDirectory){
            outData.materialAssetRef = toAssetRefFromAbsolutePath(legacyMaterialAsset);
            return true;
        }
        return false;
    };

    bool hasValidResolvedRef = false;
    if(!outData.materialAssetRef.empty()){
        std::string resolvedRef;
        std::string resolveError;
        if(MaterialAssetIO::ResolveMaterialAssetRef(outData.materialAssetRef, resolvedRef, &resolveError)){
            std::filesystem::path resolvedPath;
            bool isDirectory = false;
            if(toAbsolutePathFromAssetRef(resolvedRef, resolvedPath) &&
               pathExists(resolvedPath, &isDirectory) &&
               !isDirectory &&
               IsMaterialAssetPath(resolvedPath)){
                hasValidResolvedRef = true;
                outData.materialAssetRef = resolvedRef;
            }
        }
    }

    if(!hasValidResolvedRef){
        if(!fallbackToSiblingAsset()){
            if(outData.materialAssetRef.empty() && outError){
                *outError = "Material object has no linked material asset: " + path.generic_string();
            }
            outData.materialAssetRef.clear();
        }
    }

    // Heal legacy/broken material links on read so old projects migrate forward.
    if(!outData.materialAssetRef.empty() && outData.materialAssetRef != originalMaterialAssetRef){
        SaveMaterialObjectToAbsolutePath(path, outData, nullptr);
    }

    return true;
}

bool LoadMaterialObjectFromAssetRef(const std::string& assetRef, MaterialObjectData& outData, std::string* outError){
    std::filesystem::path path;
    if(!toAbsolutePathFromAssetRef(assetRef, path)){
        if(outError){
            *outError = "Invalid material object asset ref: " + assetRef;
        }
        return false;
    }
    return LoadMaterialObjectFromAbsolutePath(path, outData, outError);
}

bool SaveMaterialObjectToAbsolutePath(const std::filesystem::path& path, const MaterialObjectData& data, std::string* outError){
    if(!IsMaterialObjectPath(path)){
        if(outError){
            *outError = "Material object files must use .material extension.";
        }
        return false;
    }

    std::string resolvedAssetRef = data.materialAssetRef;
    if(!resolvedAssetRef.empty()){
        std::string normalizedRef;
        if(!ResolveMaterialAssetRef(resolvedAssetRef, normalizedRef, outError)){
            return false;
        }
        resolvedAssetRef = normalizedRef;
    }

    const std::string objectName = data.name.empty() ? path.stem().string() : data.name;
    std::string text;
    text += StringUtils::Format("name=%s\n", objectName.c_str());
    text += StringUtils::Format("material_asset=%s\n", resolvedAssetRef.c_str());
    if(!writeTextPath(path, text, outError)){
        return false;
    }

    if(!resolvedAssetRef.empty()){
        MaterialAssetData linkedData;
        std::string linkedError;
        if(LoadFromAssetRef(resolvedAssetRef, linkedData, &linkedError)){
            const std::string parentRef = toAssetRefFromAbsolutePath(path);
            if(linkedData.linkParentRef != parentRef){
                linkedData.linkParentRef = parentRef;
                SaveToAssetRef(resolvedAssetRef, linkedData, nullptr);
            }
        }
    }

    return true;
}

bool SaveMaterialObjectToAssetRef(const std::string& assetRef, const MaterialObjectData& data, std::string* outError){
    std::filesystem::path path;
    if(!toAbsolutePathFromAssetRef(assetRef, path)){
        if(outError){
            *outError = "Invalid material object asset ref: " + assetRef;
        }
        return false;
    }
    return SaveMaterialObjectToAbsolutePath(path, data, outError);
}

bool ResolveMaterialAssetRef(const std::string& materialOrAssetRef, std::string& outAssetRef, std::string* outError){
    std::string currentRef = StringUtils::Trim(materialOrAssetRef);
    if(currentRef.empty()){
        if(outError){
            *outError = "Material reference is empty.";
        }
        return false;
    }

    constexpr int kMaxResolveDepth = 8;
    for(int depth = 0; depth < kMaxResolveDepth; ++depth){
        std::filesystem::path absolutePath;
        if(!toAbsolutePathFromAssetRef(currentRef, absolutePath)){
            if(outError){
                *outError = "Invalid material reference: " + currentRef;
            }
            return false;
        }

        if(IsMaterialAssetPath(absolutePath)){
            bool isDirectory = false;
            if(!pathExists(absolutePath, &isDirectory) || isDirectory){
                if(outError){
                    *outError = "Material asset does not exist: " + absolutePath.generic_string();
                }
                return false;
            }
            if(isAssetRef(currentRef)){
                outAssetRef = currentRef;
            }else{
                outAssetRef = toAssetRefFromAbsolutePath(absolutePath);
            }
            return true;
        }

        if(IsMaterialObjectPath(absolutePath)){
            MaterialObjectData objectData;
            if(!LoadMaterialObjectFromAbsolutePath(absolutePath, objectData, outError)){
                return false;
            }
            if(objectData.materialAssetRef.empty()){
                if(outError){
                    *outError = "Material object has no linked material asset: " + absolutePath.generic_string();
                }
                return false;
            }
            currentRef = objectData.materialAssetRef;
            continue;
        }

        if(outError){
            *outError = "Unsupported material reference: " + currentRef;
        }
        return false;
    }

    if(outError){
        *outError = "Material reference resolve depth exceeded.";
    }
    return false;
}

std::shared_ptr<Material> InstantiateMaterialFromRef(const std::string& materialOrAssetRef, std::string* outResolvedAssetRef, std::string* outError){
    std::string resolvedAssetRef;
    if(!ResolveMaterialAssetRef(materialOrAssetRef, resolvedAssetRef, outError)){
        return nullptr;
    }

    MaterialAssetData data;
    if(!LoadFromAssetRef(resolvedAssetRef, data, outError)){
        return nullptr;
    }

    if(outResolvedAssetRef){
        *outResolvedAssetRef = resolvedAssetRef;
    }
    return InstantiateMaterial(data, outError);
}

} // namespace MaterialAssetIO
