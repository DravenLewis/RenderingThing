#ifndef MODELPART_H
#define MODELPART_H

#include <memory>
#include <vector>

#include "Mesh.h"
#include "Material.h"
#include "Math.h"
#include "Drawable.h"

struct ModelPart : public IDrawable{
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    Math3D::Transform localTransform = Math3D::Transform();
    
    void draw(const Math3D::Mat4& parent, const Math3D::Mat4& view, const Math3D::Mat4& proj);
};

struct ModelPartFactory{
    private:
        std::shared_ptr<Mesh> meshInstancePtr;
        std::shared_ptr<Material> materialInstancePtr;
        std::vector<Vertex> vertexCache;
        std::vector<uint32_t> faceCache;
    public:
        ModelPartFactory() = default;
        static ModelPartFactory Create(std::shared_ptr<Material> material);
        ModelPartFactory& addVertex(Vertex vtx, int* index = nullptr);
        ModelPartFactory& defineFace(int vtx1, int vtx2, int vtx3, int vtx4 = -1);
        std::shared_ptr<ModelPart> assemble();
};

#endif // MODELPART_H