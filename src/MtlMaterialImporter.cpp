#include "MtlMaterialImporter.h"

#include "AssetDescriptorUtils.h"
#include "ConstructedMaterial.h"
#include "StringUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>

namespace {
    std::string normalizeMtlText(std::string text){
        if(text.empty()){
            return text;
        }

        // Handle UTF-8 BOM.
        if(text.size() >= 3 &&
           static_cast<unsigned char>(text[0]) == 0xEF &&
           static_cast<unsigned char>(text[1]) == 0xBB &&
           static_cast<unsigned char>(text[2]) == 0xBF){
            text.erase(0, 3);
        }

        // Asset text can come through with embedded nulls for UTF-16-like content.
        // Dropping null bytes recovers plain ASCII tokens such as "newmtl".
        text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
        return text;
    }

    std::string stripLeadingNonTokenBytes(const std::string& value){
        if(value.empty()){
            return value;
        }

        size_t start = 0;
        while(start < value.size()){
            const unsigned char c = static_cast<unsigned char>(value[start]);
            const bool isTokenChar =
                std::isalnum(c) != 0 ||
                c == '_' || c == '-' || c == '.' || c == '#';
            if(isTokenChar || std::isspace(c) != 0){
                break;
            }
            start++;
        }
        return value.substr(start);
    }

    std::string trimCopy(const std::string& value){
        return StringUtils::Trim(value);
    }

    std::string toLowerCopy(const std::string& value){
        return StringUtils::ToLowerCase(value);
    }

    float clamp01(float value){
        return Math3D::Clamp(value, 0.0f, 1.0f);
    }

    std::string stripQuotes(const std::string& value){
        std::string out = trimCopy(value);
        if(out.size() >= 2 && out.front() == '"' && out.back() == '"'){
            out = out.substr(1, out.size() - 2);
        }
        return out;
    }

    bool parseVec3(std::istringstream& iss, Math3D::Vec3& out){
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if(!(iss >> x >> y >> z)){
            return false;
        }
        out = Math3D::Vec3(x, y, z);
        return true;
    }

    bool parseFloat(std::istringstream& iss, float& out){
        float value = 0.0f;
        if(!(iss >> value)){
            return false;
        }
        out = value;
        return true;
    }

    bool parseInt(std::istringstream& iss, int& out){
        int value = 0;
        if(!(iss >> value)){
            return false;
        }
        out = value;
        return true;
    }

    std::string parseMapPathRemainder(const std::string& remainder){
        std::istringstream mapStream(remainder);
        std::string token;
        std::string lastValue;
        while(mapStream >> token){
            if(!token.empty() && token[0] == '-'){
                // Skip option payload token.
                std::string skipToken;
                if(mapStream >> skipToken){
                    continue;
                }
                break;
            }
            lastValue = token;
        }
        return stripQuotes(lastValue);
    }

    std::string resolveTextureReference(const std::filesystem::path& mtlPath, const std::string& rawPath){
        std::string texturePath = stripQuotes(rawPath);
        if(texturePath.empty()){
            return "";
        }

        std::filesystem::path inputPath(texturePath);
        if(!inputPath.is_absolute()){
            inputPath = (mtlPath.parent_path() / inputPath).lexically_normal();
        }

        std::error_code ec;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(inputPath, ec);
        if(ec){
            normalized = inputPath.lexically_normal();
        }
        return AssetDescriptorUtils::AbsolutePathToAssetRef(normalized);
    }

    float deriveRoughnessFromShininess(float ns){
        // Common conversion from Phong shininess to roughness.
        const float safeNs = Math3D::Max(1.0f, ns);
        const float roughness = std::sqrt(2.0f / (safeNs + 2.0f));
        return clamp01(roughness);
    }

    float derivePbrMetallicFromMtl(const MtlMaterialDefinition&){
        // Classic .mtl describes Phong specular color/intensity, not metallic workflow data.
        // Treat imported materials as dielectrics by default to avoid over-metallic results.
        return 0.0f;
    }

    float derivePbrRoughnessFromMtl(const MtlMaterialDefinition& def){
        // Keep a minimum roughness so common authoring defaults (e.g. high Ns) do not
        // import as mirror-like materials in the editor.
        return Math3D::Max(0.20f, deriveRoughnessFromShininess(def.shininess));
    }
}

