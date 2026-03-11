/**
 * @file src/Rendering/Geometry/ModelPrefabs.h
 * @brief Declarations for ModelPrefabs.
 */

#ifndef MODEL_PREFABS_H
#define MODEL_PREFABS_H

#include <memory>

#include "Rendering/Materials/MaterialDefaults.h"
#include "Rendering/Geometry/ModelPartPrefabs.h"
#include "Rendering/Geometry/Model.h"

/// @brief Holds data for ModelPrefabs.
struct ModelPrefabs{
    /// @brief Holds data for CubeMaterialDefinition.
    struct CubeMaterialDefinition{
        PMaterial matTop = MaterialDefaults::ColorMaterial::Create(Color::MAGENTA);
        PMaterial matBottom = MaterialDefaults::ColorMaterial::Create(Color::MAGENTA);
        PMaterial matLeft = MaterialDefaults::ColorMaterial::Create(Color::MAGENTA);
        PMaterial matRight = MaterialDefaults::ColorMaterial::Create(Color::MAGENTA);
        PMaterial matFront = MaterialDefaults::ColorMaterial::Create(Color::MAGENTA);
        PMaterial matBack = MaterialDefaults::ColorMaterial::Create(Color::MAGENTA);


        /**
         * @brief Creates a new object.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<CubeMaterialDefinition> Create() {return std::make_shared<CubeMaterialDefinition>();};
        static std::shared_ptr<CubeMaterialDefinition> CreateSame(PMaterial mat) {
            auto def = std::make_shared<CubeMaterialDefinition>();
            def->matFront = mat;
            def->matBack = mat;
            def->matLeft = mat;
            def->matRight = mat;
            def->matTop = mat;
            def->matBottom = mat;
            return def;
        };
    };

    /**
     * @brief Creates cube.
     * @param scale Spatial value used by this operation.
     * @param matDef Value for mat def.
     * @return Pointer to the resulting object.
     */
    static std::shared_ptr<Model> MakeCube(float scale = 1.0f, std::shared_ptr<CubeMaterialDefinition> matDef = CubeMaterialDefinition::Create()){

        auto model = Model::Create();

        std::shared_ptr<ModelPart> cubeTop = ModelPartPrefabs::MakePlane(scale, scale, matDef->matTop);
        cubeTop->localTransform.setPosition(Math3D::Vec3(0.0f,1.0f,0.0f));
        cubeTop->localTransform.setRotation(0.0f,0.0f,0.0f);

        std::shared_ptr<ModelPart> cubeBottom = ModelPartPrefabs::MakePlane(scale, scale, matDef->matBottom);
        cubeBottom->localTransform.setPosition(Math3D::Vec3(0.0f,-1.0f,0.0f));
        cubeBottom->localTransform.setRotation(180.0f,0.0f,0.0f);

        std::shared_ptr<ModelPart> cubeLeft = ModelPartPrefabs::MakePlane(scale, scale, matDef->matLeft);
        cubeLeft->localTransform.setPosition(Math3D::Vec3(-1.0f,0.0f,0.0f));
        cubeLeft->localTransform.setRotation(0.0f,-90.0f,90.0f);

        std::shared_ptr<ModelPart> cubeRight = ModelPartPrefabs::MakePlane(scale, scale, matDef->matRight);
        cubeRight->localTransform.setPosition(Math3D::Vec3(1.0f,0.0f,0.0f));
        cubeRight->localTransform.setRotation(0.0f,90.0f,-90.0f);

        std::shared_ptr<ModelPart> cubeFront = ModelPartPrefabs::MakePlane(scale, scale, matDef->matFront);
        cubeFront->localTransform.setPosition(Math3D::Vec3(0.0f,0.0f,1.0f));
        cubeFront->localTransform.setRotation(-90.0f,180.0f,180.0f);

        std::shared_ptr<ModelPart> cubeBack = ModelPartPrefabs::MakePlane(scale, scale, matDef->matBack);
        cubeBack->localTransform.setPosition(Math3D::Vec3(0.0f,0.0f,-1.0f));
        cubeBack->localTransform.setRotation(90.0f,180.0f,0.0f);

        model->addPart(cubeTop).addPart(cubeBottom).addPart(cubeLeft).addPart(cubeRight).addPart(cubeFront).addPart(cubeBack);

        return model;
    }
};

#endif // MODEL_PREFABS_H
