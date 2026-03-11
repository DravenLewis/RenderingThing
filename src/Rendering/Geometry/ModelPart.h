/**
 * @file src/Rendering/Geometry/ModelPart.h
 * @brief Declarations for ModelPart.
 */

#ifndef MODELPART_H
#define MODELPART_H

#include <memory>
#include <vector>

#include "Rendering/Geometry/Mesh.h"
#include "Rendering/Materials/Material.h"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Geometry/Drawable.h"

/// @brief Holds data for ModelPart.
struct ModelPart : public IDrawable{
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    Math3D::Transform localTransform = Math3D::Transform();
    bool visible = true;
    // Model parts are hidden in the ECS tree by default to keep entity trees compact.
    bool hideInEditorTree = true;
    
    /**
     * @brief Draws this object.
     * @param parent Value for parent.
     * @param view Value for view.
     * @param proj Value for proj.
     */
    void draw(const Math3D::Mat4& parent, const Math3D::Mat4& view, const Math3D::Mat4& proj);
};

/// @brief Holds data for ModelPartFactory.
struct ModelPartFactory{
    private:
        std::shared_ptr<Mesh> meshInstancePtr;
        std::shared_ptr<Material> materialInstancePtr;
        std::vector<Vertex> vertexCache;
        std::vector<uint32_t> faceCache;
    public:
        /**
         * @brief Constructs a new ModelPartFactory instance.
         */
        ModelPartFactory() = default;
        /**
         * @brief Creates a new object.
         * @param material Value for material.
         * @return Result of this operation.
         */
        static ModelPartFactory Create(std::shared_ptr<Material> material);
        /**
         * @brief Adds vertex.
         * @param vtx Value for vtx.
         * @param index Identifier or index value.
         * @return Reference to the resulting value.
         */
        ModelPartFactory& addVertex(Vertex vtx, int* index = nullptr);
        /**
         * @brief Adds a face definition to the model part.
         * @param vtx1 Value for vtx 1.
         * @param vtx2 Value for vtx 2.
         * @param vtx3 Value for vtx 3.
         * @param vtx4 Value for vtx 4.
         * @return Reference to the resulting value.
         */
        ModelPartFactory& defineFace(int vtx1, int vtx2, int vtx3, int vtx4 = -1);
        /**
         * @brief Builds a model part from accumulated data.
         * @return Pointer to the resulting object.
         */
        std::shared_ptr<ModelPart> assemble();
};

#endif // MODELPART_H