namespace MtlMaterialImporter {

std::string SanitizeMaterialName(const std::string& value){
    std::string out = trimCopy(value);
    if(out.empty()){
        return "ImportedMaterial";
    }

    for(char& c : out){
        const bool isAllowed =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == ' ';
        if(!isAllowed){
            c = '_';
        }
    }

    while(!out.empty() && (out.back() == '.' || out.back() == ' ')){
        out.pop_back();
    }
    if(out.empty()){
        out = "ImportedMaterial";
    }
    return out;
}

bool LoadFromAbsolutePath(const std::filesystem::path& mtlPath,
                          std::vector<MtlMaterialDefinition>& outMaterials,
                          std::string* outError){
    outMaterials.clear();

    std::string text;
    if(!AssetDescriptorUtils::ReadTextPath(mtlPath, text, outError)){
        return false;
    }
    text = normalizeMtlText(std::move(text));

    std::istringstream stream(text);
    std::string line;
    MtlMaterialDefinition* current = nullptr;
    auto ensureCurrentMaterial = [&](){
        if(current){
            return;
        }
        MtlMaterialDefinition entry;
        entry.name = SanitizeMaterialName(mtlPath.stem().string());
        if(entry.name.empty()){
            entry.name = "ImportedMaterial";
        }
        outMaterials.push_back(entry);
        current = &outMaterials.back();
    };

    while(std::getline(stream, line)){
        std::string trimmed = trimCopy(stripLeadingNonTokenBytes(line));
        if(trimmed.empty()){
            continue;
        }
        if(trimmed[0] == '#' || StringUtils::BeginsWith(trimmed, "//") || trimmed[0] == ';'){
            continue;
        }

        std::istringstream iss(trimmed);
        std::string command;
        iss >> command;
        if(command.empty()){
            continue;
        }

        const std::string commandLower = toLowerCopy(command);
        if(commandLower == "newmtl"){
            std::string name;
            std::getline(iss, name);
            MtlMaterialDefinition entry;
            entry.name = trimCopy(name);
            if(entry.name.empty()){
                entry.name = "Material_" + std::to_string(outMaterials.size() + 1);
            }
            outMaterials.push_back(entry);
            current = &outMaterials.back();
            continue;
        }

        if(commandLower == "ka"){
            ensureCurrentMaterial();
            parseVec3(iss, current->ambient);
        }else if(commandLower == "kd"){
            ensureCurrentMaterial();
            parseVec3(iss, current->diffuse);
        }else if(commandLower == "ks"){
            ensureCurrentMaterial();
            parseVec3(iss, current->specular);
        }else if(commandLower == "ke"){
            ensureCurrentMaterial();
            parseVec3(iss, current->emissive);
        }else if(commandLower == "ns"){
            ensureCurrentMaterial();
            parseFloat(iss, current->shininess);
        }else if(commandLower == "d"){
            ensureCurrentMaterial();
            parseFloat(iss, current->dissolve);
            current->dissolve = clamp01(current->dissolve);
        }else if(commandLower == "tr"){
            ensureCurrentMaterial();
            float tr = 0.0f;
            if(parseFloat(iss, tr)){
                current->dissolve = clamp01(1.0f - tr);
            }
        }else if(commandLower == "illum"){
            ensureCurrentMaterial();
            parseInt(iss, current->illumModel);
        }else if(commandLower == "map_kd"){
            ensureCurrentMaterial();
            std::string remainder;
            std::getline(iss, remainder);
            current->diffuseMapRef = resolveTextureReference(mtlPath, parseMapPathRemainder(remainder));
        }else if(commandLower == "map_bump" || commandLower == "bump"){
            ensureCurrentMaterial();
            std::string remainder;
            std::getline(iss, remainder);
            current->normalMapRef = resolveTextureReference(mtlPath, parseMapPathRemainder(remainder));
        }else if(commandLower == "map_d"){
            ensureCurrentMaterial();
            std::string remainder;
            std::getline(iss, remainder);
            current->opacityMapRef = resolveTextureReference(mtlPath, parseMapPathRemainder(remainder));
        }else if(commandLower == "map_ke"){
            ensureCurrentMaterial();
            std::string remainder;
            std::getline(iss, remainder);
            current->emissiveMapRef = resolveTextureReference(mtlPath, parseMapPathRemainder(remainder));
        }
    }

    if(outMaterials.empty()){
        MtlMaterialDefinition fallback;
        fallback.name = SanitizeMaterialName(mtlPath.stem().string());
        if(fallback.name.empty()){
            fallback.name = "ImportedMaterial";
        }
        outMaterials.push_back(fallback);
        if(outError){
            *outError = "No 'newmtl' definitions found. Using generated default material.";
        }
    }

    return true;
}

std::shared_ptr<ConstructedMaterial> BuildConstructedMaterial(const MtlMaterialDefinition& def,
                                                              const std::string& sourceAssetRef,
                                                              std::string* outError){
    auto material = ConstructedMaterial::CreateWithPBRShader();
    if(!material){
        if(outError){
            *outError = "Failed to allocate ConstructedMaterial.";
        }
        return nullptr;
    }

    material->setSourceAssetRef(sourceAssetRef);
    material->setSourceMaterialName(def.name);
    material->setCastsShadows(true);
    material->setReceivesShadows(true);

    const float metallic = derivePbrMetallicFromMtl(def);
    const float roughness = derivePbrRoughnessFromMtl(def);
    const Math3D::Vec4 baseColor(def.diffuse.x, def.diffuse.y, def.diffuse.z, clamp01(def.dissolve));
    const float emissiveStrength = Math3D::Max(1.0f, Math3D::Max(def.emissive.x, Math3D::Max(def.emissive.y, def.emissive.z)));

    material->addVec4Field("base_color", "Base Color", "u_baseColor", baseColor, "u_color");
    material->addFloatField("metallic", "Metallic", "u_metallic", metallic);
    material->addFloatField("roughness", "Roughness", "u_roughness", roughness);
    material->addVec3Field("emissive_color", "Emissive Color", "u_emissiveColor", def.emissive);
    material->addFloatField("emissive_strength", "Emissive Strength", "u_emissiveStrength", emissiveStrength);
    material->addFloatField("normal_scale", "Normal Scale", "u_normalScale", 1.0f);
    material->addFloatField("alpha_cutoff", "Alpha Cutoff", "u_alphaCutoff", 0.5f);
    material->addBoolField("use_alpha_clip", "Use Alpha Clip", "u_useAlphaClip", def.dissolve < 0.999f);
    material->addBoolField("use_env_map", "Use Env Map", "u_useEnvMap", true);
    material->addFloatField("env_strength", "Env Strength", "u_envStrength", 1.0f);
    material->addVec2Field("uv_scale", "UV Scale", "u_uvScale", Math3D::Vec2(1.0f, 1.0f));
    material->addVec2Field("uv_offset", "UV Offset", "u_uvOffset", Math3D::Vec2(0.0f, 0.0f));
    material->addTexture2DField("base_color_tex", "Base Color Tex", "u_baseColorTex", def.diffuseMapRef, 0, "u_useBaseColorTex");
    material->addTexture2DField("normal_tex", "Normal Tex", "u_normalTex", def.normalMapRef, 2, "u_useNormalTex");
    material->addTexture2DField("emissive_tex", "Emissive Tex", "u_emissiveTex", def.emissiveMapRef, 3, "u_useEmissiveTex");

    material->markFieldsDirty();
    material->applyAllFields();
    return material;
}

bool BuildMaterialAssetData(const MtlMaterialDefinition& def,
                            MaterialAssetData& outData,
                            std::string* outError){
    (void)outError;
    outData = MaterialAssetData{};
    outData.type = MaterialAssetType::PBR;
    outData.color = Math3D::Vec4(def.diffuse.x, def.diffuse.y, def.diffuse.z, clamp01(def.dissolve));
    outData.metallic = derivePbrMetallicFromMtl(def);
    outData.roughness = derivePbrRoughnessFromMtl(def);
    outData.normalScale = 1.0f;
    outData.heightScale = 0.02f;
    outData.emissiveColor = def.emissive;
    outData.emissiveStrength = Math3D::Max(1.0f, Math3D::Max(def.emissive.x, Math3D::Max(def.emissive.y, def.emissive.z)));
    outData.occlusionStrength = 1.0f;
    outData.envStrength = 1.0f;
    outData.useEnvMap = 1;
    outData.uvScale = Math3D::Vec2(1.0f, 1.0f);
    outData.uvOffset = Math3D::Vec2(0.0f, 0.0f);
    outData.alphaCutoff = 0.5f;
    outData.useAlphaClip = (def.dissolve < 0.999f) ? 1 : 0;
    outData.baseColorTexRef = def.diffuseMapRef;
    outData.normalTexRef = def.normalMapRef;
    outData.emissiveTexRef = def.emissiveMapRef;
    outData.castsShadows = true;
    outData.receivesShadows = true;
    return true;
}

} // namespace MtlMaterialImporter
