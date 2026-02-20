#include "MaterialAsset.h"

#include "Asset.h"
#include "File.h"
#include "MaterialDefaults.h"
#include "PBRMaterial.h"
#include "ShaderAsset.h"
#include "StringUtils.h"
#include "Texture.h"

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
        return std::filesystem::path(File::GetCWD()) / "res";
    }

    std::string makeAssetRefFromRelative(const std::string& relative){
        if(relative.empty()){
            return std::string(ASSET_DELIMITER);
        }
        return std::string(ASSET_DELIMITER) + "/" + relative;
    }

    bool isAssetRef(const std::string& value){
        return StringUtils::BeginsWith(value, ASSET_DELIMITER);
    }

    bool readTextAsset(const std::string& assetRef, std::string& outText, std::string* outError){
        auto asset = AssetManager::Instance.getOrLoad(assetRef);
        if(!asset){
            if(outError){
                *outError = "Failed to load asset: " + assetRef;
            }
            return false;
        }
        outText = asset->asString();
        if(outText.empty()){
            auto raw = asset->asRaw();
            outText.assign(raw.begin(), raw.end());
        }
        return true;
    }

    bool readTextPath(const std::filesystem::path& path, std::string& outText, std::string* outError){
        std::error_code ec;
        const std::filesystem::path assetRoot = std::filesystem::weakly_canonical(getAssetRootPath(), ec);
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
            return readTextAsset(makeAssetRefFromRelative(rel.generic_string()), outText, outError);
        }

        auto asset = std::make_shared<Asset>(path.string());
        if(!asset || !asset->load()){
            if(outError){
                *outError = "Failed to load file: " + path.generic_string();
            }
            return false;
        }
        outText = asset->asString();
        if(outText.empty()){
            auto raw = asset->asRaw();
            outText.assign(raw.begin(), raw.end());
        }
        return true;
    }

    bool readTextRefOrPath(const std::string& refOrPath, std::string& outText, std::string* outError){
        if(refOrPath.empty()){
            outText.clear();
            return true;
        }
        if(isAssetRef(refOrPath)){
            return readTextAsset(refOrPath, outText, outError);
        }
        return readTextPath(std::filesystem::path(refOrPath), outText, outError);
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
        if(assetRef.empty()){
            return false;
        }
        if(isAssetRef(assetRef)){
            std::string rel = assetRef.substr(std::strlen(ASSET_DELIMITER));
            if(!rel.empty() && (rel[0] == '/' || rel[0] == '\\')){
                rel.erase(rel.begin());
            }
            outPath = getAssetRootPath() / rel;
            return true;
        }
        outPath = std::filesystem::path(assetRef);
        return true;
    }

    std::string toAssetRefFromAbsolutePath(const std::filesystem::path& absolutePath){
        std::error_code ec;
        std::filesystem::path assetRoot = std::filesystem::weakly_canonical(getAssetRootPath(), ec);
        if(ec){
            assetRoot = getAssetRootPath().lexically_normal();
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
        return std::string(ASSET_DELIMITER) + "/" + rel.generic_string();
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
    std::error_code ec;
    if(!std::filesystem::exists(resolvedPath, ec) || std::filesystem::is_directory(resolvedPath, ec)){
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
    std::filesystem::path parent = path.parent_path();
    std::error_code ec;
    if(!parent.empty() && !std::filesystem::exists(parent, ec)){
        if(!std::filesystem::create_directories(parent, ec)){
            if(outError){
                *outError = "Failed to create directory: " + parent.generic_string();
            }
            return false;
        }
    }

    auto writer = std::make_unique<FileWriter>(new File(path.string()));
    if(!writer){
        if(outError){
            *outError = "Failed to open file for write: " + path.generic_string();
        }
        return false;
    }

    writer->putln(StringUtils::Format("name=%s", data.name.c_str()).c_str());
    writer->putln(StringUtils::Format("@link-parent=%s", data.linkParentRef.c_str()).c_str());
    writer->putln(StringUtils::Format("type=%s", TypeToString(data.type)).c_str());
    writer->putln(StringUtils::Format("shader_asset=%s", data.shaderAssetRef.c_str()).c_str());
    writer->putln(StringUtils::Format("color=%s", vec4ToString(data.color).c_str()).c_str());
    writer->putln(StringUtils::Format("uv=%s", vec2ToString(data.uv).c_str()).c_str());
    writer->putln(StringUtils::Format("texture=%s", data.textureRef.c_str()).c_str());
    writer->putln(StringUtils::Format("metallic=%.6f", data.metallic).c_str());
    writer->putln(StringUtils::Format("roughness=%.6f", data.roughness).c_str());
    writer->putln(StringUtils::Format("normal_scale=%.6f", data.normalScale).c_str());
    writer->putln(StringUtils::Format("height_scale=%.6f", data.heightScale).c_str());
    writer->putln(StringUtils::Format("emissive_color=%s", vec3ToString(data.emissiveColor).c_str()).c_str());
    writer->putln(StringUtils::Format("emissive_strength=%.6f", data.emissiveStrength).c_str());
    writer->putln(StringUtils::Format("occlusion_strength=%.6f", data.occlusionStrength).c_str());
    writer->putln(StringUtils::Format("env_strength=%.6f", data.envStrength).c_str());
    writer->putln(StringUtils::Format("use_env_map=%d", data.useEnvMap).c_str());
    writer->putln(StringUtils::Format("uv_scale=%s", vec2ToString(data.uvScale).c_str()).c_str());
    writer->putln(StringUtils::Format("uv_offset=%s", vec2ToString(data.uvOffset).c_str()).c_str());
    writer->putln(StringUtils::Format("alpha_cutoff=%.6f", data.alphaCutoff).c_str());
    writer->putln(StringUtils::Format("use_alpha_clip=%d", data.useAlphaClip).c_str());
    writer->putln(StringUtils::Format("base_color_tex=%s", data.baseColorTexRef.c_str()).c_str());
    writer->putln(StringUtils::Format("roughness_tex=%s", data.roughnessTexRef.c_str()).c_str());
    writer->putln(StringUtils::Format("metallic_roughness_tex=%s", data.metallicRoughnessTexRef.c_str()).c_str());
    writer->putln(StringUtils::Format("normal_tex=%s", data.normalTexRef.c_str()).c_str());
    writer->putln(StringUtils::Format("height_tex=%s", data.heightTexRef.c_str()).c_str());
    writer->putln(StringUtils::Format("emissive_tex=%s", data.emissiveTexRef.c_str()).c_str());
    writer->putln(StringUtils::Format("occlusion_tex=%s", data.occlusionTexRef.c_str()).c_str());
    writer->putln(StringUtils::Format("casts_shadows=%d", data.castsShadows ? 1 : 0).c_str());
    writer->putln(StringUtils::Format("receives_shadows=%d", data.receivesShadows ? 1 : 0).c_str());

    if(!writer->flush()){
        if(outError){
            *outError = "Failed to write file: " + path.generic_string();
        }
        writer->close();
        return false;
    }
    writer->close();
    AssetManager::Instance.unmanageAsset(path.generic_string());
    return true;
}

bool SaveToAssetRef(const std::string& assetRef, const MaterialAssetData& data, std::string* outError){
    std::filesystem::path path;
    if(!toAbsolutePathFromAssetRef(assetRef, path)){
        if(outError){
            *outError = "Invalid material asset path: " + assetRef;
        }
        return false;
    }
    return SaveToAbsolutePath(path, data, outError);
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
        std::error_code ec;
        if(std::filesystem::exists(defaultMaterialAsset, ec) && !std::filesystem::is_directory(defaultMaterialAsset, ec)){
            outData.materialAssetRef = toAssetRefFromAbsolutePath(defaultMaterialAsset);
            return true;
        }
        ec.clear();
        if(std::filesystem::exists(legacyMaterialAsset, ec) && !std::filesystem::is_directory(legacyMaterialAsset, ec)){
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
            std::error_code ec;
            if(toAbsolutePathFromAssetRef(resolvedRef, resolvedPath) &&
               std::filesystem::exists(resolvedPath, ec) &&
               !std::filesystem::is_directory(resolvedPath, ec) &&
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

    std::filesystem::path parent = path.parent_path();
    std::error_code ec;
    if(!parent.empty() && !std::filesystem::exists(parent, ec)){
        if(!std::filesystem::create_directories(parent, ec)){
            if(outError){
                *outError = "Failed to create directory: " + parent.generic_string();
            }
            return false;
        }
    }

    std::string resolvedAssetRef = data.materialAssetRef;
    if(!resolvedAssetRef.empty()){
        std::string normalizedRef;
        if(!ResolveMaterialAssetRef(resolvedAssetRef, normalizedRef, outError)){
            return false;
        }
        resolvedAssetRef = normalizedRef;
    }

    auto writer = std::make_unique<FileWriter>(new File(path.string()));
    if(!writer){
        if(outError){
            *outError = "Failed to open file for write: " + path.generic_string();
        }
        return false;
    }

    const std::string objectName = data.name.empty() ? path.stem().string() : data.name;
    writer->putln(StringUtils::Format("name=%s", objectName.c_str()).c_str());
    writer->putln(StringUtils::Format("material_asset=%s", resolvedAssetRef.c_str()).c_str());

    if(!writer->flush()){
        if(outError){
            *outError = "Failed to write file: " + path.generic_string();
        }
        writer->close();
        return false;
    }
    writer->close();
    AssetManager::Instance.unmanageAsset(path.generic_string());

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
            std::error_code ec;
            if(!std::filesystem::exists(absolutePath, ec) || std::filesystem::is_directory(absolutePath, ec)){
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
