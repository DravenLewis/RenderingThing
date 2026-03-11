/**
 * @file src/Assets/Descriptors/MaterialAsset.h
 * @brief Declarations for MaterialAsset.
 */

#ifndef MATERIAL_ASSET_H
#define MATERIAL_ASSET_H

#include <filesystem>
#include <memory>
#include <string>

#include "Foundation/Math/Color.h"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Materials/Material.h"

/// @brief Enumerates values for MaterialAssetType.
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
/// @brief Holds data for MaterialAssetData.
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

/// @brief Holds data for MaterialObjectData.
struct MaterialObjectData {
    std::string name;
    std::string materialAssetRef;
};

// Legacy compatibility name. Prefer `MaterialDescriptorIO` in new code.
namespace MaterialAssetIO {
    /**
     * @brief Checks whether material asset path.
     * @param path Filesystem path for path.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool IsMaterialAssetPath(const std::filesystem::path& path);
    /**
     * @brief Checks whether material object path.
     * @param path Filesystem path for path.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool IsMaterialObjectPath(const std::filesystem::path& path);
    /**
     * @brief Checks whether material path.
     * @param path Filesystem path for path.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool IsMaterialPath(const std::filesystem::path& path);
    /**
     * @brief Converts material type to text.
     * @param type Mode or type selector.
     * @return Pointer to the resulting object.
     */
    const char* TypeToString(MaterialAssetType type);
    /**
     * @brief Converts material type text to enum.
     * @param value Value for value.
     * @return Result of this operation.
     */
    MaterialAssetType TypeFromString(const std::string& value);

    /**
     * @brief Loads from absolute path.
     * @param path Filesystem path for path.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAbsolutePath(const std::filesystem::path& path, MaterialAssetData& outData, std::string* outError = nullptr);
    /**
     * @brief Loads from asset ref.
     * @param assetRef Reference to asset.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAssetRef(const std::string& assetRef, MaterialAssetData& outData, std::string* outError = nullptr);
    /**
     * @brief Saves to absolute path.
     * @param path Filesystem path for path.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveToAbsolutePath(const std::filesystem::path& path, const MaterialAssetData& data, std::string* outError = nullptr);
    /**
     * @brief Saves to asset ref.
     * @param assetRef Reference to asset.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveToAssetRef(const std::string& assetRef, const MaterialAssetData& data, std::string* outError = nullptr);

    /**
     * @brief Instantiates a material object.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return Pointer to the resulting object.
     */
    std::shared_ptr<Material> InstantiateMaterial(const MaterialAssetData& data, std::string* outError = nullptr);
    /**
     * @brief Checks whether resolve material asset ref.
     * @param materialOrAssetRef Reference to material or asset.
     * @param outAssetRef Output value for asset.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool ResolveMaterialAssetRef(const std::string& materialOrAssetRef, std::string& outAssetRef, std::string* outError = nullptr);
    /**
     * @brief Loads material object from absolute path.
     * @param path Filesystem path for path.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadMaterialObjectFromAbsolutePath(const std::filesystem::path& path, MaterialObjectData& outData, std::string* outError = nullptr);
    /**
     * @brief Loads material object from asset ref.
     * @param assetRef Reference to asset.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadMaterialObjectFromAssetRef(const std::string& assetRef, MaterialObjectData& outData, std::string* outError = nullptr);
    /**
     * @brief Saves material object to absolute path.
     * @param path Filesystem path for path.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveMaterialObjectToAbsolutePath(const std::filesystem::path& path, const MaterialObjectData& data, std::string* outError = nullptr);
    /**
     * @brief Saves material object to asset ref.
     * @param assetRef Reference to asset.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveMaterialObjectToAssetRef(const std::string& assetRef, const MaterialObjectData& data, std::string* outError = nullptr);
    /**
     * @brief Instantiates a material from an asset reference.
     * @param materialOrAssetRef Reference to material or asset.
     * @param outResolvedAssetRef Output value for resolved asset.
     * @param outError Output value for error.
     * @return Pointer to the resulting object.
     */
    std::shared_ptr<Material> InstantiateMaterialFromRef(const std::string& materialOrAssetRef, std::string* outResolvedAssetRef = nullptr, std::string* outError = nullptr);
}

// Clearer naming for new code (kept as aliases for backward compatibility).
using MaterialDescriptorData = MaterialAssetData;
using MaterialLinkDescriptorData = MaterialObjectData;
namespace MaterialDescriptorIO = MaterialAssetIO;

#endif // MATERIAL_ASSET_H
