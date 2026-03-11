/**
 * @file src/Assets/Importers/MtlMaterialImporter.h
 * @brief Declarations for MtlMaterialImporter.
 */

#ifndef MTL_MATERIAL_IMPORTER_H
#define MTL_MATERIAL_IMPORTER_H

#include "Foundation/Math/Math3D.h"
#include "Assets/Descriptors/MaterialAsset.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class ConstructedMaterial;

/// @brief Holds data for MtlMaterialDefinition.
struct MtlMaterialDefinition {
    std::string name;

    Math3D::Vec3 ambient = Math3D::Vec3(0.0f, 0.0f, 0.0f);   // Ka
    Math3D::Vec3 diffuse = Math3D::Vec3(1.0f, 1.0f, 1.0f);   // Kd
    Math3D::Vec3 specular = Math3D::Vec3(0.0f, 0.0f, 0.0f);  // Ks
    Math3D::Vec3 emissive = Math3D::Vec3(0.0f, 0.0f, 0.0f);  // Ke

    float shininess = 32.0f;  // Ns
    float dissolve = 1.0f;    // d / Tr
    int illumModel = 2;

    std::string diffuseMapRef;   // map_Kd
    std::string normalMapRef;    // bump / map_Bump
    std::string opacityMapRef;   // map_d
    std::string emissiveMapRef;  // map_Ke
};

namespace MtlMaterialImporter {
    /**
     * @brief Loads from absolute path.
     * @param mtlPath Filesystem path for mtl path.
     * @param outMaterials Output value for materials.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAbsolutePath(const std::filesystem::path& mtlPath,
                              std::vector<MtlMaterialDefinition>& outMaterials,
                              std::string* outError = nullptr);

    /**
     * @brief Builds constructed material.
     * @param def Value for def.
     * @param sourceAssetRef Reference to source asset.
     * @param outError Output value for error.
     * @return Pointer to the resulting object.
     */
    std::shared_ptr<ConstructedMaterial> BuildConstructedMaterial(const MtlMaterialDefinition& def,
                                                                  const std::string& sourceAssetRef = "",
                                                                  std::string* outError = nullptr);

    /**
     * @brief Builds material asset data.
     * @param def Value for def.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool BuildMaterialAssetData(const MtlMaterialDefinition& def,
                                MaterialAssetData& outData,
                                std::string* outError = nullptr);

    /**
     * @brief Sanitizes a material name for safe use.
     * @param value Value for value.
     * @return Resulting string value.
     */
    std::string SanitizeMaterialName(const std::string& value);
}

#endif // MTL_MATERIAL_IMPORTER_H
