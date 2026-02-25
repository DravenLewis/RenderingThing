#ifndef MTL_MATERIAL_IMPORTER_H
#define MTL_MATERIAL_IMPORTER_H

#include "Math.h"
#include "MaterialAsset.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class ConstructedMaterial;

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
    bool LoadFromAbsolutePath(const std::filesystem::path& mtlPath,
                              std::vector<MtlMaterialDefinition>& outMaterials,
                              std::string* outError = nullptr);

    std::shared_ptr<ConstructedMaterial> BuildConstructedMaterial(const MtlMaterialDefinition& def,
                                                                  const std::string& sourceAssetRef = "",
                                                                  std::string* outError = nullptr);

    bool BuildMaterialAssetData(const MtlMaterialDefinition& def,
                                MaterialAssetData& outData,
                                std::string* outError = nullptr);

    std::string SanitizeMaterialName(const std::string& value);
}

#endif // MTL_MATERIAL_IMPORTER_H
