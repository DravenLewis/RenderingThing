#ifndef SKYBOX_H
#define SKYBOX_H

#include "Model.h"
#include "SkyboxMaterial.h"
#include "CubeMap.h"
#include "Asset.h"
#include "Camera.h"

#include <memory>

struct SkyBox6Face{
    PAsset topFaceAsset = nullptr;
    PAsset bottomFaceAsset = nullptr;
    PAsset leftFaceAsset = nullptr;
    PAsset rightFaceAsset = nullptr;
    PAsset frontFaceAsset = nullptr;
    PAsset backFaceAsset = nullptr;
};

class SkyBox{
    private:
        PModel skyboxModel;
        PMaterial skyboxMaterial;
        PCubeMap skyboxCubeMap;
        SkyBox() = delete;
    public:
        SkyBox(const SkyBox6Face& faceAssetDef);
        void draw(PCamera cam);
        PCubeMap getCubeMap() const { return skyboxCubeMap; }

};

class SkyBoxLoader{
    public:
        static std::shared_ptr<SkyBox> CreateSkyBox(const std::string& directoryAssetPath, const std::string& skyboxName);
};

typedef std::shared_ptr<SkyBox> PSkyBox;

#endif//SKYBOX_H
