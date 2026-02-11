#include "Scene.h"

#include "Screen.h"
#include "ShadowRenderer.h"
#include "SkyBox.h"

Scene::Scene(RenderWindow* window) : View(window) {}

void Scene::addModel(const PModel& model){
    if(model){
        models.push_back(model);
    }
}

void Scene::clearModels(){
    models.clear();
}

void Scene::addLight(const Light& light){
    auto screen = getMainScreen();
    if(!screen) return;

    auto env = screen->getEnvironment();
    if(env){
        env->getLightManager().addLight(light);
    }
}

void Scene::clearLights(){
    auto screen = getMainScreen();
    if(!screen) return;

    auto env = screen->getEnvironment();
    if(env){
        env->getLightManager().clearLights();
    }
}

void Scene::render3DPass(){
    auto screen = getMainScreen();
    if(!screen) return;

    screen->bind();

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
    for(const auto& model : models){
        if(model){
            model->draw(cam);
        }
    }
}

void Scene::drawShadowsPass(){
    for(const auto& model : models){
        if(model){
            model->drawShadows();
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
