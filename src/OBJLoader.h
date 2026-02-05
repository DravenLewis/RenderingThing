#ifndef OBJ_LOADER_H
#define OBJ_LOADER_H

#include <memory>
#include <vector>
#include <string>

#include "Asset.h"
#include "Model.h"
#include "Material.h"

#include "MaterialDefaults.h"

class OBJLoader {
private:
    struct VertexIndex {
        int posIdx = -1;
        int texIdx = -1;
        int normIdx = -1;
    };

    struct OBJData {
        std::vector<Math3D::Vec3> positions;
        std::vector<Math3D::Vec3> normals;
        std::vector<Math3D::Vec2> texCoords;
        std::vector<std::vector<VertexIndex>> faces;  // Each face stores vertex indices
    };

    static OBJData ParseOBJData(const std::string& content);
    static std::vector<Vertex> ConstructVertices(const OBJData& data);
    
public:
    OBJLoader() = delete;

    // Load OBJ from an asset
    static std::shared_ptr<Model> LoadFromAsset(
        PAsset asset,
        PMaterial material = MaterialDefaults::LitColorMaterial::Create(Color::WHITE)
    );
};

#endif // OBJ_LOADER_H
