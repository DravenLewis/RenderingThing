/**
 * @file src/Assets/Descriptors/EnvironmentAsset.cpp
 * @brief Implementation for EnvironmentAsset.
 */

#include "Assets/Descriptors/EnvironmentAsset.h"

#include "Assets/Core/AssetDescriptorUtils.h"
#include "Foundation/Util/StringUtils.h"

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

    bool isEnvironmentAssetPathInternal(const std::filesystem::path& path){
        const std::string lower = toLowerCopy(path.generic_string());
        return StringUtils::EndsWith(lower, ".environment.asset");
    }

    bool parseBool(const std::string& value, bool fallback){
        const std::string lower = toLowerCopy(trimCopy(value));
        if(lower == "1" || lower == "true" || lower == "yes" || lower == "on"){
            return true;
        }
        if(lower == "0" || lower == "false" || lower == "no" || lower == "off"){
            return false;
        }
        return fallback;
    }

    float parseFloat(const std::string& value, float fallback){
        try{
            return std::stof(trimCopy(value));
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

    std::string vec3ToString(const Math3D::Vec3& value){
        return StringUtils::Format("%.6f,%.6f,%.6f", value.x, value.y, value.z);
    }

    std::string vec4ToString(const Math3D::Vec4& value){
        return StringUtils::Format("%.6f,%.6f,%.6f,%.6f", value.x, value.y, value.z, value.w);
    }

    void sanitize(EnvironmentAssetData& data){
        data.settings.fogStart = Math3D::Max(0.0f, data.settings.fogStart);
        data.settings.fogStop = Math3D::Max(data.settings.fogStart, data.settings.fogStop);
        data.settings.fogEnd = Math3D::Max(data.settings.fogStop, data.settings.fogEnd);
        data.settings.ambientIntensity = Math3D::Clamp(data.settings.ambientIntensity, 0.0f, 32.0f);
        data.settings.rayleighStrength = Math3D::Max(0.0f, data.settings.rayleighStrength);
        data.settings.mieStrength = Math3D::Max(0.0f, data.settings.mieStrength);
        data.settings.mieAnisotropy = Math3D::Clamp(data.settings.mieAnisotropy, 0.0f, 0.99f);
        if(data.settings.sunDirection.length() <= Math3D::EPSILON){
            data.settings.sunDirection = Math3D::Vec3(0.0f, -1.0f, 0.0f);
        }else{
            data.settings.sunDirection = data.settings.sunDirection.normalize();
        }
    }

    bool parseEnvironmentAssetText(const std::string& text, EnvironmentAssetData& outData){
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
            }else if(key == "skybox" || key == "skybox_asset" || key == "skyboxassetref"){
                outData.skyboxAssetRef = value;
            }else if(key == "fog_enabled" || key == "fogenabled"){
                outData.settings.fogEnabled = parseBool(value, outData.settings.fogEnabled);
            }else if(key == "fog_color" || key == "fogcolor"){
                outData.settings.fogColor = parseVec4(value, outData.settings.fogColor);
            }else if(key == "fog_start" || key == "fogstart"){
                outData.settings.fogStart = parseFloat(value, outData.settings.fogStart);
            }else if(key == "fog_stop" || key == "fogstop"){
                outData.settings.fogStop = parseFloat(value, outData.settings.fogStop);
            }else if(key == "fog_end" || key == "fogend"){
                outData.settings.fogEnd = parseFloat(value, outData.settings.fogEnd);
            }else if(key == "ambient_color" || key == "ambientcolor"){
                outData.settings.ambientColor = parseVec4(value, outData.settings.ambientColor);
            }else if(key == "ambient_intensity" || key == "ambientintensity"){
                outData.settings.ambientIntensity = parseFloat(value, outData.settings.ambientIntensity);
            }else if(key == "use_procedural_sky" || key == "useproceduralsky"){
                outData.settings.useProceduralSky = parseBool(value, outData.settings.useProceduralSky);
            }else if(key == "sun_direction" || key == "sundirection"){
                outData.settings.sunDirection = parseVec3(value, outData.settings.sunDirection);
            }else if(key == "rayleigh_strength" || key == "rayleigh"){
                outData.settings.rayleighStrength = parseFloat(value, outData.settings.rayleighStrength);
            }else if(key == "mie_strength" || key == "mie"){
                outData.settings.mieStrength = parseFloat(value, outData.settings.mieStrength);
            }else if(key == "mie_anisotropy" || key == "mieg"){
                outData.settings.mieAnisotropy = parseFloat(value, outData.settings.mieAnisotropy);
            }
        }
        sanitize(outData);
        return true;
    }

    std::string buildEnvironmentAssetText(const std::filesystem::path& sourcePath,
                                          const std::string& sourceRef,
                                          const EnvironmentAssetData& data){
        const std::string resolvedName = !data.name.empty()
            ? data.name
            : (!sourcePath.empty() ? sourcePath.filename().string() : std::filesystem::path(sourceRef).filename().string());
        std::string text;
        text += StringUtils::Format("name=%s\n", resolvedName.c_str());
        text += StringUtils::Format("skybox=%s\n", data.skyboxAssetRef.c_str());
        text += StringUtils::Format("fog_enabled=%s\n", data.settings.fogEnabled ? "true" : "false");
        text += StringUtils::Format("fog_color=%s\n", vec4ToString(data.settings.fogColor).c_str());
        text += StringUtils::Format("fog_start=%.6f\n", data.settings.fogStart);
        text += StringUtils::Format("fog_stop=%.6f\n", data.settings.fogStop);
        text += StringUtils::Format("fog_end=%.6f\n", data.settings.fogEnd);
        text += StringUtils::Format("ambient_color=%s\n", vec4ToString(data.settings.ambientColor).c_str());
        text += StringUtils::Format("ambient_intensity=%.6f\n", data.settings.ambientIntensity);
        text += StringUtils::Format("use_procedural_sky=%s\n", data.settings.useProceduralSky ? "true" : "false");
        text += StringUtils::Format("sun_direction=%s\n", vec3ToString(data.settings.sunDirection).c_str());
        text += StringUtils::Format("rayleigh_strength=%.6f\n", data.settings.rayleighStrength);
        text += StringUtils::Format("mie_strength=%.6f\n", data.settings.mieStrength);
        text += StringUtils::Format("mie_anisotropy=%.6f\n", data.settings.mieAnisotropy);
        return text;
    }
}

