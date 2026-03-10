#include "Scene/Scene.h"

#include "Rendering/Core/Screen.h"
#include "Rendering/Lighting/ShadowRenderer.h"
#include "Foundation/Logging/Logbot.h"
#include "Rendering/Textures/SkyBox.h"
#include "Assets/Descriptors/SkyboxAsset.h"
#include "ECS/Core/ECSComponents.h"
#include "Foundation/Math/Color.h"
#include "Engine/Core/GameEngine.h"
#include "Rendering/Materials/PBRMaterial.h"
#include "Rendering/Lighting/LightUtils.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Assets/Core/Asset.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

namespace {
    bool buildLocalBoundsFromComponent(const BoundsComponent* bounds, Math3D::Vec3& outMin, Math3D::Vec3& outMax){
        if(!bounds){
            return false;
        }
        const Math3D::Vec3 center = bounds->offset;
        switch(bounds->type){
            case BoundsType::Box: {
                Math3D::Vec3 e = bounds->size;
                outMin = center + Math3D::Vec3(-e.x, -e.y, -e.z);
                outMax = center + Math3D::Vec3( e.x,  e.y,  e.z);
                return true;
            }
            case BoundsType::Sphere: {
                float r = bounds->radius;
                outMin = center + Math3D::Vec3(-r, -r, -r);
                outMax = center + Math3D::Vec3( r,  r,  r);
                return true;
            }
            case BoundsType::Capsule: {
                float r = bounds->radius;
                float half = bounds->height * 0.5f;
                outMin = center + Math3D::Vec3(-r, -(r + half), -r);
                outMax = center + Math3D::Vec3( r,  (r + half),  r);
                return true;
            }
            default:
                break;
        }
        return false;
    }

    void transformAabb(const Math3D::Mat4& model,
                       const Math3D::Vec3& localMin,
                       const Math3D::Vec3& localMax,
                       Math3D::Vec3& outMin,
                       Math3D::Vec3& outMax){
        glm::vec3 corners[8] = {
            glm::vec3(localMin.x, localMin.y, localMin.z),
            glm::vec3(localMax.x, localMin.y, localMin.z),
            glm::vec3(localMin.x, localMax.y, localMin.z),
            glm::vec3(localMax.x, localMax.y, localMin.z),
            glm::vec3(localMin.x, localMin.y, localMax.z),
            glm::vec3(localMax.x, localMin.y, localMax.z),
            glm::vec3(localMin.x, localMax.y, localMax.z),
            glm::vec3(localMax.x, localMax.y, localMax.z)
        };

        glm::vec3 minV(FLT_MAX);
        glm::vec3 maxV(-FLT_MAX);
        glm::mat4 m = (glm::mat4)model;
        for(const auto& c : corners){
            glm::vec4 world = m * glm::vec4(c, 1.0f);
            if(world.w != 0.0f){
                world /= world.w;
            }
            glm::vec3 p(world);
            minV = glm::min(minV, p);
            maxV = glm::max(maxV, p);
        }
        outMin = Math3D::Vec3(minV);
        outMax = Math3D::Vec3(maxV);
    }

    bool rayIntersectsAabb(const Math3D::Vec3& origin,
                           const Math3D::Vec3& direction,
                           const Math3D::Vec3& boundsMin,
                           const Math3D::Vec3& boundsMax,
                           float& outDistance){
        float tMin = 0.0f;
        float tMax = FLT_MAX;

        auto testAxis = [&](float originAxis, float dirAxis, float minAxis, float maxAxis) -> bool {
            if(std::abs(dirAxis) < 0.000001f){
                return (originAxis >= minAxis && originAxis <= maxAxis);
            }

            float inv = 1.0f / dirAxis;
            float t1 = (minAxis - originAxis) * inv;
            float t2 = (maxAxis - originAxis) * inv;
            if(t1 > t2){
                std::swap(t1, t2);
            }
            tMin = Math3D::Max(tMin, t1);
            tMax = Math3D::Min(tMax, t2);
            return tMax >= tMin;
        };

        if(!testAxis(origin.x, direction.x, boundsMin.x, boundsMax.x)) return false;
        if(!testAxis(origin.y, direction.y, boundsMin.y, boundsMax.y)) return false;
        if(!testAxis(origin.z, direction.z, boundsMin.z, boundsMax.z)) return false;

        float hitDistance = (tMin >= 0.0f) ? tMin : tMax;
        if(hitDistance < 0.0f || !std::isfinite(hitDistance)){
            return false;
        }

        outDistance = hitDistance;
        return true;
    }

}

Scene::Scene(RenderWindow* window) : View(window) {
    ecsInstance = NeoECS::NeoECS::newInstance();
    if(ecsInstance){
        ecsInstance->init();
        ecsAPI = ecsInstance->getAPI();
        if(ecsAPI){
            sceneRootObject = ecsAPI->CreateGameObjectAndInstantiate("SceneNode", nullptr);
            if(sceneRootObject){
                sceneRootObject->setReparentChildrenOnDestroy(true);
                if(!sceneRootObject->getComponent<EntityPropertiesComponent>()){
                    sceneRootObject->addComponent<EntityPropertiesComponent>();
                }
            }
        }
    }
}

void Scene::setPreferredCamera(PCamera cam, bool applyToScreen){
    preferredCamera = cam;
    if(!applyToScreen){
        return;
    }
    auto screen = getMainScreen();
    if(screen && cam){
        screen->setCamera(cam);
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
    NeoECS::GameObject* targetParent = parent;
    if(!targetParent){
        targetParent = sceneRootObject;
    }
    NeoECS::GameObject* object = ecsAPI->CreateGameObjectAndInstantiate(name, targetParent);
    if(object && !object->getComponent<EntityPropertiesComponent>()){
        object->addComponent<EntityPropertiesComponent>();
    }
    return object;
}

bool Scene::destroyECSGameObject(NeoECS::GameObject* object){
    if(!ecsAPI || !object) return false;
    if(sceneRootObject &&
       object->gameobject() &&
       sceneRootObject->gameobject() &&
       object->gameobject() == sceneRootObject->gameobject()){
        return false;
    }
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
        if(model){
            renderer->modelSourceRef = model->getSourceAssetRef();
            renderer->modelForceSmoothNormals = model->getSourceForceSmoothNormals() ? 1 : 0;
        }else{
            renderer->modelSourceRef.clear();
            renderer->modelForceSmoothNormals = 0;
        }
    }

    return root;
}

