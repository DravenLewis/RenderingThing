/**
 * @file src/Assets/Descriptors/EffectAsset.cpp
 * @brief Implementation for EffectAsset.
 */

#include "Assets/Descriptors/EffectAsset.h"

#include "Assets/Core/AssetDescriptorUtils.h"
#include "Foundation/Util/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <cstring>
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

    bool isEffectAssetPathInternal(const std::filesystem::path& path){
        return StringUtils::EndsWith(toLowerCopy(path.generic_string()), ".effect.asset");
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
        const std::string lower = toLowerCopy(trimCopy(value));
        if(lower == "1" || lower == "true" || lower == "yes" || lower == "on"){
            return true;
        }
        if(lower == "0" || lower == "false" || lower == "no" || lower == "off"){
            return false;
        }
        return fallback;
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

    std::string vec2ToString(const Math3D::Vec2& value){
        return StringUtils::Format("%.6f,%.6f", value.x, value.y);
    }

    std::string vec3ToString(const Math3D::Vec3& value){
        return StringUtils::Format("%.6f,%.6f,%.6f", value.x, value.y, value.z);
    }

    std::string vec4ToString(const Math3D::Vec4& value){
        return StringUtils::Format("%.6f,%.6f,%.6f,%.6f", value.x, value.y, value.z, value.w);
    }

    bool tryParseIndexedKey(const std::string& key,
                            const char* prefix,
                            size_t& outIndex,
                            std::string& outField){
        outIndex = 0;
        outField.clear();
        if(!prefix || !StringUtils::BeginsWith(key, prefix)){
            return false;
        }

        size_t cursor = std::strlen(prefix);
        const size_t digitsStart = cursor;
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

    std::string effectNameFallbackFromPath(const std::filesystem::path& path){
        std::string name = path.filename().string();
        const std::string lower = toLowerCopy(name);
        static const std::string kSuffix = ".effect.asset";
        if(StringUtils::EndsWith(lower, kSuffix) && name.size() > kSuffix.size()){
            name = name.substr(0, name.size() - kSuffix.size());
        }else{
            name = path.stem().string();
        }
        return SanitizeEffectDisplayName(name);
    }

    void finalizePropertyDefaults(EffectPropertyData& property, size_t index){
        if(property.key.empty()){
            if(!property.uniformName.empty()){
                property.key = property.uniformName;
            }else{
                property.key = "property" + std::to_string(index);
            }
        }
        if(property.displayName.empty()){
            property.displayName = SanitizeEffectDisplayName(!property.key.empty() ? property.key : property.uniformName);
        }
        property.textureSlot = std::max(property.textureSlot, 0);
    }

    void finalizeAssetDefaults(EffectAssetData& outData){
        for(size_t i = 0; i < outData.inputs.size(); ++i){
            outData.inputs[i].textureSlot = std::max(outData.inputs[i].textureSlot, 0);
        }
        for(size_t i = 0; i < outData.properties.size(); ++i){
            finalizePropertyDefaults(outData.properties[i], i);
        }
    }

    bool parseEffectAssetText(const std::string& text, EffectAssetData& outData){
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

            size_t indexed = 0;
            std::string field;
            if(key == "name"){
                outData.name = value;
            }else if(key == "vertex" || key == "vert" || key == "vertex_shader"){
                outData.vertexAssetRef = value;
            }else if(key == "fragment" || key == "frag" || key == "fragment_shader"){
                outData.fragmentAssetRef = value;
            }else if(key == "input_count"){
                outData.inputs.resize(static_cast<size_t>(std::max(parseInt(value, static_cast<int>(outData.inputs.size())), 0)));
            }else if(key == "property_count"){
                outData.properties.resize(static_cast<size_t>(std::max(parseInt(value, static_cast<int>(outData.properties.size())), 0)));
            }else if(key == "required_count"){
                outData.requiredEffects.resize(static_cast<size_t>(std::max(parseInt(value, static_cast<int>(outData.requiredEffects.size())), 0)));
            }else if(tryParseIndexedKey(key, "input", indexed, field)){
                if(outData.inputs.size() <= indexed){
                    outData.inputs.resize(indexed + 1);
                }

                EffectInputBindingData& input = outData.inputs[indexed];
                if(field == "uniform" || field == "uniform_name"){
                    input.uniformName = value;
                }else if(field == "source"){
                    input.source = EffectAssetIO::InputSourceFromString(value);
                }else if(field == "slot" || field == "texture_slot"){
                    input.textureSlot = std::max(parseInt(value, input.textureSlot), 0);
                }
            }else if(tryParseIndexedKey(key, "property", indexed, field)){
                if(outData.properties.size() <= indexed){
                    outData.properties.resize(indexed + 1);
                }

                EffectPropertyData& property = outData.properties[indexed];
                if(field == "key"){
                    property.key = value;
                }else if(field == "display_name" || field == "name" || field == "label"){
                    property.displayName = value;
                }else if(field == "uniform" || field == "uniform_name"){
                    property.uniformName = value;
                }else if(field == "mirror_uniform" || field == "mirror_uniform_name"){
                    property.mirrorUniformName = value;
                }else if(field == "presence_uniform" || field == "presence_uniform_name"){
                    property.presenceUniformName = value;
                }else if(field == "type"){
                    property.type = EffectAssetIO::PropertyTypeFromString(value);
                }else if(field == "float" || field == "default_float"){
                    property.floatValue = parseFloat(value, property.floatValue);
                }else if(field == "int" || field == "default_int"){
                    property.intValue = parseInt(value, property.intValue);
                }else if(field == "bool" || field == "default_bool"){
                    property.boolValue = parseBool(value, property.boolValue);
                }else if(field == "vec2" || field == "default_vec2"){
                    property.vec2Value = parseVec2(value, property.vec2Value);
                }else if(field == "vec3" || field == "default_vec3"){
                    property.vec3Value = parseVec3(value, property.vec3Value);
                }else if(field == "vec4" || field == "default_vec4"){
                    property.vec4Value = parseVec4(value, property.vec4Value);
                }else if(field == "texture" || field == "texture_ref" || field == "default_texture"){
                    property.textureAssetRef = value;
                }else if(field == "slot" || field == "texture_slot"){
                    property.textureSlot = std::max(parseInt(value, property.textureSlot), 0);
                }
            }else if(tryParseIndexedKey(key, "required", indexed, field)){
                if(field == "effect"){
                    if(outData.requiredEffects.size() <= indexed){
                        outData.requiredEffects.resize(indexed + 1);
                    }
                    outData.requiredEffects[indexed] = value;
                }
            }
        }

        finalizeAssetDefaults(outData);
        return true;
    }

    std::string buildEffectAssetText(const EffectAssetData& data, const std::string& fallbackName){
        std::string text;
        text += StringUtils::Format("name=%s\n", (data.name.empty() ? fallbackName : data.name).c_str());
        text += StringUtils::Format("vertex=%s\n", data.vertexAssetRef.c_str());
        text += StringUtils::Format("fragment=%s\n", data.fragmentAssetRef.c_str());
        text += StringUtils::Format("input_count=%d\n", static_cast<int>(data.inputs.size()));
        for(size_t i = 0; i < data.inputs.size(); ++i){
            const EffectInputBindingData& input = data.inputs[i];
            text += StringUtils::Format("input%d_uniform=%s\n", static_cast<int>(i), input.uniformName.c_str());
            text += StringUtils::Format("input%d_source=%s\n", static_cast<int>(i), EffectAssetIO::InputSourceToString(input.source));
            text += StringUtils::Format("input%d_slot=%d\n", static_cast<int>(i), std::max(input.textureSlot, 0));
        }
        text += StringUtils::Format("property_count=%d\n", static_cast<int>(data.properties.size()));
        for(size_t i = 0; i < data.properties.size(); ++i){
            const EffectPropertyData& property = data.properties[i];
            text += StringUtils::Format("property%d_key=%s\n", static_cast<int>(i), property.key.c_str());
            text += StringUtils::Format("property%d_display_name=%s\n", static_cast<int>(i), property.displayName.c_str());
            text += StringUtils::Format("property%d_uniform=%s\n", static_cast<int>(i), property.uniformName.c_str());
            text += StringUtils::Format("property%d_mirror_uniform=%s\n", static_cast<int>(i), property.mirrorUniformName.c_str());
            text += StringUtils::Format("property%d_presence_uniform=%s\n", static_cast<int>(i), property.presenceUniformName.c_str());
            text += StringUtils::Format("property%d_type=%s\n", static_cast<int>(i), EffectAssetIO::PropertyTypeToString(property.type));
            text += StringUtils::Format("property%d_float=%.6f\n", static_cast<int>(i), property.floatValue);
            text += StringUtils::Format("property%d_int=%d\n", static_cast<int>(i), property.intValue);
            text += StringUtils::Format("property%d_bool=%d\n", static_cast<int>(i), property.boolValue ? 1 : 0);
            text += StringUtils::Format("property%d_vec2=%s\n", static_cast<int>(i), vec2ToString(property.vec2Value).c_str());
            text += StringUtils::Format("property%d_vec3=%s\n", static_cast<int>(i), vec3ToString(property.vec3Value).c_str());
            text += StringUtils::Format("property%d_vec4=%s\n", static_cast<int>(i), vec4ToString(property.vec4Value).c_str());
            text += StringUtils::Format("property%d_texture=%s\n", static_cast<int>(i), property.textureAssetRef.c_str());
            text += StringUtils::Format("property%d_slot=%d\n", static_cast<int>(i), std::max(property.textureSlot, 0));
        }
        text += StringUtils::Format("required_count=%d\n", static_cast<int>(data.requiredEffects.size()));
        for(size_t i = 0; i < data.requiredEffects.size(); ++i){
            text += StringUtils::Format("required%d_effect=%s\n", static_cast<int>(i), data.requiredEffects[i].c_str());
        }
        return text;
    }
}

