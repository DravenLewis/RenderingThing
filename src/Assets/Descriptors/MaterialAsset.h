#ifndef MATERIAL_ASSET_H
#define MATERIAL_ASSET_H

#include <filesystem>
#include <memory>
#include <string>

#include "Foundation/Math/Color.h"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Materials/Material.h"

enum class MaterialAssetType {
    PBR = 0,
    Color,
    Image,
    LitColor,
    LitImage,
    FlatColor,
    FlatImage
};

// Descriptor wrappers around runtime materials (text metadata files, not Asset subclasses).
struct MaterialAssetData {
    std::string name;
    std::string linkParentRef;
    MaterialAssetType type = MaterialAssetType::PBR;
    std::string shaderAssetRef;

    Math3D::Vec4 color = Color::WHITE;
    Math3D::Vec2 uv = Math3D::Vec2(0.0f, 0.0f);
    std::string textureRef;

    float metallic = 0.0f;
    float roughness = 1.0f;
    float normalScale = 1.0f;
    float heightScale = 0.02f;
    Math3D::Vec3 emissiveColor = Math3D::Vec3(0.0f, 0.0f, 0.0f);
    float emissiveStrength = 1.0f;
    float occlusionStrength = 1.0f;
    float envStrength = 1.0f;
    int useEnvMap = 0;
    Math3D::Vec2 uvScale = Math3D::Vec2(1.0f, 1.0f);
    Math3D::Vec2 uvOffset = Math3D::Vec2(0.0f, 0.0f);
    float alphaCutoff = 0.5f;
    int useAlphaClip = 0;

    std::string baseColorTexRef;
    std::string roughnessTexRef;
    std::string metallicRoughnessTexRef;
    std::string normalTexRef;
    std::string heightTexRef;
    std::string emissiveTexRef;
    std::string occlusionTexRef;

    bool castsShadows = true;
    bool receivesShadows = true;
};

struct MaterialObjectData {
    std::string name;
    std::string materialAssetRef;
};

// Legacy compatibility name. Prefer `MaterialDescriptorIO` in new code.
namespace MaterialAssetIO {
    bool IsMaterialAssetPath(const std::filesystem::path& path);
    bool IsMaterialObjectPath(const std::filesystem::path& path);
    bool IsMaterialPath(const std::filesystem::path& path);
    const char* TypeToString(MaterialAssetType type);
    MaterialAssetType TypeFromString(const std::string& value);

    bool LoadFromAbsolutePath(const std::filesystem::path& path, MaterialAssetData& outData, std::string* outError = nullptr);
    bool LoadFromAssetRef(const std::string& assetRef, MaterialAssetData& outData, std::string* outError = nullptr);
    bool SaveToAbsolutePath(const std::filesystem::path& path, const MaterialAssetData& data, std::string* outError = nullptr);
    bool SaveToAssetRef(const std::string& assetRef, const MaterialAssetData& data, std::string* outError = nullptr);

    std::shared_ptr<Material> InstantiateMaterial(const MaterialAssetData& data, std::string* outError = nullptr);
    bool ResolveMaterialAssetRef(const std::string& materialOrAssetRef, std::string& outAssetRef, std::string* outError = nullptr);
    bool LoadMaterialObjectFromAbsolutePath(const std::filesystem::path& path, MaterialObjectData& outData, std::string* outError = nullptr);
    bool LoadMaterialObjectFromAssetRef(const std::string& assetRef, MaterialObjectData& outData, std::string* outError = nullptr);
    bool SaveMaterialObjectToAbsolutePath(const std::filesystem::path& path, const MaterialObjectData& data, std::string* outError = nullptr);
    bool SaveMaterialObjectToAssetRef(const std::string& assetRef, const MaterialObjectData& data, std::string* outError = nullptr);
    std::shared_ptr<Material> InstantiateMaterialFromRef(const std::string& materialOrAssetRef, std::string* outResolvedAssetRef = nullptr, std::string* outError = nullptr);
}

// Clearer naming for new code (kept as aliases for backward compatibility).
using MaterialDescriptorData = MaterialAssetData;
using MaterialLinkDescriptorData = MaterialObjectData;
namespace MaterialDescriptorIO = MaterialAssetIO;

#endif // MATERIAL_ASSET_H
