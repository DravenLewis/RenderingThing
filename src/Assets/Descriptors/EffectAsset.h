/**
 * @file src/Assets/Descriptors/EffectAsset.h
 * @brief Declarations for EffectAsset.
 */

#ifndef EFFECT_ASSET_H
#define EFFECT_ASSET_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Foundation/Math/Math3D.h"

class Texture;

/// @brief Enumerates editable uniform property types for effect assets.
enum class EffectPropertyType {
    Float = 0,
    Int,
    Bool,
    Vec2,
    Vec3,
    Vec4,
    Texture2D
};

/// @brief Enumerates built-in runtime bindings for post effect inputs.
enum class EffectInputSource {
    ScreenColor = 0,
    Depth,
    InputTexelSize,
    OutputTexelSize
};

/// @brief Holds a runtime-provided uniform binding for an effect asset.
struct EffectInputBindingData {
    std::string uniformName;
    EffectInputSource source = EffectInputSource::ScreenColor;
    int textureSlot = 0;
};

/// @brief Holds an editable property definition/value for an effect asset.
struct EffectPropertyData {
    std::string key;
    std::string displayName;
    std::string uniformName;
    std::string mirrorUniformName;
    std::string presenceUniformName;
    EffectPropertyType type = EffectPropertyType::Float;

    float floatValue = 0.0f;
    int intValue = 0;
    bool boolValue = false;
    Math3D::Vec2 vec2Value = Math3D::Vec2(0.0f, 0.0f);
    Math3D::Vec3 vec3Value = Math3D::Vec3(0.0f, 0.0f, 0.0f);
    Math3D::Vec4 vec4Value = Math3D::Vec4(0.0f, 0.0f, 0.0f, 1.0f);

    std::string textureAssetRef;
    int textureSlot = 0;

    // Runtime-only cache used by loaded post effects.
    std::shared_ptr<Texture> texturePtr;
    std::string loadedTextureRef;
    std::uint64_t loadedTextureRevision = 0;
};

/// @brief Holds data for EffectAssetData.
struct EffectAssetData {
    std::string name;
    std::string vertexAssetRef;
    std::string fragmentAssetRef;
    std::vector<EffectInputBindingData> inputs;
    std::vector<EffectPropertyData> properties;
    std::vector<std::string> requiredEffects;

    /**
     * @brief Checks whether the descriptor contains a usable shader pair.
     * @return True when the effect can compile; otherwise false.
     */
    bool isComplete() const{
        return !vertexAssetRef.empty() && !fragmentAssetRef.empty();
    }
};

/**
 * @brief Builds an editor-friendly display name from a uniform/property identifier.
 * @param rawName Source uniform/property/token string.
 * @return Sanitized title-style display name.
 */
std::string SanitizeEffectDisplayName(const std::string& rawName);

namespace EffectAssetIO {
    bool IsEffectAssetPath(const std::filesystem::path& path);
    bool LoadFromAbsolutePath(const std::filesystem::path& path, EffectAssetData& outData, std::string* outError = nullptr);
    bool LoadFromAssetRef(const std::string& assetRef, EffectAssetData& outData, std::string* outError = nullptr);
    bool SaveToAbsolutePath(const std::filesystem::path& path, const EffectAssetData& data, std::string* outError = nullptr);
    bool SaveToAssetRef(const std::string& assetRef, const EffectAssetData& data, std::string* outError = nullptr);

    const char* PropertyTypeToString(EffectPropertyType type);
    EffectPropertyType PropertyTypeFromString(const std::string& value);
    const char* InputSourceToString(EffectInputSource source);
    EffectInputSource InputSourceFromString(const std::string& value);
}

using EffectDescriptorData = EffectAssetData;
namespace EffectDescriptorIO = EffectAssetIO;

#endif // EFFECT_ASSET_H