std::string SanitizeEffectDisplayName(const std::string& rawName){
    std::string value = rawName;
    if(StringUtils::BeginsWith(value, "u_") || StringUtils::BeginsWith(value, "g_")){
        value = value.substr(2);
    }

    std::string normalized;
    normalized.reserve(value.size());
    for(char c : value){
        if(c == '_' || c == '-' || c == '/' || c == '\\' || c == '.'){
            normalized.push_back(' ');
        }else{
            normalized.push_back(c);
        }
    }

    std::string spaced;
    spaced.reserve(normalized.size() * 2);
    for(size_t i = 0; i < normalized.size(); ++i){
        const unsigned char current = static_cast<unsigned char>(normalized[i]);
        const unsigned char previous = (i > 0) ? static_cast<unsigned char>(normalized[i - 1]) : 0;
        const unsigned char next = (i + 1 < normalized.size()) ? static_cast<unsigned char>(normalized[i + 1]) : 0;
        if(i > 0 && normalized[i] != ' ' && normalized[i - 1] != ' '){
            const bool prevLower = std::islower(previous) != 0;
            const bool prevUpper = std::isupper(previous) != 0;
            const bool prevDigit = std::isdigit(previous) != 0;
            const bool currUpper = std::isupper(current) != 0;
            const bool currDigit = std::isdigit(current) != 0;
            const bool nextLower = std::islower(next) != 0;
            if((prevLower && currUpper) ||
               (prevUpper && currUpper && nextLower) ||
               (prevDigit && !currDigit) ||
               (!prevDigit && currDigit)){
                spaced.push_back(' ');
            }
        }
        spaced.push_back(static_cast<char>(current));
    }

    std::string collapsed;
    collapsed.reserve(spaced.size());
    bool lastWasSpace = true;
    for(char c : spaced){
        if(c == ' '){
            if(!lastWasSpace){
                collapsed.push_back(' ');
                lastWasSpace = true;
            }
            continue;
        }
        collapsed.push_back(c);
        lastWasSpace = false;
    }
    if(!collapsed.empty() && collapsed.back() == ' '){
        collapsed.pop_back();
    }
    if(collapsed.empty()){
        return "Property";
    }

    std::string out;
    size_t cursor = 0;
    while(cursor < collapsed.size()){
        size_t end = collapsed.find(' ', cursor);
        if(end == std::string::npos){
            end = collapsed.size();
        }
        std::string token = collapsed.substr(cursor, end - cursor);
        if(!token.empty()){
            bool hasUpper = false;
            bool hasLower = false;
            for(char ch : token){
                const unsigned char uch = static_cast<unsigned char>(ch);
                hasUpper = hasUpper || (std::isupper(uch) != 0);
                hasLower = hasLower || (std::islower(uch) != 0);
            }
            if(hasLower || !hasUpper){
                token[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(token[0])));
                for(size_t i = 1; i < token.size(); ++i){
                    token[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(token[i])));
                }
            }
            if(!out.empty()){
                out.push_back(' ');
            }
            out += token;
        }
        cursor = end + 1;
    }
    return out.empty() ? std::string("Property") : out;
}