NeoECS::GameObject* Scene::createLightGameObject(const std::string& name, const Light& light, NeoECS::GameObject* parent, bool syncTransform, bool syncDirection){
    auto* root = createECSGameObject(name, parent);
    if(!root) return nullptr;

    if(!syncDirection && light.type != LightType::POINT){
        syncDirection = true;
    }

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

NeoECS::GameObject* Scene::createCameraGameObject(const std::string& name, NeoECS::GameObject* parent){
    auto* root = createECSGameObject(name, parent);
    if(!root){
        return nullptr;
    }

    root->addComponent<TransformComponent>();
    root->addComponent<CameraComponent>();
    root->addComponent<BoundsComponent>();

    if(auto* cameraComp = root->getComponent<CameraComponent>()){
        if(!cameraComp->camera){
            float width = 1280.0f;
            float height = 720.0f;
            if(getWindow()){
                width = static_cast<float>(getWindow()->getWindowWidth());
                height = static_cast<float>(getWindow()->getWindowHeight());
            }
            cameraComp->camera = Camera::CreatePerspective(45.0f, Math3D::Vec2(width, height), 0.1f, 1000.0f);
        }
    }

    if(auto* bounds = root->getComponent<BoundsComponent>()){
        bounds->type = BoundsType::Sphere;
        bounds->radius = 0.5f;
    }

    return root;
}

NeoECS::ECSEntity* Scene::getSceneRootEntity() const{
    if(!sceneRootObject){
        return nullptr;
    }
    return sceneRootObject->gameobject();
}

bool Scene::isSceneRootEntity(NeoECS::ECSEntity* entity) const{
    return entity && sceneRootObject && (entity == sceneRootObject->gameobject());
}

void Scene::dispose(){
    preferredCamera.reset();
    sceneRootObject = nullptr;
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
    refreshRenderState();
}

void Scene::refreshRenderState(){
    if(!ecsInstance) return;
    auto snapshotStart = std::chrono::steady_clock::now();

    auto mainScreen = getMainScreen();
    PCamera activeCamera = mainScreen ? mainScreen->getCamera() : nullptr;

    const int backIndex = 1 - renderSnapshotIndex.load(std::memory_order_acquire);
    auto& snapshot = renderSnapshots[backIndex];
    snapshot.drawItems.clear();
    snapshot.lights.clear();

    auto* componentManager = ecsInstance->getComponentManager();
    auto& entities = ecsInstance->getEntityManager()->getEntities();
    snapshot.drawItems.reserve(entities.size());
    snapshot.lights.reserve(entities.size());
    NeoECS::ECSEntity* activeCameraEntity = nullptr;
    NeoECS::ECSEntity* firstEnabledCameraEntity = nullptr;
    NeoECS::ECSEntity* preferredEnabledCameraEntity = nullptr;
    PCamera firstEnabledCamera = nullptr;
    PCamera preferredEnabledCamera = nullptr;
    int resolvedSelectedLightIndex = -1;

    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(!entity) continue;

        auto* entityProperties = componentManager->getECSComponent<EntityPropertiesComponent>(entity);
        if(!entityProperties){
            std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(ecsInstance->getContext(), entity));
            if(wrapper && wrapper->addComponent<EntityPropertiesComponent>()){
                entityProperties = componentManager->getECSComponent<EntityPropertiesComponent>(entity);
            }
        }

        auto* transform = componentManager->getECSComponent<TransformComponent>(entity);
        auto* renderer = componentManager->getECSComponent<MeshRendererComponent>(entity);
        auto* lightComponent = componentManager->getECSComponent<LightComponent>(entity);
        auto* boundsComp = componentManager->getECSComponent<BoundsComponent>(entity);
        auto* cameraComponent = componentManager->getECSComponent<CameraComponent>(entity);
        const bool rendererActive = IsComponentActive(renderer);
        const bool lightActive = IsComponentActive(lightComponent);
        const bool boundsActive = IsComponentActive(boundsComp);
        const bool cameraActive = IsComponentActive(cameraComponent);
        const bool entityPropertiesActive = IsComponentActive(entityProperties);

        const bool needsWorld = (rendererActive && renderer && renderer->visible) ||
                                (lightActive && lightComponent && (lightComponent->syncTransform || lightComponent->syncDirection)) ||
                                (cameraComponent && cameraComponent->camera && transform);

        Math3D::Mat4 world(1.0f);
        if(needsWorld){
            world = buildWorldMatrix(entity, componentManager);
        }

        if(rendererActive && renderer && renderer->visible){
            Math3D::Mat4 base = world * renderer->localOffset.toMat4();
            bool cull = renderer->enableBackfaceCulling;

            bool hasOverrideBounds = false;
            Math3D::Vec3 overrideMin;
            Math3D::Vec3 overrideMax;
            Math3D::Vec3 localMin;
            Math3D::Vec3 localMax;
            if(boundsActive && buildLocalBoundsFromComponent(boundsComp, localMin, localMax)){
                transformAabb(base, localMin, localMax, overrideMin, overrideMax);
                hasOverrideBounds = true;
            }

            if(renderer->model){
                cull = renderer->model->isBackfaceCullingEnabled();
                const auto& parts = renderer->model->getParts();
                for(const auto& part : parts){
                    if(!part || !part->visible || !part->mesh || !part->material) continue;
                    RenderItem item;
                    item.mesh = part->mesh;
                    item.material = part->material;
                    item.model = base * part->localTransform.toMat4();
                    item.enableBackfaceCulling = cull;
                    item.isTransparent = isMaterialTransparent(item.material);
                    item.isDeferredCompatible = isDeferredCompatibleMaterial(item.material);
                    item.entityId = entity->getNodeUniqueID();
                    item.ignoreRaycastHit = (entityPropertiesActive && entityProperties && entityProperties->ignoreRaycastHit);
                    item.castsShadows = item.material->castsShadows();
                    if(hasOverrideBounds){
                        item.hasBounds = true;
                        item.boundsMin = overrideMin;
                        item.boundsMax = overrideMax;
                    }else if(item.mesh->getLocalBounds(localMin, localMax)){
                        transformAabb(item.model, localMin, localMax, item.boundsMin, item.boundsMax);
                        item.hasBounds = true;
                    }
                    snapshot.drawItems.push_back(std::move(item));
                }
            }else if(renderer->mesh && renderer->material){
                RenderItem item;
                item.mesh = renderer->mesh;
                item.material = renderer->material;
                item.model = base;
                item.enableBackfaceCulling = cull;
                item.isTransparent = isMaterialTransparent(item.material);
                item.isDeferredCompatible = isDeferredCompatibleMaterial(item.material);
                item.entityId = entity->getNodeUniqueID();
                item.ignoreRaycastHit = (entityPropertiesActive && entityProperties && entityProperties->ignoreRaycastHit);
                item.castsShadows = item.material->castsShadows();
                if(hasOverrideBounds){
                    item.hasBounds = true;
                    item.boundsMin = overrideMin;
                    item.boundsMax = overrideMax;
                }else if(item.mesh->getLocalBounds(localMin, localMax)){
                    transformAabb(item.model, localMin, localMax, item.boundsMin, item.boundsMax);
                    item.hasBounds = true;
                }
                snapshot.drawItems.push_back(std::move(item));
            }
        }

        if(lightActive && lightComponent){
            Light light = lightComponent->light;
            light.shadowDebugMode = Math3D::Clamp(light.shadowDebugMode, 0, 3);
            lightComponent->light.shadowDebugMode = light.shadowDebugMode;
            if(lightComponent->syncTransform){
                light.position = world.getPosition();
                lightComponent->light.position = light.position;
            }
            if(lightComponent->syncDirection){
                Math3D::Vec3 origin = world.getPosition();
                Math3D::Vec3 forward = Math3D::Transform::transformPoint(world, Math3D::Vec3(0,0,1)) - origin;
                if(std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z) &&
                   forward.length() > 0.0001f){
                    light.direction = forward.normalize();
                    lightComponent->light.direction = light.direction;
                }
            }
            if(light.type != LightType::POINT){
                if(!std::isfinite(light.direction.x) || !std::isfinite(light.direction.y) || !std::isfinite(light.direction.z)){
                    light.direction = Math3D::Vec3(0,-1,0);
                }else if(light.direction.length() < Math3D::EPSILON){
                    light.direction = Math3D::Vec3(0,-1,0);
                }else{
                    light.direction = light.direction.normalize();
                }
                lightComponent->light.direction = light.direction;
            }
            if(entity->getNodeUniqueID() == selectedEntityId){
                resolvedSelectedLightIndex = static_cast<int>(snapshot.lights.size());
            }
            snapshot.lights.push_back(light);
        }

        if(cameraComponent && cameraComponent->camera){
            if(transform){
                cameraComponent->camera->setTransform(Math3D::Transform::fromMat4(world));
            }
            if(cameraActive){
                if(!firstEnabledCamera){
                    firstEnabledCamera = cameraComponent->camera;
                    firstEnabledCameraEntity = entity;
                }
                if(preferredCamera && cameraComponent->camera == preferredCamera){
                    preferredEnabledCamera = cameraComponent->camera;
                    preferredEnabledCameraEntity = entity;
                }
                if(activeCamera && cameraComponent->camera == activeCamera){
                    activeCameraEntity = entity;
                }
            }
        }
    }

    NeoECS::ECSEntity* resolvedCameraEntity = nullptr;
    PCamera resolvedCamera = nullptr;
    if(activeCameraEntity && activeCamera){
        resolvedCamera = activeCamera;
        resolvedCameraEntity = activeCameraEntity;
    }else if(preferredEnabledCamera){
        resolvedCamera = preferredEnabledCamera;
        resolvedCameraEntity = preferredEnabledCameraEntity;
    }else if(firstEnabledCamera){
        resolvedCamera = firstEnabledCamera;
        resolvedCameraEntity = firstEnabledCameraEntity;
    }

    preferredCamera = resolvedCamera;
    if(mainScreen && mainScreen->getCamera() != resolvedCamera){
        mainScreen->setCamera(resolvedCamera);
    }

    updateActiveCameraEffects(resolvedCameraEntity, componentManager);
    selectedLightUploadIndex = resolvedSelectedLightIndex;

    renderSnapshotIndex.store(backIndex, std::memory_order_release);

    auto snapshotEnd = std::chrono::steady_clock::now();
    std::chrono::duration<float, std::milli> snapshotMs = snapshotEnd - snapshotStart;
    debugStats.snapshotMs.store(snapshotMs.count(), std::memory_order_relaxed);
    debugStats.drawCount.store(static_cast<int>(snapshot.drawItems.size()), std::memory_order_relaxed);
    debugStats.lightCount.store(static_cast<int>(snapshot.lights.size()), std::memory_order_relaxed);
}

