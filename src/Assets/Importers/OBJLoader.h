/**
 * @file src/Assets/Importers/OBJLoader.h
 * @brief Declarations for OBJLoader.
 */

#ifndef OBJ_LOADER_H
#define OBJ_LOADER_H

#include <memory>
#include <vector>
#include <string>

#include "Assets/Core/Asset.h"
#include "Rendering/Geometry/Model.h"
#include "Rendering/Materials/Material.h"

#include "Rendering/Materials/MaterialDefaults.h"

/// @brief Represents the OBJLoader type.
class OBJLoader {
private:
    /// @brief Holds data for VertexIndex.
    struct VertexIndex {
        int posIdx = -1;
        int texIdx = -1;
        int normIdx = -1;
    };

    /// @brief Holds data for FaceDefinition.
    struct FaceDefinition {
        std::vector<VertexIndex> vertices;
        std::string materialName;
    };

    /// @brief Holds data for OBJData.
    struct OBJData {
        std::vector<Math3D::Vec3> positions;
        std::vector<Math3D::Vec3> normals;
        std::vector<Math3D::Vec2> texCoords;
        std::vector<FaceDefinition> faces;
        std::vector<std::string> materialLibs;
    };

    /**
     * @brief Parses OBJ file data.
     * @param content Value for content.
     * @return Result of this operation.
     */
    static OBJData ParseOBJData(const std::string& content);
    /**
     * @brief Builds smooth normals.
     * @param data Value for data.
     * @return Result of this operation.
     */
    static std::vector<Math3D::Vec3> BuildSmoothNormals(const OBJData& data);
    
public:
    /**
     * @brief Constructs a new OBJLoader instance.
     */
    OBJLoader() = delete;

    // Load OBJ from an asset
    static std::shared_ptr<Model> LoadFromAsset(
        PAsset asset,
        PMaterial material = MaterialDefaults::LitColorMaterial::Create(Color::WHITE),
        bool forceSmoothNormals = false
    );
};

#endif // OBJ_LOADER_H
