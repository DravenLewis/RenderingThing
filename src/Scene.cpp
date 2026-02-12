#include "Scene.h"

#include "Screen.h"
#include "ShadowRenderer.h"
#include "SkyBox.h"
#include "ECSComponents.h"
#include <glad/glad.h>

Scene::Scene(RenderWindow* window) : View(window) {
    ecsInstance = NeoECS::NeoECS::newInstance();
    if(ecsInstance){
        ecsInstance->init();
        ecsAPI = ecsInstance->getAPI();
    }
}

NeoECS::GameObject* Scene::createECSGameObject(const std::string& name, NeoECS::GameObject* parent){
    if(!ecsAPI) return nullptr;
    return ecsAPI->CreateGameObjectAndInstantiate(name, parent);
}

bool Scene::destroyECSGameObject(NeoECS::GameObject* object){
    if(!ecsAPI || !object) return false;
    return ecsAPI->DestroyGameObject(object);
}

NeoECS::GameObject* Scene::createModelGameObject(const std::string& name, const PModel& model, NeoECS::GameObject* parent){
    auto* root = createECSGameObject(name, parent);
    if(!root || !model) return root;

    root->addComponent<TransformComponent>();
    if(auto* transform = root->getComponent<TransformComponent>()){
        transform->local = model->transform();
    }

    root->addComponent<MeshRendererComponent>();
    if(auto* renderer = root->getComponent<MeshRendererComponent>()){
        renderer->model = model;
    }

    return root;
}

NeoECS::GameObject* Scene::createLightGameObject(const std::string& name, const Light& light, NeoECS::GameObject* parent, bool syncTransform, bool syncDirection){
    auto* root = createECSGameObject(name, parent);
    if(!root) return nullptr;

    root->addComponent<TransformComponent>();
    if(auto* transform = root->getComponent<TransformComponent>()){
        if(syncTransform){
            transform->local.setPosition(light.position);
        }
    }

    root->addComponent<LightComponent>();
    if(auto* lightComponent = root->getComponent<LightComponent>()){
        lightComponent->light = light;
        lightComponent->syncTransform = syncTransform;
        lightComponent->syncDirection = syncDirection;
    }

    return root;
}

void Scene::dispose(){
    if(ecsInstance){
        NeoECS::NeoECS::disposeInstance(ecsInstance);
        ecsInstance = nullptr;
        ecsAPI = nullptr;
    }
}

void Scene::updateECS(float deltaTime){
    if(ecsInstance){
        ecsInstance->update(deltaTime);
    }
}

namespace {
    Math3D::Mat4 buildWorldMatrix(NeoECS::ECSEntity* entity, NeoECS::ECSComponentManager* componentManager){
        Math3D::Mat4 world(1.0f);
        std::vector<NeoECS::ECSEntity*> chain;
        for(auto* current = entity; current != nullptr; current = current->getParent()){
            chain.push_back(current);
        }
        for(auto it = chain.rbegin(); it != chain.rend(); ++it){
            auto* transform = componentManager->getECSComponent<TransformComponent>(*it);
            if(transform){
                world = world * transform->local.toMat4();
            }
        }
        return world;
    }
}

void Scene::updateSceneLights(){
    auto screen = getMainScreen();
    if(!screen) return;

    auto env = screen->getEnvironment();
    if(!env) return;

    auto& lightManager = env->getLightManager();
    lightManager.clearLights();
    if(!ecsInstance) return;

    auto* componentManager = ecsInstance->getComponentManager();
    auto& entities = ecsInstance->getEntityManager()->getEntities();
    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        auto* lightComponent = componentManager->getECSComponent<LightComponent>(entity);
        if(!lightComponent) continue;

        Light light = lightComponent->light;
        if(lightComponent->syncTransform){
            Math3D::Mat4 world = buildWorldMatrix(entity, componentManager);
            light.position = world.getPosition();
            if(lightComponent->syncDirection){
                Math3D::Vec3 forward = Math3D::Transform::transformPoint(world, Math3D::Vec3(0,0,1)) - light.position;
                if(forward.length() > 0.0001f){
                    light.direction = forward.normalize();
                }
            }
        }
        lightManager.addLight(light);
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
    if(!cam || !ecsInstance) return;

    auto* componentManager = ecsInstance->getComponentManager();
    auto& entities = ecsInstance->getEntityManager()->getEntities();
    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        auto* renderer = componentManager->getECSComponent<MeshRendererComponent>(entity);
        if(!renderer || !renderer->visible) continue;

        Math3D::Mat4 world = buildWorldMatrix(entity, componentManager);
        world = world * renderer->localOffset.toMat4();

        bool enableCulling = renderer->enableBackfaceCulling;
        if(renderer->model){
            enableCulling = renderer->model->isBackfaceCullingEnabled();
        }
        if(enableCulling){
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }else{
            glDisable(GL_CULL_FACE);
        }

        if(renderer->model){
            const auto& parts = renderer->model->getParts();
            for(const auto& part : parts){
                if(!part || !part->mesh || !part->material) continue;
                Math3D::Mat4 partWorld = world * part->localTransform.toMat4();
                part->material->set<Math3D::Mat4>("u_model", partWorld);
                part->material->set<Math3D::Mat4>("u_view", cam->getViewMatrix());
                part->material->set<Math3D::Mat4>("u_projection", cam->getProjectionMatrix());
                part->material->bind();
                part->mesh->draw();
                part->material->unbind();
            }
        }else{
            if(!renderer->mesh || !renderer->material) continue;
            renderer->material->set<Math3D::Mat4>("u_model", world);
            renderer->material->set<Math3D::Mat4>("u_view", cam->getViewMatrix());
            renderer->material->set<Math3D::Mat4>("u_projection", cam->getProjectionMatrix());
            renderer->material->bind();
            renderer->mesh->draw();
            renderer->material->unbind();
        }

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
}

void Scene::drawShadowsPass(){
    if(!ecsInstance) return;

    auto* componentManager = ecsInstance->getComponentManager();
    auto& entities = ecsInstance->getEntityManager()->getEntities();
    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        auto* renderer = componentManager->getECSComponent<MeshRendererComponent>(entity);
        if(!renderer || !renderer->visible) continue;

        Math3D::Mat4 world = buildWorldMatrix(entity, componentManager);
        world = world * renderer->localOffset.toMat4();
        if(renderer->model){
            const auto& parts = renderer->model->getParts();
            for(const auto& part : parts){
                if(!part || !part->mesh || !part->material) continue;
                Math3D::Mat4 partWorld = world * part->localTransform.toMat4();
                ShadowRenderer::RenderShadows(part->mesh, partWorld, part->material);
            }
        }else{
            if(!renderer->mesh || !renderer->material) continue;
            ShadowRenderer::RenderShadows(renderer->mesh, world, renderer->material);
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