void Scene::renderViewportContents(){
    render3DPass();
}

void Scene::ApplyCameraEffectsToScreen(
    PScreen screen,
    NeoECS::ECSComponentManager* manager,
    NeoECS::ECSEntity* cameraEntity,
    bool clearExisting,
    NeoECS::ECSEntityManager* entityManager
){
    (void)entityManager;
    if(!screen){
        return;
    }
    if(clearExisting){
        screen->clearEffects();
    }
    if(!cameraEntity || !manager){
        return;
    }

    auto env = screen->getEnvironment();
    auto* camComponent = manager->getECSComponent<CameraComponent>(cameraEntity);
    if(!camComponent || !camComponent->camera || !IsComponentActive(camComponent)){
        if(env){
            env->setSkyBox(nullptr);
        }
        return;
    }

    const CameraSettings& settings = camComponent->camera->getSettings();

    auto* autoExposure = manager->getECSComponent<AutoExposureComponent>(cameraEntity);
    Graphics::PostProcessing::PPostProcessingEffect autoExposureEffect = nullptr;
    if(IsComponentActive(autoExposure)){
        autoExposureEffect = autoExposure->getEffectForCamera(settings);
    }

    if(auto* skybox = manager->getECSComponent<SkyboxComponent>(cameraEntity)){
        if(IsComponentActive(skybox)){
            if(skybox->skyboxAssetRef.empty()){
                skybox->loadedSkyboxAssetRef.clear();
                skybox->runtimeSkyBox.reset();
                if(env){
                    env->setSkyBox(nullptr);
                }
            }else{
                if(!skybox->runtimeSkyBox || skybox->loadedSkyboxAssetRef != skybox->skyboxAssetRef){
                    std::string error;
                    auto runtimeSkybox = SkyboxAssetIO::InstantiateSkyBoxFromRef(skybox->skyboxAssetRef, &error);
                    if(!runtimeSkybox){
                        if(!error.empty()){
                            LogBot.Log(LOG_WARN, "Failed to load skybox '%s': %s", skybox->skyboxAssetRef.c_str(), error.c_str());
                        }
                        skybox->loadedSkyboxAssetRef.clear();
                        skybox->runtimeSkyBox.reset();
                    }else{
                        skybox->runtimeSkyBox = runtimeSkybox;
                        skybox->loadedSkyboxAssetRef = skybox->skyboxAssetRef;
                    }
                }

                if(env && skybox->runtimeSkyBox){
                    env->setSkyBox(skybox->runtimeSkyBox);
                }
            }
        }else if(env){
            env->setSkyBox(nullptr);
        }
    }

    if(auto* ssao = manager->getECSComponent<SSAOComponent>(cameraEntity)){
        if(IsComponentActive(ssao)){
            auto effect = ssao->getEffectForCamera(settings);
            if(effect){
                screen->addEffect(effect);
            }
        }
    }

    if(auto* bloom = manager->getECSComponent<BloomComponent>(cameraEntity)){
        if(IsComponentActive(bloom)){
            auto effect = bloom->getEffectForCamera(settings);
            if(effect){
                if(autoExposureEffect){
                    autoExposure->applyBloomCoupling(bloom);
                }
                screen->addEffect(effect);
            }
        }
    }

    if(auto* dof = manager->getECSComponent<DepthOfFieldComponent>(cameraEntity)){
        if(IsComponentActive(dof)){
            auto effect = dof->getEffectForCamera(settings);
            if(effect){
                screen->addEffect(effect);
            }
        }
    }

    if(autoExposureEffect){
        screen->addEffect(autoExposureEffect);
    }

    if(auto* aa = manager->getECSComponent<AntiAliasingComponent>(cameraEntity)){
        if(IsComponentActive(aa)){
            auto effect = aa->getEffectForCamera(settings);
            if(effect){
                screen->addEffect(effect);
            }
        }
    }
}

