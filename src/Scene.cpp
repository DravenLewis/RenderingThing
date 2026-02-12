#include "Scene.h"

#include "Screen.h"
#include "ShadowRenderer.h"
#include "SkyBox.h"

#include <algorithm>

Scene::Scene(RenderWindow* window) : View(window) {}

void Scene::addSceneObject(const PSceneObject& object){
    if(object){
        sceneObjects.push_back(object);
        if(std::dynamic_pointer_cast<LightSceneObject>(object)){
            manageSceneLights = true;
        }
    }
}

void Scene::removeSceneObject(const PSceneObject& object){
    if(!object) return;
    sceneObjects.erase(
        std::remove(sceneObjects.begin(), sceneObjects.end(), object),
        sceneObjects.end()
    );
    if(manageSceneLights){
        updateSceneLights();
    }
}

void Scene::clearSceneObjects(){
    sceneObjects.clear();
    if(manageSceneLights){
        updateSceneLights();
    }
}

void Scene::addModel(const PModel& model){
    if(!model) return;
    addSceneObject(ModelSceneObject::Create(model));
}

void Scene::removeModel(const PModel& model){
    if(!model) return;
    sceneObjects.erase(
        std::remove_if(
            sceneObjects.begin(),
            sceneObjects.end(),
            [&model](const PSceneObject& object){
                auto modelObject = std::dynamic_pointer_cast<ModelSceneObject>(object);
                return modelObject && modelObject->getModel() == model;
            }
        ),
        sceneObjects.end()
    );
}

void Scene::removeModelObject(const PModelSceneObject& object){
    if(!object) return;
    removeSceneObject(object);
}

void Scene::clearModels(){
    sceneObjects.erase(
        std::remove_if(
            sceneObjects.begin(),
            sceneObjects.end(),
            [](const PSceneObject& object){
                return std::dynamic_pointer_cast<ModelSceneObject>(object) != nullptr;
            }
        ),
        sceneObjects.end()
    );
}

std::vector<PModel> Scene::getModels() const {
    std::vector<PModel> result;
    result.reserve(sceneObjects.size());
    for(const auto& object : sceneObjects){
        auto modelObject = std::dynamic_pointer_cast<ModelSceneObject>(object);
        if(modelObject && modelObject->getModel()){
            result.push_back(modelObject->getModel());
        }
    }
    return result;
}

void Scene::addLight(const Light& light){
    manageSceneLights = true;
    addSceneObject(LightSceneObject::Create(light));
}

void Scene::removeLightObject(const PLightSceneObject& object){
    manageSceneLights = true;
    removeSceneObject(object);
}

void Scene::clearLights(){
    manageSceneLights = true;
    sceneObjects.erase(
        std::remove_if(
            sceneObjects.begin(),
            sceneObjects.end(),
            [](const PSceneObject& object){
                return std::dynamic_pointer_cast<LightSceneObject>(object) != nullptr;
            }
        ),
        sceneObjects.end()
    );
}

void Scene::updateSceneLights(){
    if(!manageSceneLights) return;
    auto screen = getMainScreen();
    if(!screen) return;

    auto env = screen->getEnvironment();
    if(!env) return;

    auto& lightManager = env->getLightManager();
    lightManager.clearLights();
    for(const auto& object : sceneObjects){
        if(object){
            object->addLights(lightManager);
        }
    }
}

void Scene::render3DPass(){
    auto screen = getMainScreen();
    if(!screen) return;

    screen->bind();

    updateSceneLights();

    auto cam = screen->getCamera();
    if(cam){
        if(!ShadowRenderer::IsEnabled()){
            ShadowRenderer::BeginFrame(cam);
        }

        drawShadowsPass();
        drawSkybox(cam);
        drawModels3D(cam);
    }

    screen->unbind();
}

void Scene::drawModels3D(PCamera cam){
    if(!cam) return;
    for(const auto& object : sceneObjects){
        if(object){
            object->draw(cam);
        }
    }
}

void Scene::drawShadowsPass(){
    for(const auto& object : sceneObjects){
        if(object){
            object->drawShadows();
        }
    }
}

void Scene::drawSkybox(PCamera cam){
    if(!cam) return;
    auto screen = getMainScreen();
    if(!screen) return;
    auto env = screen->getEnvironment();
    if(env && env->getSkyBox()){
        env->getSkyBox()->draw(cam);
    }
}