namespace EffectAssetIO {

bool IsEffectAssetPath(const std::filesystem::path& path){
    return isEffectAssetPathInternal(path);
}

const char* PropertyTypeToString(EffectPropertyType type){
    switch(type){
        case EffectPropertyType::Int: return "int";
        case EffectPropertyType::Bool: return "bool";
        case EffectPropertyType::Vec2: return "vec2";
        case EffectPropertyType::Vec3: return "vec3";
        case EffectPropertyType::Vec4: return "vec4";
        case EffectPropertyType::Texture2D: return "texture2d";
        case EffectPropertyType::Float:
        default:
            return "float";
    }
}

EffectPropertyType PropertyTypeFromString(const std::string& value){
    const std::string lower = toLowerCopy(trimCopy(value));
    if(lower == "int"){
        return EffectPropertyType::Int;
    }
    if(lower == "bool" || lower == "boolean"){
        return EffectPropertyType::Bool;
    }
    if(lower == "vec2" || lower == "float2"){
        return EffectPropertyType::Vec2;
    }
    if(lower == "vec3" || lower == "float3"){
        return EffectPropertyType::Vec3;
    }
    if(lower == "vec4" || lower == "float4" || lower == "color"){
        return EffectPropertyType::Vec4;
    }
    if(lower == "texture" || lower == "texture2d" || lower == "sampler2d"){
        return EffectPropertyType::Texture2D;
    }
    return EffectPropertyType::Float;
}

const char* InputSourceToString(EffectInputSource source){
    switch(source){
        case EffectInputSource::Depth: return "depth";
        case EffectInputSource::InputTexelSize: return "input_texel_size";
        case EffectInputSource::OutputTexelSize: return "output_texel_size";
        case EffectInputSource::ScreenColor:
        default:
            return "screen_color";
    }
}

EffectInputSource InputSourceFromString(const std::string& value){
    const std::string lower = toLowerCopy(trimCopy(value));
    if(lower == "depth" || lower == "screen_depth"){
        return EffectInputSource::Depth;
    }
    if(lower == "input_texel_size" || lower == "screen_texel_size"){
        return EffectInputSource::InputTexelSize;
    }
    if(lower == "output_texel_size" || lower == "target_texel_size"){
        return EffectInputSource::OutputTexelSize;
    }
    return EffectInputSource::ScreenColor;
}

bool LoadFromAbsolutePath(const std::filesystem::path& path, EffectAssetData& outData, std::string* outError){
    if(!IsEffectAssetPath(path)){
        if(outError){
            *outError = "Effect asset files must use .effect.asset extension.";
        }
        return false;
    }

    bool isDirectory = false;
    if(!pathExists(path, &isDirectory) || isDirectory){
        if(outError){
            *outError = "Effect asset does not exist: " + path.generic_string();
        }
        return false;
    }

    outData = EffectAssetData{};
    std::string text;
    if(!readTextPath(path, text, outError)){
        return false;
    }

    parseEffectAssetText(text, outData);
    if(outData.name.empty()){
        outData.name = effectNameFallbackFromPath(path);
    }
    return true;
}

bool LoadFromAssetRef(const std::string& assetRef, EffectAssetData& outData, std::string* outError){
    outData = EffectAssetData{};

    std::string text;
    if(!readTextRefOrPath(assetRef, text, outError)){
        return false;
    }

    parseEffectAssetText(text, outData);
    if(outData.name.empty()){
        std::filesystem::path resolvedPath;
        if(toAbsolutePathFromAssetRef(assetRef, resolvedPath)){
            outData.name = effectNameFallbackFromPath(resolvedPath);
        }else{
            outData.name = effectNameFallbackFromPath(std::filesystem::path(assetRef));
        }
    }
    return true;
}

bool SaveToAbsolutePath(const std::filesystem::path& path, const EffectAssetData& data, std::string* outError){
    if(!IsEffectAssetPath(path)){
        if(outError){
            *outError = "Effect asset files must use .effect.asset extension.";
        }
        return false;
    }

    EffectAssetData normalized = data;
    finalizeAssetDefaults(normalized);
    return writeTextPath(path, buildEffectAssetText(normalized, effectNameFallbackFromPath(path)), outError);
}

bool SaveToAssetRef(const std::string& assetRef, const EffectAssetData& data, std::string* outError){
    EffectAssetData normalized = data;
    finalizeAssetDefaults(normalized);

    std::filesystem::path resolvedPath;
    const std::string fallbackName = toAbsolutePathFromAssetRef(assetRef, resolvedPath)
        ? effectNameFallbackFromPath(resolvedPath)
        : effectNameFallbackFromPath(std::filesystem::path(assetRef));
    return writeTextAsset(assetRef, buildEffectAssetText(normalized, fallbackName), outError);
}

} // namespace EffectAssetIO