bool Scene::computeAdaptiveFocusDistanceFromSnapshot(NeoECS::ECSEntity* cameraEntity, const PCamera& camera, float& outDistance) const{
    if(!camera){
        return false;
    }

    Math3D::Transform cameraTransform = camera->transform();
    Math3D::Vec3 origin = cameraTransform.position;
    Math3D::Vec3 forward = cameraTransform.forward() * -1.0f;
    if(!std::isfinite(forward.x) || !std::isfinite(forward.y) || !std::isfinite(forward.z) ||
       forward.length() < 0.0001f){
        return false;
    }
    forward = forward.normalize();

    Math3D::Vec3 right = cameraTransform.right();
    if(!std::isfinite(right.x) || !std::isfinite(right.y) || !std::isfinite(right.z) || right.length() < 0.0001f){
        right = Math3D::Vec3::cross(forward, Math3D::Vec3::up());
        if(right.length() < 0.0001f){
            right = Math3D::Vec3::cross(forward, Math3D::Vec3::right());
        }
    }
    if(right.length() < 0.0001f){
        return false;
    }
    right = right.normalize();

    Math3D::Vec3 up = cameraTransform.up();
    if(!std::isfinite(up.x) || !std::isfinite(up.y) || !std::isfinite(up.z) || up.length() < 0.0001f){
        up = Math3D::Vec3::cross(right, forward);
    }
    if(up.length() < 0.0001f){
        up = Math3D::Vec3::up();
    }
    up = up.normalize();

    // Re-orthonormalize basis to keep tap rays stable when camera transform is noisy.
    right = Math3D::Vec3::cross(forward, up);
    if(right.length() < 0.0001f){
        return false;
    }
    right = right.normalize();
    up = Math3D::Vec3::cross(right, forward);
    if(up.length() < 0.0001f){
        return false;
    }
    up = up.normalize();

    const std::string cameraId = cameraEntity ? cameraEntity->getNodeUniqueID() : std::string();
    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    auto findNearestHitDistance = [&](const Math3D::Vec3& rayOrigin,
                                      const Math3D::Vec3& rayDirection,
                                      float& outHitDistance) -> bool {
        float nearestDistance = FLT_MAX;
        bool found = false;
        for(const auto& item : snapshot.drawItems){
            if(!item.hasBounds){
                continue;
            }
            if(item.ignoreRaycastHit){
                continue;
            }
            if(!cameraId.empty() && item.entityId == cameraId){
                continue;
            }

            float hitDistance = 0.0f;
            if(rayIntersectsAabb(rayOrigin, rayDirection, item.boundsMin, item.boundsMax, hitDistance) &&
               hitDistance >= 0.01f &&
               hitDistance < nearestDistance){
                nearestDistance = hitDistance;
                found = true;
            }
        }
        if(!found || !std::isfinite(nearestDistance)){
            return false;
        }
        outHitDistance = nearestDistance;
        return true;
    };

    struct AdaptiveFocusTap {
        float x;
        float y;
        float weight;
    };
    // Normalized screen-space tap pattern around the center (NDC-like offsets).
    static const std::array<AdaptiveFocusTap, 9> kAdaptiveFocusTaps = {{
        { 0.000f,  0.000f, 2.00f},
        { 0.020f,  0.000f, 1.00f},
        {-0.020f,  0.000f, 1.00f},
        { 0.000f,  0.020f, 1.00f},
        { 0.000f, -0.020f, 1.00f},
        { 0.014f,  0.014f, 0.85f},
        {-0.014f,  0.014f, 0.85f},
        { 0.014f, -0.014f, 0.85f},
        {-0.014f, -0.014f, 0.85f}
    }};

    const CameraSettings cameraSettings = camera->getSettings();
    const bool isOrtho = cameraSettings.isOrtho;
    const float safeAspect = Math3D::Max(std::abs(cameraSettings.aspect), 0.001f);
    const float tanHalfFov = std::tan(glm::radians(Math3D::Clamp(cameraSettings.fov, 1.0f, 179.0f) * 0.5f));
    const float orthoHalfWidth = Math3D::Max(std::abs(cameraSettings.viewPlane.size.x) * 0.5f, 0.001f);
    const float orthoHalfHeight = Math3D::Max(std::abs(cameraSettings.viewPlane.size.y) * 0.5f, 0.001f);

    auto sampleTap = [&](const AdaptiveFocusTap& tap, float& outTapDistance) -> bool {
        Math3D::Vec3 tapOrigin = origin;
        Math3D::Vec3 tapDirection = forward;
        if(isOrtho){
            tapOrigin += (right * (tap.x * orthoHalfWidth)) + (up * (tap.y * orthoHalfHeight));
        }else{
            float offsetX = tap.x * safeAspect * tanHalfFov;
            float offsetY = tap.y * tanHalfFov;
            tapDirection = (forward + (right * offsetX) + (up * offsetY));
            if(tapDirection.length() < 0.0001f){
                return false;
            }
            tapDirection = tapDirection.normalize();
        }
        return findNearestHitDistance(tapOrigin, tapDirection, outTapDistance);
    };

    float centerDistance = 0.0f;
    bool centerHit = sampleTap(kAdaptiveFocusTaps[0], centerDistance);

    float weightedDistanceSum = 0.0f;
    float totalWeight = 0.0f;
    int hitCount = 0;

    if(centerHit){
        const float centerWeight = 4.5f;
        weightedDistanceSum = centerDistance * centerWeight;
        totalWeight = centerWeight;
        hitCount = 1;

        // Keep peripheral taps only if they are close to the center hit distance.
        // This prevents far background/sky taps from stealing focus on small center subjects.
        const float inlierThreshold = Math3D::Max(0.35f, centerDistance * 0.45f);
        for(size_t i = 1; i < kAdaptiveFocusTaps.size(); ++i){
            float tapDistance = 0.0f;
            if(!sampleTap(kAdaptiveFocusTaps[i], tapDistance)){
                continue;
            }

            float delta = std::abs(tapDistance - centerDistance);
            if(delta > inlierThreshold){
                continue;
            }

            float proximity = 1.0f - Math3D::Clamp(delta / inlierThreshold, 0.0f, 1.0f);
            float adjustedWeight = kAdaptiveFocusTaps[i].weight * (0.35f + (0.65f * proximity));
            weightedDistanceSum += tapDistance * adjustedWeight;
            totalWeight += adjustedWeight;
            hitCount++;
        }
    }else{
        for(const auto& tap : kAdaptiveFocusTaps){
            float tapDistance = 0.0f;
            if(!sampleTap(tap, tapDistance)){
                continue;
            }

            // Without a center hit, bias toward nearer hits to avoid locking onto far background.
            float distanceBias = 1.0f / (1.0f + (tapDistance * 0.08f));
            float adjustedWeight = tap.weight * distanceBias;
            weightedDistanceSum += tapDistance * adjustedWeight;
            totalWeight += adjustedWeight;
            hitCount++;
        }
    }

    if(hitCount == 0 || totalWeight <= 0.0001f){
        return false;
    }

    outDistance = weightedDistanceSum / totalWeight;
    return std::isfinite(outDistance) && outDistance >= 0.01f;
}

bool Scene::computeAdaptiveFocusDistanceFromSnapshotForCamera(const PCamera& camera, float& outDistance) const{
    return computeAdaptiveFocusDistanceFromSnapshot(nullptr, camera, outDistance);
}

