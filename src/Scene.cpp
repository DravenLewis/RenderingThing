#include "Scene.h"

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
