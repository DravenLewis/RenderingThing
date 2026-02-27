#include "Scene/LoadedScene.h"

#include "Assets/Core/AssetDescriptorUtils.h"
#include "ECS/Core/ECSComponents.h"
#include "Foundation/IO/File.h"
#include "Foundation/Logging/Logbot.h"
#include "Serialization/IO/SceneIO.h"
#include "Serialization/Schema/ComponentSerializationRegistry.h"

#include <glad/glad.h>
#include <utility>

LoadedScene::LoadedScene(RenderWindow* window,
                         std::string sceneRefOrPath,
                         std::filesystem::path baseDirectory)
    : Scene3D(window),
      sceneRefOrPath(std::move(sceneRefOrPath)),
      baseDirectory(std::move(baseDirectory)) {
}

void LoadedScene::init(){
    sceneLoaded = loadSceneDocument();
    if(sceneLoaded){
        LogBot.Log(LOG_INFO, "LoadedScene initialized from '%s'.", sceneRefOrPath.c_str());
    }else if(!lastLoadError.empty()){
        LogBot.Log(LOG_ERRO, "LoadedScene initialization failed: %s", lastLoadError.c_str());
    }
}

bool LoadedScene::loadSceneDocument(){
    lastLoadError.clear();
    if(sceneRefOrPath.empty()){
        lastLoadError = "Scene ref/path is empty.";
        return false;
    }

    PScene self = std::static_pointer_cast<Scene>(shared_from_this());
    if(!self){
        lastLoadError = "Failed to resolve shared scene pointer for load.";
        return false;
    }

    SceneIO::SceneLoadOptions loadOptions;
    loadOptions.registry = &Serialization::DefaultComponentSerializationRegistry();

    const bool usesAssetRef = AssetDescriptorUtils::IsAssetRef(sceneRefOrPath);
    if(usesAssetRef){
        if(!SceneIO::LoadSceneFromAssetRef(self, sceneRefOrPath, loadOptions, nullptr, &lastLoadError)){
            return false;
        }
    }else{
        std::filesystem::path absolutePath = std::filesystem::path(sceneRefOrPath);
        if(absolutePath.is_relative()){
            if(!baseDirectory.empty()){
                absolutePath = baseDirectory / absolutePath;
            }else{
                absolutePath = std::filesystem::path(File::GetCWD()) / absolutePath;
            }
        }
        absolutePath = absolutePath.lexically_normal();

        if(!SceneIO::LoadSceneFromAbsolutePath(self, absolutePath, loadOptions, nullptr, &lastLoadError)){
            return false;
        }
    }

    ensureCameraAfterLoad();
    return true;
}

void LoadedScene::ensureCameraAfterLoad(){
    if(getPreferredCamera()){
        return;
    }
    if(!getECS()){
        return;
    }
    auto* manager = getECS()->getComponentManager();
    auto* entityManager = getECS()->getEntityManager();
    if(!manager || !entityManager){
        return;
    }

    const auto& entities = entityManager->getEntities();
    for(const auto& entityPtr : entities){
        NeoECS::ECSEntity* entity = entityPtr.get();
        if(!entity){
            continue;
        }
        auto* cameraComp = manager->getECSComponent<CameraComponent>(entity);
        if(cameraComp && cameraComp->camera){
            setPreferredCamera(cameraComp->camera, true);
            return;
        }
    }
}

void LoadedScene::render(){
    if(!getWindow()){
        return;
    }

    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render3DPass();
}