void Scene::applyCameraEffectsToScreen(PScreen screen,
                                       NeoECS::ECSEntity* cameraEntity,
                                       bool clearExisting,
                                       const Scene* adaptiveFocusSourceScene){
    NeoECS::ECSComponentManager* manager = ecsInstance ? ecsInstance->getComponentManager() : nullptr;
    NeoECS::ECSEntityManager* entityManager = ecsInstance ? ecsInstance->getEntityManager() : nullptr;
    const Scene* focusSource = adaptiveFocusSourceScene ? adaptiveFocusSourceScene : this;

    if(manager && cameraEntity){
        auto* camComponent = manager->getECSComponent<CameraComponent>(cameraEntity);
        auto* dof = manager->getECSComponent<DepthOfFieldComponent>(cameraEntity);
        if(camComponent && IsComponentActive(camComponent) &&
           camComponent->camera &&
           dof && IsComponentActive(dof) &&
           dof->adaptiveFocus &&
           dof->runtimeEffect){
            float adaptiveFocusDistance = 0.0f;
            bool hasAdaptiveFocus = false;
            if(focusSource == this){
                hasAdaptiveFocus = computeAdaptiveFocusDistanceFromSnapshot(cameraEntity, camComponent->camera, adaptiveFocusDistance);
            }else{
                hasAdaptiveFocus = focusSource->computeAdaptiveFocusDistanceFromSnapshotForCamera(camComponent->camera, adaptiveFocusDistance);
            }
            if(hasAdaptiveFocus){
                dof->runtimeEffect->externalAdaptiveFocusDistance = adaptiveFocusDistance;
                dof->runtimeEffect->externalAdaptiveFocusValid = true;
            }else{
                dof->runtimeEffect->externalAdaptiveFocusValid = false;
            }
        }else if(dof && dof->runtimeEffect){
            dof->runtimeEffect->externalAdaptiveFocusValid = false;
        }
    }

    ApplyCameraEffectsToScreen(screen, manager, cameraEntity, clearExisting, entityManager);
}

void Scene::updateActiveCameraEffects(NeoECS::ECSEntity* activeCameraEntity, NeoECS::ECSComponentManager* manager){
    (void)manager;
    applyCameraEffectsToScreen(getMainScreen(), activeCameraEntity, true);
}

void Scene::updateSceneLights(){
    auto screen = getMainScreen();
    if(!screen){
        ShadowRenderer::SetSelectedLightIndex(-1);
        return;
    }

    auto env = screen->getEnvironment();
    if(!env){
        ShadowRenderer::SetSelectedLightIndex(-1);
        return;
    }

    auto& lightManager = env->getLightManager();
    lightManager.clearLights();
    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    for(const auto& light : snapshot.lights){
        lightManager.addLight(light);
    }

    const int uploadedCount = static_cast<int>(lightManager.getLightCount());
    const int selectedIndex =
        (selectedLightUploadIndex >= 0 && selectedLightUploadIndex < uploadedCount)
            ? selectedLightUploadIndex
            : -1;
    ShadowRenderer::SetSelectedLightIndex(selectedIndex);
}

bool Scene::isMaterialTransparent(const std::shared_ptr<Material>& material) const{
    if(!material) return false;

    if(auto pbr = Material::GetAs<PBRMaterial>(material)){
        if(pbr->UseAlphaClip.get() != 0){
            return false;
        }
        return pbr->BaseColor.get().w < 0.999f;
    }

    if(auto litColor = Material::GetAs<MaterialDefaults::LitColorMaterial>(material)){
        return litColor->Color.get().w < 0.999f;
    }
    if(auto litImage = Material::GetAs<MaterialDefaults::LitImageMaterial>(material)){
        return litImage->Color.get().w < 0.999f;
    }
    if(auto flatColor = Material::GetAs<MaterialDefaults::FlatColorMaterial>(material)){
        return flatColor->Color.get().w < 0.999f;
    }
    if(auto flatImage = Material::GetAs<MaterialDefaults::FlatImageMaterial>(material)){
        return flatImage->Color.get().w < 0.999f;
    }
    if(auto colorMat = Material::GetAs<MaterialDefaults::ColorMaterial>(material)){
        return colorMat->Color.get().w < 0.999f;
    }
    if(auto imageMat = Material::GetAs<MaterialDefaults::ImageMaterial>(material)){
        return imageMat->Color.get().w < 0.999f;
    }

    return false;
}

bool Scene::isDeferredCompatibleMaterial(const std::shared_ptr<Material>& material) const{
    if(!material){
        return false;
    }

    // Deferred path currently supports PBR data end-to-end.
    // All other material families are rendered by forward fallback.
    return (Material::GetAs<PBRMaterial>(material) != nullptr);
}

void Scene::ensureDeferredResources(PScreen screen){
    if(!screen) return;

    int w = screen->getWidth();
    int h = screen->getHeight();
    if(!gBuffer){
        gBuffer = FrameBuffer::CreateGBuffer(w, h);
        gBufferWidth = w;
        gBufferHeight = h;
        gBufferValidationDirty = true;
    }else if(gBufferWidth != w || gBufferHeight != h){
        gBuffer->resize(w, h);
        gBufferWidth = w;
        gBufferHeight = h;
        gBufferValidationDirty = true;
    }

    if(!gBufferShader){
        auto vertexShader = AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Lit.vert");
        auto fragmentShader = AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_GBuffer.frag");
        if(vertexShader && fragmentShader){
            gBufferShader = ShaderCacheManager::INSTANCE.getOrCompile("GBufferPass_v1", vertexShader->asString(), fragmentShader->asString());
            if(gBufferShader && gBufferShader->getID() == 0){
                LogBot.Log(LOG_ERRO, "Failed to link GBufferPass shader: \n%s", gBufferShader->getLog().c_str());
            }
        }else{
            LogBot.Log(LOG_ERRO, "GBufferPass shader assets missing.");
        }
    }

    if(!deferredLightShader){
        auto vertexShader = AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Default.vert");
        auto fragmentShader = AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_DeferredLight.frag");
        if(vertexShader && fragmentShader){
            deferredLightShader = ShaderCacheManager::INSTANCE.getOrCompile("DeferredLightPass_v4", vertexShader->asString(), fragmentShader->asString());
            if(deferredLightShader && deferredLightShader->getID() == 0){
                LogBot.Log(LOG_ERRO, "Failed to link DeferredLightPass shader: \n%s", deferredLightShader->getLog().c_str());
            }
        }else{
            LogBot.Log(LOG_ERRO, "DeferredLightPass shader assets missing.");
        }
    }

    if(!deferredQuad){
        try{
            auto factory = ModelPartFactory::Create(nullptr); 
            int v1, v2, v3, v4;

            factory
                .addVertex(Vertex::Build(Math3D::Vec3(-1.0f, -1.0f, 0.0f)).UV(0, 0), &v1)
                .addVertex(Vertex::Build(Math3D::Vec3( 1.0f, -1.0f, 0.0f)).UV(1, 0), &v2)
                .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,  1.0f, 0.0f)).UV(1, 1), &v3)
                .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,  1.0f, 0.0f)).UV(0, 1), &v4)
                .defineFace(v1, v2, v3, v4);

            deferredQuad = factory.assemble();
        }catch(const std::exception& e){
            LogBot.Log(LOG_ERRO, "Deferred quad creation failed: %s", e.what());
        }catch(...){
            LogBot.Log(LOG_ERRO, "Deferred quad creation failed: unknown exception");
        }
    }
}

