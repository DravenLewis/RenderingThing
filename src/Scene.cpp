#include "Scene.h"

#include "Screen.h"
#include "ShadowRenderer.h"
#include "Logbot.h"
#include "SkyBox.h"
#include "ECSComponents.h"
#include "Color.h"
#include <chrono>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

Scene::Scene(RenderWindow* window) : View(window) {
    ecsInstance = NeoECS::NeoECS::newInstance();
    if(ecsInstance){
        ecsInstance->init();
        ecsAPI = ecsInstance->getAPI();
    }
}

void Scene::requestClose(){
    LogBot.Log(LOG_WARN, "Scene::requestClose() called.");
    closeRequested.store(true, std::memory_order_relaxed);
}

bool Scene::consumeCloseRequest(){
    bool requested = closeRequested.exchange(false, std::memory_order_relaxed);
    if(requested){
        LogBot.Log(LOG_WARN, "Scene::consumeCloseRequest() consumed request.");
    }
    return requested;
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

Math3D::Mat4 Scene::buildWorldMatrix(NeoECS::ECSEntity* entity, NeoECS::ECSComponentManager* manager) const{
    Math3D::Mat4 world(1.0f);
    std::vector<NeoECS::ECSEntity*> chain;
    for(auto* current = entity; current != nullptr; current = current->getParent()){
        chain.push_back(current);
    }
    for(auto it = chain.rbegin(); it != chain.rend(); ++it){
        auto* transform = manager->getECSComponent<TransformComponent>(*it);
        if(transform){
            world = world * transform->local.toMat4();
        }
    }
    return world;
}

void Scene::updateECS(float deltaTime){
    if(!ecsInstance) return;
    ecsInstance->update(deltaTime);

    auto snapshotStart = std::chrono::steady_clock::now();

    const int backIndex = 1 - renderSnapshotIndex.load(std::memory_order_acquire);
    auto& snapshot = renderSnapshots[backIndex];
    snapshot.drawItems.clear();
    snapshot.lights.clear();

    auto* componentManager = ecsInstance->getComponentManager();
    auto& entities = ecsInstance->getEntityManager()->getEntities();
    snapshot.drawItems.reserve(entities.size());
    snapshot.lights.reserve(entities.size());

    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(!entity) continue;

        auto* renderer = componentManager->getECSComponent<MeshRendererComponent>(entity);
        auto* lightComponent = componentManager->getECSComponent<LightComponent>(entity);
        const bool needsWorld = (renderer && renderer->visible) || (lightComponent && lightComponent->syncTransform);

        Math3D::Mat4 world(1.0f);
        if(needsWorld){
            world = buildWorldMatrix(entity, componentManager);
        }

        if(renderer && renderer->visible){
            Math3D::Mat4 base = world * renderer->localOffset.toMat4();
            bool cull = renderer->enableBackfaceCulling;

            if(renderer->model){
                cull = renderer->model->isBackfaceCullingEnabled();
                const auto& parts = renderer->model->getParts();
                for(const auto& part : parts){
                    if(!part || !part->mesh || !part->material) continue;
                    RenderItem item;
                    item.mesh = part->mesh;
                    item.material = part->material;
                    item.model = base * part->localTransform.toMat4();
                    item.enableBackfaceCulling = cull;
                    item.entityId = entity->getNodeUniqueID();
                    snapshot.drawItems.push_back(std::move(item));
                }
            }else if(renderer->mesh && renderer->material){
                RenderItem item;
                item.mesh = renderer->mesh;
                item.material = renderer->material;
                item.model = base;
                item.enableBackfaceCulling = cull;
                item.entityId = entity->getNodeUniqueID();
                snapshot.drawItems.push_back(std::move(item));
            }
        }

        if(lightComponent){
            Light light = lightComponent->light;
            if(lightComponent->syncTransform){
                light.position = world.getPosition();
                if(lightComponent->syncDirection){
                    Math3D::Vec3 forward = Math3D::Transform::transformPoint(world, Math3D::Vec3(0,0,1)) - light.position;
                    if(forward.length() > 0.0001f){
                        light.direction = forward.normalize();
                    }
                }
            }
            snapshot.lights.push_back(light);
        }
    }

    renderSnapshotIndex.store(backIndex, std::memory_order_release);

    auto snapshotEnd = std::chrono::steady_clock::now();
    std::chrono::duration<float, std::milli> snapshotMs = snapshotEnd - snapshotStart;
    debugStats.snapshotMs.store(snapshotMs.count(), std::memory_order_relaxed);
    debugStats.drawCount.store(static_cast<int>(snapshot.drawItems.size()), std::memory_order_relaxed);
    debugStats.lightCount.store(static_cast<int>(snapshot.lights.size()), std::memory_order_relaxed);
}

void Scene::updateSceneLights(){
    auto screen = getMainScreen();
    if(!screen) return;

    auto env = screen->getEnvironment();
    if(!env) return;

    auto& lightManager = env->getLightManager();
    lightManager.clearLights();
    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    for(const auto& light : snapshot.lights){
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

        auto shadowStart = std::chrono::steady_clock::now();
        drawShadowsPass();
        auto shadowEnd = std::chrono::steady_clock::now();
        std::chrono::duration<float, std::milli> shadowMs = shadowEnd - shadowStart;
        debugStats.shadowMs.store(shadowMs.count(), std::memory_order_relaxed);

        drawSkybox(cam);

        auto drawStart = std::chrono::steady_clock::now();
        drawModels3D(cam);
        auto drawEnd = std::chrono::steady_clock::now();
        std::chrono::duration<float, std::milli> drawMs = drawEnd - drawStart;
        debugStats.drawMs.store(drawMs.count(), std::memory_order_relaxed);
    }

    screen->unbind();
}

void Scene::drawModels3D(PCamera cam){
    if(!cam) return;

    if(!outlineMaterial){
        outlineMaterial = MaterialDefaults::ColorMaterial::Create(Color::fromRGBA32(0x3498dbFF));
    }

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material) continue;

        if(item.enableBackfaceCulling){
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }else{
            glDisable(GL_CULL_FACE);
        }

        item.material->set<Math3D::Mat4>("u_model", item.model);
        item.material->set<Math3D::Mat4>("u_view", cam->getViewMatrix());
        item.material->set<Math3D::Mat4>("u_projection", cam->getProjectionMatrix());
        item.material->bind();
        item.mesh->draw();
        item.material->unbind();

        if(outlineEnabled && !selectedEntityId.empty() && item.entityId == selectedEntityId && outlineMaterial){
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);

            glm::mat4 outline = (glm::mat4)item.model;
            outline = outline * glm::scale(glm::mat4(1.0f), glm::vec3(1.03f));
            Math3D::Mat4 outlineModel(outline);

            outlineMaterial->set<Math3D::Mat4>("u_model", outlineModel);
            outlineMaterial->set<Math3D::Mat4>("u_view", cam->getViewMatrix());
            outlineMaterial->set<Math3D::Mat4>("u_projection", cam->getProjectionMatrix());
            outlineMaterial->bind();
            item.mesh->draw();
            outlineMaterial->unbind();
            glCullFace(GL_BACK);
        }
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void Scene::drawShadowsPass(){
    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material) continue;
        ShadowRenderer::RenderShadows(item.mesh, item.model, item.material);
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

Math3D::Vec3 Scene::getWorldPosition(NeoECS::ECSEntity* entity) const{
    if(!entity || !ecsInstance) return Math3D::Vec3();
    auto* manager = ecsInstance->getComponentManager();
    Math3D::Mat4 world = buildWorldMatrix(entity, manager);
    return world.getPosition();
}
