#include "SkyBox.h"

#include "ModelPartPrefabs.h"
#include "StringUtils.h"
#include "Logbot.h"

SkyBox::SkyBox(const SkyBox6Face& faceAssetDef){

    this->skyboxCubeMap = CubeMap::Load(
        faceAssetDef.rightFaceAsset,
        faceAssetDef.leftFaceAsset,
        faceAssetDef.topFaceAsset,
        faceAssetDef.bottomFaceAsset,
        faceAssetDef.frontFaceAsset,
        faceAssetDef.backFaceAsset
    );

    if(!this->skyboxCubeMap){
        LogBot.Log(LOG_WARN, "Skybox cubemap failed to load; background will be black.");
    }

    this->skyboxMaterial = SkyboxMaterial::Create(this->skyboxCubeMap);
    this->skyboxModel = Model::Create();
    if(this->skyboxMaterial){
        this->skyboxMaterial->setCastsShadows(false);
        this->skyboxMaterial->setReceivesShadows(false);
        this->skyboxModel->addPart(ModelPartPrefabs::MakeBox(1.0f, 1.0f, 1.0f, skyboxMaterial));
        this->skyboxModel->setBackfaceCulling(false);
    }
};

void SkyBox::draw(PCamera cam){
    if(!this->skyboxModel || !cam){
        return;
    }

    if(this->skyboxModel && cam){
        // Preserve GL state to avoid breaking other draws
        GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
        GLboolean blendEnabled = glIsEnabled(GL_BLEND);
        GLint prevDepthFunc = GL_LESS;
        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
        GLboolean prevDepthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

        // Draw skybox first, with inside faces visible
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDepthFunc(GL_ALWAYS);
        this->skyboxModel->draw(cam);
        glDepthFunc(prevDepthFunc);
        if(cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        if(blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glDepthMask(prevDepthMask);
        if(depthTestEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    }
}

std::shared_ptr<SkyBox> SkyBoxLoader::CreateSkyBox(const std::string& directoryAssetPath, const std::string& skyboxName){
    if(directoryAssetPath.empty() || skyboxName.empty()){
        LogBot.Log(LOG_ERRO,"SkyBoxLoader::CreateSkyBox - directory and name are required.");
        return nullptr;
    }

    // Require an @assets-style path to a directory, e.g. "@assets/images/skybox/default"
    std::string basePath = directoryAssetPath;
    if(!StringUtils::EndsWith(basePath, "/") && !StringUtils::EndsWith(basePath, "\\")){
        basePath += "/";
    }

    SkyBox6Face assetBundle;

    assetBundle.backFaceAsset = AssetManager::Instance.getOrLoad(StringUtils::Format("%s%s_back.png", basePath.c_str(), skyboxName.c_str()));
    assetBundle.bottomFaceAsset = AssetManager::Instance.getOrLoad(StringUtils::Format("%s%s_bottom.png", basePath.c_str(), skyboxName.c_str()));
    assetBundle.frontFaceAsset = AssetManager::Instance.getOrLoad(StringUtils::Format("%s%s_front.png", basePath.c_str(), skyboxName.c_str()));
    assetBundle.leftFaceAsset = AssetManager::Instance.getOrLoad(StringUtils::Format("%s%s_left.png", basePath.c_str(), skyboxName.c_str()));
    assetBundle.rightFaceAsset = AssetManager::Instance.getOrLoad(StringUtils::Format("%s%s_right.png", basePath.c_str(), skyboxName.c_str()));
    assetBundle.topFaceAsset = AssetManager::Instance.getOrLoad(StringUtils::Format("%s%s_top.png", basePath.c_str(), skyboxName.c_str()));

    return std::make_shared<SkyBox>(assetBundle);
}
