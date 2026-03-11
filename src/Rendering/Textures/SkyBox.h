/**
 * @file src/Rendering/Textures/SkyBox.h
 * @brief Declarations for SkyBox.
 */

#ifndef SKYBOX_H
#define SKYBOX_H

#include "Rendering/Geometry/Model.h"
#include "Rendering/Materials/SkyboxMaterial.h"
#include "Rendering/Textures/CubeMap.h"
#include "Assets/Core/Asset.h"
#include "Scene/Camera.h"

#include <memory>

/// @brief Holds data for SkyBox6Face.
struct SkyBox6Face{
    PAsset topFaceAsset = nullptr;
    PAsset bottomFaceAsset = nullptr;
    PAsset leftFaceAsset = nullptr;
    PAsset rightFaceAsset = nullptr;
    PAsset frontFaceAsset = nullptr;
    PAsset backFaceAsset = nullptr;
};

/// @brief Represents the SkyBox type.
class SkyBox{
    private:
        PModel skyboxModel;
        PMaterial skyboxMaterial;
        PCubeMap skyboxCubeMap;
        /**
         * @brief Constructs a new SkyBox instance.
         */
        SkyBox() = delete;
    public:
        /**
         * @brief Constructs a new SkyBox instance.
         * @param faceAssetDef Value for face asset def.
         */
        SkyBox(const SkyBox6Face& faceAssetDef);
        /**
         * @brief Draws this object.
         * @param cam Value for cam.
         * @param depthTested Value for depth tested.
         */
        void draw(PCamera cam, bool depthTested = false);
        /**
         * @brief Returns the cube map.
         * @return Result of this operation.
         */
        PCubeMap getCubeMap() const { return skyboxCubeMap; }

};

/// @brief Represents the SkyBoxLoader type.
class SkyBoxLoader{
    public:
        /**
         * @brief Creates sky box.
         * @param directoryAssetPath Filesystem path for directory asset path.
         * @param skyboxName Name used for skybox name.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<SkyBox> CreateSkyBox(const std::string& directoryAssetPath, const std::string& skyboxName);
};

typedef std::shared_ptr<SkyBox> PSkyBox;

#endif//SKYBOX_H
