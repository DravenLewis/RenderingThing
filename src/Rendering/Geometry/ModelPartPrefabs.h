/**
 * @file src/Rendering/Geometry/ModelPartPrefabs.h
 * @brief Declarations for ModelPartPrefabs.
 */


#ifndef MODELPART_PREFABS_H
#define MODELPART_PREFABS_H

#include <memory>
#include <algorithm>
#include <cmath>
#include <vector>

#include "Rendering/Geometry/ModelPart.h"
#include "Rendering/Materials/Material.h"
#include "Rendering/Materials/MaterialDefaults.h"
#include "Foundation/Math/Math3D.h"

/// @brief Holds data for ModelPartPrefabs.
struct ModelPartPrefabs{
    /**
     * @brief Creates plane.
     * @param width Dimension value.
     * @param height Dimension value.
     * @param mat Value for mat.
     * @param normal Value for normal.
     * @return Pointer to the resulting object.
     */
    static std::shared_ptr<ModelPart> MakePlane(float width, float height, PMaterial mat = MaterialDefaults::ColorMaterial::Create(Color::RED), Math3D::Vec3 normal = Math3D::Vec3(0,1,0)){
        std::shared_ptr<ModelPart> part;
        auto factory = ModelPartFactory::Create(mat);
        int v1, v2, v3, v4;
        factory
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,0.0f,-1.0f)).UV(0,0).Norm(normal), &v1)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,0.0f,-1.0f)).UV(1,0).Norm(normal), &v2)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,0.0f, 1.0f)).UV(1,1).Norm(normal), &v3)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,0.0f, 1.0f)).UV(0,1).Norm(normal), &v4)
            .defineFace(v1,v4,v3,v2);

        part = factory.assemble();
        part->localTransform.setScale(Math3D::Vec3(width,1.0f,height));
        return part;
    }

    /**
     * @brief Creates box.
     * @param width Dimension value.
     * @param height Dimension value.
     * @param depth Value for depth.
     * @param mat Value for mat.
     * @return Pointer to the resulting object.
     */
    static std::shared_ptr<ModelPart> MakeBox(float width, float height, float depth,PMaterial mat = MaterialDefaults::ColorMaterial::Create(Color::RED)){
        auto factory = ModelPartFactory::Create(mat);
        int v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16, v17, v18, v19, v20, v21, v22, v23;

        factory
            // Front face (Z = 1.0, Normal = 0, 0, 1)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f, -1.0f,  1.0f)).UV(0,0).Norm(Math3D::Vec3(0,0,1)), &v0)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f, -1.0f,  1.0f)).UV(1,0).Norm(Math3D::Vec3(0,0,1)), &v1)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,  1.0f,  1.0f)).UV(1,1).Norm(Math3D::Vec3(0,0,1)), &v2)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,  1.0f,  1.0f)).UV(0,1).Norm(Math3D::Vec3(0,0,1)), &v3)

            // Back face (Z = -1.0, Normal = 0, 0, -1)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f, -1.0f, -1.0f)).UV(0,0).Norm(Math3D::Vec3(0,0,-1)), &v4)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f, -1.0f, -1.0f)).UV(1,0).Norm(Math3D::Vec3(0,0,-1)), &v5)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,  1.0f, -1.0f)).UV(1,1).Norm(Math3D::Vec3(0,0,-1)), &v6)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,  1.0f, -1.0f)).UV(0,1).Norm(Math3D::Vec3(0,0,-1)), &v7)

            // Top face (Y = 1.0, Normal = 0, 1, 0)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,  1.0f, -1.0f)).UV(0,0).Norm(Math3D::Vec3(0,1,0)), &v8)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,  1.0f, -1.0f)).UV(1,0).Norm(Math3D::Vec3(0,1,0)), &v9)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,  1.0f,  1.0f)).UV(1,1).Norm(Math3D::Vec3(0,1,0)), &v10)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,  1.0f,  1.0f)).UV(0,1).Norm(Math3D::Vec3(0,1,0)), &v11)

            // Bottom face (Y = -1.0, Normal = 0, -1, 0)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f, -1.0f, -1.0f)).UV(0,1).Norm(Math3D::Vec3(0,-1,0)), &v12)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f, -1.0f, -1.0f)).UV(1,1).Norm(Math3D::Vec3(0,-1,0)), &v13)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f, -1.0f,  1.0f)).UV(1,0).Norm(Math3D::Vec3(0,-1,0)), &v14)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f, -1.0f,  1.0f)).UV(0,0).Norm(Math3D::Vec3(0,-1,0)), &v15)

            // Right face (X = 1.0, Normal = 1, 0, 0)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f, -1.0f, -1.0f)).UV(0,0).Norm(Math3D::Vec3(1,0,0)), &v16)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f, -1.0f,  1.0f)).UV(1,0).Norm(Math3D::Vec3(1,0,0)), &v17)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,  1.0f,  1.0f)).UV(1,1).Norm(Math3D::Vec3(1,0,0)), &v18)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,  1.0f, -1.0f)).UV(0,1).Norm(Math3D::Vec3(1,0,0)), &v19)

            // Left face (X = -1.0, Normal = -1, 0, 0)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f, -1.0f,  1.0f)).UV(0,0).Norm(Math3D::Vec3(-1,0,0)), &v20)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f, -1.0f, -1.0f)).UV(1,0).Norm(Math3D::Vec3(-1,0,0)), &v21)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,  1.0f, -1.0f)).UV(1,1).Norm(Math3D::Vec3(-1,0,0)), &v22)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,  1.0f,  1.0f)).UV(0,1).Norm(Math3D::Vec3(-1,0,0)), &v23)

            // Define the 6 faces
            .defineFace(v0, v1, v2, v3)   // Front
            .defineFace(v4, v5, v6, v7)   // Back
            .defineFace(v8, v11, v10, v9) // Top
            .defineFace(v12, v13, v14, v15) // Bottom
            .defineFace(v16, v19, v18, v17) // Right
            .defineFace(v20, v23, v22, v21); // Left

        auto part = factory.assemble();
        part->localTransform.setScale(Math3D::Vec3(width, height, depth));
        return part;
    }

    /**
     * @brief Creates circle plane.
     * @param radius Value for radius.
     * @param segments Value for segments.
     * @param material Value for material.
     * @param normal Value for normal.
     * @return Pointer to the resulting object.
     */
    static std::shared_ptr<ModelPart> MakeCirclePlane(
        float radius, 
        int segments = 32, 
        PMaterial material = MaterialDefaults::ColorMaterial::Create(Color::RED), 
        Math3D::Vec3 normal = Math3D::Vec3(0,1,0)
    ){
        auto factory = ModelPartFactory::Create(material);

        int centerIndex;
        factory.addVertex(Vertex::Build(Math3D::Vec3(0,0,0)).UV(0.5f,0.5f).Norm(normal), &centerIndex);

        int firstEdgeIndex = -1, lastEdgeIndex = -1;
        for(int i = 0; i <= segments; i++){
            float theta = (float)i / (float)segments * 2.0f * (float) Math3D::PI;

            float x = Math3D::Cos(theta);
            float z = Math3D::Sin(theta);

            int currentIndex;
            float u = x * 0.5f + 0.5f;
            float v = z * 0.5f + 0.5f;

            factory.addVertex(Vertex::Build(Math3D::Vec3(x,0.0f,z)).UV(u,v).Norm(normal), &currentIndex);

            if(i == 0) firstEdgeIndex = currentIndex;

            if(i > 0){
                factory.defineFace(centerIndex, currentIndex, lastEdgeIndex);
            }
            lastEdgeIndex = currentIndex;
        }

        auto part = factory.assemble();

        part->localTransform.setScale(Math3D::Vec3(radius, 1.0f, radius));

        return part;
    }

    /**
     * @brief Creates sphere.
     * @param radius Value for radius.
     * @param slices Value for slices.
     * @param stacks Value for stacks.
     * @param material Value for material.
     * @return Pointer to the resulting object.
     */
    static std::shared_ptr<ModelPart> MakeSphere(
        float radius = 1.0f,
        int slices = 32,
        int stacks = 18,
        PMaterial material = MaterialDefaults::ColorMaterial::Create(Color::WHITE)
    ){
        slices = Math3D::Max(slices, 8);
        stacks = Math3D::Max(stacks, 6);
        if(radius <= Math3D::EPSILON){
            radius = 1.0f;
        }
        if(!material){
            material = MaterialDefaults::ColorMaterial::Create(Color::WHITE);
        }

        auto factory = ModelPartFactory::Create(material);
        std::vector<int> indices(static_cast<size_t>((stacks + 1) * (slices + 1)), -1);

        for(int stack = 0; stack <= stacks; ++stack){
            float v = (float)stack / (float)stacks;
            float phi = v * (float)Math3D::PI;
            float y = std::cos(phi);
            float ringRadius = std::sin(phi);

            for(int slice = 0; slice <= slices; ++slice){
                float u = (float)slice / (float)slices;
                float theta = u * (float)Math3D::PI * 2.0f;
                float x = ringRadius * std::cos(theta);
                float z = ringRadius * std::sin(theta);

                Math3D::Vec3 normal(x, y, z);
                if(normal.length() <= Math3D::EPSILON){
                    normal = Math3D::Vec3(0.0f, 1.0f, 0.0f);
                }else{
                    normal = normal.normalize();
                }

                Vertex vertex = Vertex::Build(Math3D::Vec3(normal.x * radius, normal.y * radius, normal.z * radius))
                                    .Norm(normal)
                                    .UV(u, 1.0f - v);
                int outIndex = -1;
                factory.addVertex(vertex, &outIndex);
                indices[(size_t)stack * (size_t)(slices + 1) + (size_t)slice] = outIndex;
            }
        }

        for(int stack = 0; stack < stacks; ++stack){
            for(int slice = 0; slice < slices; ++slice){
                int i00 = indices[(size_t)stack * (size_t)(slices + 1) + (size_t)slice];
                int i01 = indices[(size_t)stack * (size_t)(slices + 1) + (size_t)(slice + 1)];
                int i10 = indices[(size_t)(stack + 1) * (size_t)(slices + 1) + (size_t)slice];
                int i11 = indices[(size_t)(stack + 1) * (size_t)(slices + 1) + (size_t)(slice + 1)];

                if(stack == 0){
                    factory.defineFace(i00, i11, i10);
                }else if(stack == (stacks - 1)){
                    factory.defineFace(i00, i01, i10);
                }else{
                    factory.defineFace(i00, i11, i10);
                    factory.defineFace(i00, i01, i11);
                }
            }
        }

        return factory.assemble();
    }
};

#endif // MODELPART_PREFABS_H