namespace EnvironmentAssetIO {

bool IsEnvironmentAssetPath(const std::filesystem::path& path){
    return isEnvironmentAssetPathInternal(path);
}

bool LoadFromAbsolutePath(const std::filesystem::path& path, EnvironmentAssetData& outData, std::string* outError){
    if(!IsEnvironmentAssetPath(path)){
        if(outError){
            *outError = "Environment asset files must use .environment.asset extension.";
        }
        return false;
    }

    bool isDirectory = false;
    if(!pathExists(path, &isDirectory) || isDirectory){
        if(outError){
            *outError = "Environment asset does not exist: " + path.generic_string();
        }
        return false;
    }

    outData = EnvironmentAssetData{};
    std::string text;
    if(!readTextPath(path, text, outError)){
        return false;
    }

    parseEnvironmentAssetText(text, outData);
    if(outData.name.empty()){
        outData.name = path.filename().string();
    }
    sanitize(outData);
    return true;
}

bool LoadFromAssetRef(const std::string& assetRef, EnvironmentAssetData& outData, std::string* outError){
    outData = EnvironmentAssetData{};
    std::string text;
    if(!readTextRefOrPath(assetRef, text, outError)){
        return false;
    }

    parseEnvironmentAssetText(text, outData);
    if(outData.name.empty()){
        std::filesystem::path resolvedPath;
        if(toAbsolutePathFromAssetRef(assetRef, resolvedPath)){
            outData.name = resolvedPath.filename().string();
        }else{
            outData.name = std::filesystem::path(assetRef).filename().string();
        }
    }
    sanitize(outData);
    return true;
}

bool SaveToAbsolutePath(const std::filesystem::path& path, const EnvironmentAssetData& data, std::string* outError){
    if(!IsEnvironmentAssetPath(path)){
        if(outError){
            *outError = "Environment asset files must use .environment.asset extension.";
        }
        return false;
    }

    EnvironmentAssetData sanitized = data;
    sanitize(sanitized);
    const std::string text = buildEnvironmentAssetText(path, std::string(), sanitized);
    return writeTextPath(path, text, outError);
}

bool SaveToAssetRef(const std::string& assetRef, const EnvironmentAssetData& data, std::string* outError){
    EnvironmentAssetData sanitized = data;
    sanitize(sanitized);
    const std::string text = buildEnvironmentAssetText(std::filesystem::path(), assetRef, sanitized);
    return writeTextAsset(assetRef, text, outError);
}

} // namespace EnvironmentAssetIO