void Scene::drawDeferredGeometry(PCamera cam){
    if(!cam || !gBuffer || !gBufferShader || gBufferShader->getID() == 0) return;

    gBuffer->bind();
    gBuffer->clear(Color::CLEAR);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    gBufferShader->bind();
    const Math3D::Mat4 viewMatrix = cam->getViewMatrix();
    const Math3D::Mat4 projectionMatrix = cam->getProjectionMatrix();
    gBufferShader->setUniformFast("u_view", Uniform<Math3D::Mat4>(viewMatrix));
    gBufferShader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(projectionMatrix));
    gBufferShader->setUniformFast("u_viewPos", Uniform<Math3D::Vec3>(cam->transform().position));

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    std::vector<const RenderItem*> deferredItems;
    deferredItems.reserve(snapshot.drawItems.size());
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material) continue;
        if(item.isTransparent) continue;
        if(!item.isDeferredCompatible) continue;
        deferredItems.push_back(&item);
    }
    std::sort(deferredItems.begin(), deferredItems.end(), [](const RenderItem* a, const RenderItem* b){
        if(a->enableBackfaceCulling != b->enableBackfaceCulling){
            return a->enableBackfaceCulling > b->enableBackfaceCulling;
        }
        auto shaderA = a->material ? a->material->getShader().get() : nullptr;
        auto shaderB = b->material ? b->material->getShader().get() : nullptr;
        if(shaderA != shaderB){
            return shaderA < shaderB;
        }
        if(a->material.get() != b->material.get()){
            return a->material.get() < b->material.get();
        }
        return a->mesh.get() < b->mesh.get();
    });
    std::shared_ptr<Material> lastMaterial = nullptr;
    bool cullStateKnown = false;
    bool cullEnabled = true;

    for(const RenderItem* itemPtr : deferredItems){
        if(!itemPtr) continue;
        const auto& item = *itemPtr;

        if(!cullStateKnown || cullEnabled != item.enableBackfaceCulling){
            if(item.enableBackfaceCulling){
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
                cullEnabled = true;
            }else{
                glDisable(GL_CULL_FACE);
                cullEnabled = false;
            }
            cullStateKnown = true;
        }

        gBufferShader->setUniformFast("u_model", Uniform<Math3D::Mat4>(item.model));

        if(item.material != lastMaterial){
            const auto& material = item.material;
            Math3D::Vec4 baseColor = Color::WHITE;
            PTexture baseColorTex = nullptr;
            int useBaseColorTex = 0;
            float metallic = 0.0f;
            float roughness = 1.0f;
            PTexture roughnessTex = nullptr;
            int useRoughnessTex = 0;
            PTexture metallicRoughnessTex = nullptr;
            int useMetallicRoughnessTex = 0;
            PTexture normalTex = nullptr;
            int useNormalTex = 0;
            float normalScale = 1.0f;
            PTexture heightTex = nullptr;
            int useHeightTex = 0;
            float heightScale = 0.05f;
            PTexture occlusionTex = nullptr;
            int useOcclusionTex = 0;
            float aoStrength = 1.0f;
            Math3D::Vec2 uvScale(1.0f, 1.0f);
            Math3D::Vec2 uvOffset(0.0f, 0.0f);
            int useAlphaClip = 0;
            float alphaCutoff = 0.5f;
            int surfaceMode = 0; // 0=PBR, 1=LegacyLit, 2=LegacyUnlit

            if(auto pbr = Material::GetAs<PBRMaterial>(material)){
                baseColor = pbr->BaseColor.get();
                baseColorTex = pbr->BaseColorTex.get();
                useBaseColorTex = baseColorTex ? 1 : 0;
                metallic = pbr->Metallic.get();
                roughness = pbr->Roughness.get();
                roughnessTex = pbr->RoughnessTex.get();
                useRoughnessTex = roughnessTex ? 1 : 0;
                metallicRoughnessTex = pbr->MetallicRoughnessTex.get();
                useMetallicRoughnessTex = metallicRoughnessTex ? 1 : 0;
                normalTex = pbr->NormalTex.get();
                useNormalTex = normalTex ? 1 : 0;
                normalScale = pbr->NormalScale.get();
                heightTex = pbr->HeightTex.get();
                useHeightTex = heightTex ? 1 : 0;
                heightScale = pbr->HeightScale.get();
                occlusionTex = pbr->OcclusionTex.get();
                useOcclusionTex = occlusionTex ? 1 : 0;
                aoStrength = pbr->OcclusionStrength.get();
                uvScale = pbr->UVScale.get();
                uvOffset = pbr->UVOffset.get();
                useAlphaClip = pbr->UseAlphaClip.get();
                alphaCutoff = pbr->AlphaCutoff.get();
                surfaceMode = 0;
            }else if(auto litImage = Material::GetAs<MaterialDefaults::LitImageMaterial>(material)){
                baseColor = litImage->Color.get();
                baseColorTex = litImage->Tex.get();
                useBaseColorTex = baseColorTex ? 1 : 0;
                roughness = 0.85f;
                surfaceMode = 1;
            }else if(auto flatImage = Material::GetAs<MaterialDefaults::FlatImageMaterial>(material)){
                baseColor = flatImage->Color.get();
                baseColorTex = flatImage->Tex.get();
                useBaseColorTex = baseColorTex ? 1 : 0;
                roughness = 0.95f;
                surfaceMode = 1;
            }else if(auto imageMat = Material::GetAs<MaterialDefaults::ImageMaterial>(material)){
                baseColor = imageMat->Color.get();
                baseColorTex = imageMat->Tex.get();
                useBaseColorTex = baseColorTex ? 1 : 0;
                surfaceMode = 2;
            }else if(auto litColor = Material::GetAs<MaterialDefaults::LitColorMaterial>(material)){
                baseColor = litColor->Color.get();
                roughness = 0.85f;
                surfaceMode = 1;
            }else if(auto flatColor = Material::GetAs<MaterialDefaults::FlatColorMaterial>(material)){
                baseColor = flatColor->Color.get();
                roughness = 0.95f;
                surfaceMode = 1;
            }else if(auto colorMat = Material::GetAs<MaterialDefaults::ColorMaterial>(material)){
                baseColor = colorMat->Color.get();
                surfaceMode = 2;
            }else{
                // Unknown material type fallback to the legacy lit path.
                roughness = 0.9f;
                surfaceMode = 1;
            }

            gBufferShader->setUniformFast("u_baseColor", Uniform<Math3D::Vec4>(baseColor));
            gBufferShader->setUniformFast("u_useBaseColorTex", Uniform<int>(useBaseColorTex));
            gBufferShader->setUniformFast("u_baseColorTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(baseColorTex, 0)));

            gBufferShader->setUniformFast("u_metallic", Uniform<float>(metallic));
            gBufferShader->setUniformFast("u_roughness", Uniform<float>(roughness));
            gBufferShader->setUniformFast("u_useRoughnessTex", Uniform<int>(useRoughnessTex));
            gBufferShader->setUniformFast("u_roughnessTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(roughnessTex, 5)));
            gBufferShader->setUniformFast("u_useMetallicRoughnessTex", Uniform<int>(useMetallicRoughnessTex));
            gBufferShader->setUniformFast("u_metallicRoughnessTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(metallicRoughnessTex, 1)));

            gBufferShader->setUniformFast("u_useNormalTex", Uniform<int>(useNormalTex));
            gBufferShader->setUniformFast("u_normalScale", Uniform<float>(normalScale));
            gBufferShader->setUniformFast("u_normalTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(normalTex, 2)));
            gBufferShader->setUniformFast("u_useHeightTex", Uniform<int>(useHeightTex));
            gBufferShader->setUniformFast("u_heightScale", Uniform<float>(heightScale));
            gBufferShader->setUniformFast("u_heightTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(heightTex, 4)));

            gBufferShader->setUniformFast("u_useOcclusionTex", Uniform<int>(useOcclusionTex));
            gBufferShader->setUniformFast("u_aoStrength", Uniform<float>(aoStrength));
            gBufferShader->setUniformFast("u_occlusionTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(occlusionTex, 3)));

            gBufferShader->setUniformFast("u_uvScale", Uniform<Math3D::Vec2>(uvScale));
            gBufferShader->setUniformFast("u_uvOffset", Uniform<Math3D::Vec2>(uvOffset));
            gBufferShader->setUniformFast("u_useAlphaClip", Uniform<int>(useAlphaClip));
            gBufferShader->setUniformFast("u_alphaCutoff", Uniform<float>(alphaCutoff));
            gBufferShader->setUniformFast("u_surfaceMode", Uniform<int>(surfaceMode));
            lastMaterial = material;
        }

        item.mesh->draw();
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    gBufferShader->unbind();
    gBuffer->unbind();
}

void Scene::drawDeferredLighting(PScreen screen, PCamera cam){
    if(!screen || !cam || !gBuffer || !deferredLightShader || deferredLightShader->getID() == 0 || !deferredQuad) return;

    auto drawBuffer = screen->getDrawBuffer();
    if(!drawBuffer) return;

    drawBuffer->bind();
    drawBuffer->clear(screen->getClearColor());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    deferredLightShader->bind();

    deferredLightShader->setUniformFast("u_viewPos", Uniform<Math3D::Vec3>(cam->transform().position));
    deferredLightShader->setUniformFast("u_cameraView", Uniform<Math3D::Mat4>(cam->getViewMatrix()));
    deferredLightShader->setUniformFast("gAlbedo", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(gBuffer->getGBufferTexture(0), 0)));
    deferredLightShader->setUniformFast("gNormal", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(gBuffer->getGBufferTexture(1), 1)));
    deferredLightShader->setUniformFast("gPosition", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(gBuffer->getGBufferTexture(2), 2)));

    auto env = screen->getEnvironment();
    PCubeMap envMap = (env && env->getSkyBox()) ? env->getSkyBox()->getCubeMap() : nullptr;
    deferredLightShader->setUniformFast("u_useEnvMap", Uniform<int>(envMap ? 1 : 0));
    deferredLightShader->setUniformFast("u_envStrength", Uniform<float>(0.55f));
    deferredLightShader->setUniformFast("u_envMap", Uniform<GLUniformUpload::CubeMapSlot>(GLUniformUpload::CubeMapSlot(envMap, 5)));
    static const std::vector<Light> EMPTY_LIGHTS;
    const std::vector<Light>& lights = env ? env->getLightsForUpload() : EMPTY_LIGHTS;
    LightUniformUploader::UploadLights(deferredLightShader, lights);
    ShadowRenderer::BindShadowSamplers(deferredLightShader);

    static const Math3D::Mat4 IDENTITY;
    deferredLightShader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
    deferredLightShader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
    deferredLightShader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));

    deferredQuad->draw(IDENTITY, IDENTITY, IDENTITY);
}

void Scene::drawOutlines(PCamera cam){
    if(!cam) return;
    if(!outlineEnabled || selectedEntityId.empty()) return;

    if(!outlineMaterial){
        outlineMaterial = MaterialDefaults::ColorMaterial::Create(Color::fromRGBA32(0x3498dbFF));
    }

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    const Math3D::Mat4 viewMatrix = cam->getViewMatrix();
    const Math3D::Mat4 projectionMatrix = cam->getProjectionMatrix();
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material) continue;
        if(item.entityId != selectedEntityId) continue;

        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);

        glm::mat4 outline = (glm::mat4)item.model;
        outline = outline * glm::scale(glm::mat4(1.0f), glm::vec3(1.03f));
        Math3D::Mat4 outlineModel(outline);

        outlineMaterial->set<Math3D::Mat4>("u_model", outlineModel);
        outlineMaterial->set<Math3D::Mat4>("u_view", viewMatrix);
        outlineMaterial->set<Math3D::Mat4>("u_projection", projectionMatrix);
        outlineMaterial->bind();
        item.mesh->draw();

        glCullFace(GL_BACK);
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void Scene::renderDeferred(PScreen screen, PCamera cam){
    if(deferredDisabled){
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    ensureDeferredResources(screen);
    static bool loggedInvalid = false;
    bool invalid = false;
    if(!gBuffer || gBuffer->getGBufferCount() < 3){
        invalid = true;
    }else if(!gBuffer->getGBufferTexture(0) || !gBuffer->getGBufferTexture(1) || !gBuffer->getGBufferTexture(2)){
        invalid = true;
    }

    if(!gBufferShader || gBufferShader->getID() == 0){
        invalid = true;
    }
    if(!deferredLightShader || deferredLightShader->getID() == 0){
        invalid = true;
    }
    if(!deferredQuad){
        invalid = true;
    }

    if(invalid){
        if(!loggedInvalid){
            LogBot.Log(LOG_ERRO, "Deferred rendering unavailable; falling back to forward rendering.");
            loggedInvalid = true;
        }
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    if(gBufferValidationDirty){
        gBufferValidated = gBuffer->validate();
        gBufferValidationDirty = false;
    }

    if(!gBufferValidated){
        if(!loggedInvalid){
            LogBot.Log(LOG_ERRO, "GBuffer framebuffer invalid; falling back to forward rendering.");
            loggedInvalid = true;
        }
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    constexpr bool kDeferredGlErrorChecks = false;
    if(kDeferredGlErrorChecks){
        while(glGetError() != GL_NO_ERROR) {}
    }

    auto checkGlError = [&](const char* stage){
        if(!kDeferredGlErrorChecks){
            return;
        }
        GLenum err = glGetError();
        if(err != GL_NO_ERROR){
            LogBot.Log(LOG_ERRO, "[Deferred] GL error after %s: 0x%X", stage, err);
            deferredDisabled = true;
        }
    };

    drawDeferredGeometry(cam);
    checkGlError("geometry pass");
    if(deferredDisabled){
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    drawDeferredLighting(screen, cam);
    checkGlError("lighting pass");
    if(deferredDisabled){
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    auto drawBuffer = screen ? screen->getDrawBuffer() : nullptr;
    if(drawBuffer){
        bool copiedDepth = false;
        auto gBufferDepth = gBuffer->getDepthTexture();
        auto drawDepth = drawBuffer->getDepthTexture();
        if(gBufferDepth && drawDepth &&
           gBufferDepth->getID() != 0 && drawDepth->getID() != 0 &&
           gBufferWidth == drawBuffer->getWidth() &&
           gBufferHeight == drawBuffer->getHeight()){
            if(glCopyImageSubData){
                glCopyImageSubData(
                    gBufferDepth->getID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                    drawDepth->getID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                    gBufferWidth, gBufferHeight, 1
                );
                copiedDepth = (glGetError() == GL_NO_ERROR);
            }
        }

        if(!copiedDepth){
            // Depth writes must be enabled for the depth blit to populate the main draw buffer.
            glDepthMask(GL_TRUE);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer->getID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawBuffer->getID());
            glBlitFramebuffer(
                0, 0, gBufferWidth, gBufferHeight,
                0, 0, drawBuffer->getWidth(), drawBuffer->getHeight(),
                GL_DEPTH_BUFFER_BIT, GL_NEAREST
            );
        }
        glBindFramebuffer(GL_FRAMEBUFFER, drawBuffer->getID());
    }
    checkGlError("depth blit");
    if(deferredDisabled){
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // Fallback opaque pass for non-deferred materials (existing forward shaders).
    glDisable(GL_BLEND);
    drawModels3D(cam, RenderFilter::Opaque, false, true);
    checkGlError("forward fallback opaque pass");
    if(deferredDisabled){
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    drawSkybox(cam, true);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawModels3D(cam, RenderFilter::Transparent, false);
    glDisable(GL_BLEND);
    checkGlError("transparent pass");
    if(deferredDisabled){
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    drawOutlines(cam);
}

void Scene::render3DPass(){
    auto screen = getMainScreen();
    if(!screen) return;

    screen->bind();

    updateSceneLights();

    auto cam = screen->getCamera();
    if(cam){
        const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
        const auto& snapshot = renderSnapshots[frontIndex];
        std::vector<ShadowCasterBounds> casterBounds;
        casterBounds.reserve(snapshot.drawItems.size());
        for(const auto& item : snapshot.drawItems){
            if(item.castsShadows && item.hasBounds){
                ShadowCasterBounds bounds;
                bounds.min = item.boundsMin;
                bounds.max = item.boundsMax;
                casterBounds.push_back(bounds);
            }
        }

        ShadowRenderer::BeginFrame(cam, &casterBounds);

        auto shadowStart = std::chrono::steady_clock::now();
        drawShadowsPass();
        auto shadowEnd = std::chrono::steady_clock::now();
        std::chrono::duration<float, std::milli> shadowMs = shadowEnd - shadowStart;
        debugStats.shadowMs.store(shadowMs.count(), std::memory_order_relaxed);

        bool useDeferred = (GameEngine::Engine && GameEngine::Engine->getRenderStrategy() == EngineRenderStrategy::Deferred);
        auto drawStart = std::chrono::steady_clock::now();
        if(useDeferred){
            renderDeferred(screen, cam);
        }else{
            drawSkybox(cam);
            drawModels3D(cam);
        }
        auto drawEnd = std::chrono::steady_clock::now();
        std::chrono::duration<float, std::milli> drawMs = drawEnd - drawStart;
        debugStats.drawMs.store(drawMs.count(), std::memory_order_relaxed);
    }

    screen->unbind();
    debugStats.postFxMs.store(screen->getLastPostProcessMs(), std::memory_order_relaxed);
    debugStats.postFxEffectCount.store(screen->getLastPostProcessEffectCount(), std::memory_order_relaxed);
}

void Scene::drawModels3D(PCamera cam, RenderFilter filter, bool drawOutlines, bool skipDeferredCompatible){
    if(!cam) return;

    if(!outlineMaterial){
        outlineMaterial = MaterialDefaults::ColorMaterial::Create(Color::fromRGBA32(0x3498dbFF));
    }

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    const Math3D::Mat4 viewMatrix = cam->getViewMatrix();
    const Math3D::Mat4 projectionMatrix = cam->getProjectionMatrix();
    const bool markDeferredIncompatible = (GameEngine::Engine &&
                                           GameEngine::Engine->getRenderStrategy() == EngineRenderStrategy::Deferred);
    bool cullStateKnown = false;
    bool cullEnabled = true;
    std::shared_ptr<Material> lastBoundMaterial = nullptr;
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material) continue;

        if(filter == RenderFilter::Opaque && item.isTransparent) continue;
        if(filter == RenderFilter::Transparent && !item.isTransparent) continue;
        if(skipDeferredCompatible && item.isDeferredCompatible) continue;

        std::shared_ptr<Material> drawMaterial = item.material;
        if(markDeferredIncompatible && !item.isDeferredCompatible){
            if(!deferredIncompatibleMaterial){
                deferredIncompatibleMaterial = MaterialDefaults::ColorMaterial::Create(Color::MAGENTA);
                if(deferredIncompatibleMaterial){
                    deferredIncompatibleMaterial->setCastsShadows(false);
                    deferredIncompatibleMaterial->setReceivesShadows(false);
                }
            }
            if(deferredIncompatibleMaterial){
                drawMaterial = deferredIncompatibleMaterial;
            }
        }

        if(!cullStateKnown || cullEnabled != item.enableBackfaceCulling){
            if(item.enableBackfaceCulling){
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
                cullEnabled = true;
            }else{
                glDisable(GL_CULL_FACE);
                cullEnabled = false;
            }
            cullStateKnown = true;
        }

        auto shader = drawMaterial ? drawMaterial->getShader() : nullptr;
        if(drawMaterial != lastBoundMaterial){
            drawMaterial->bind();
            lastBoundMaterial = drawMaterial;
            if(shader && shader->getID() != 0){
                shader->setUniformFast("u_view", Uniform<Math3D::Mat4>(viewMatrix));
                shader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(projectionMatrix));
            }
        }
        if(shader && shader->getID() != 0){
            shader->setUniformFast("u_model", Uniform<Math3D::Mat4>(item.model));
        }
        item.mesh->draw();

        if(drawOutlines && outlineEnabled && !selectedEntityId.empty() && item.entityId == selectedEntityId && outlineMaterial){
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);

            glm::mat4 outline = (glm::mat4)item.model;
            outline = outline * glm::scale(glm::mat4(1.0f), glm::vec3(1.0125f));
            Math3D::Mat4 outlineModel(outline);

            outlineMaterial->bind();
            auto outlineShader = outlineMaterial->getShader();
            if(outlineShader && outlineShader->getID() != 0){
                outlineShader->setUniformFast("u_view", Uniform<Math3D::Mat4>(viewMatrix));
                outlineShader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(projectionMatrix));
                outlineShader->setUniformFast("u_model", Uniform<Math3D::Mat4>(outlineModel));
            }
            item.mesh->draw();
            glCullFace(GL_BACK);
            cullEnabled = true;
            cullStateKnown = true;
            // Outline bind changed active material/program state.
            lastBoundMaterial.reset();
        }
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void Scene::drawShadowsPass(){
    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    std::vector<ShadowRenderer::ShadowDrawItem> drawItems;
    drawItems.reserve(snapshot.drawItems.size());
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material || !item.castsShadows) continue;
        ShadowRenderer::ShadowDrawItem drawItem;
        drawItem.mesh = item.mesh;
        drawItem.model = item.model;
        drawItem.material = item.material;
        drawItem.enableBackfaceCulling = item.enableBackfaceCulling;
        drawItem.hasBounds = item.hasBounds;
        drawItem.boundsMin = item.boundsMin;
        drawItem.boundsMax = item.boundsMax;
        drawItems.push_back(std::move(drawItem));
    }

    if(!drawItems.empty()){
        ShadowRenderer::RenderShadowsBatch(drawItems);
    }
}

void Scene::drawSkybox(PCamera cam, bool depthTested){
    if(!cam) return;
    auto screen = getMainScreen();
    if(!screen) return;
    auto env = screen->getEnvironment();
    if(env && env->getSkyBox()){
        env->getSkyBox()->draw(cam, depthTested);
    }
}

Math3D::Vec3 Scene::getWorldPosition(NeoECS::ECSEntity* entity) const{
    if(!entity || !ecsInstance) return Math3D::Vec3();
    auto* manager = ecsInstance->getComponentManager();
    Math3D::Mat4 world = buildWorldMatrix(entity, manager);
    return world.getPosition();
}
