#include "Scene/Scene.h"

#include "Rendering/Core/Screen.h"
#include "Rendering/Lighting/ShadowRenderer.h"
#include "Foundation/Logging/Logbot.h"
#include "Rendering/Textures/SkyBox.h"
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
        switch(bounds->type){
            case BoundsType::Box: {
                Math3D::Vec3 e = bounds->size;
                outMin = Math3D::Vec3(-e.x, -e.y, -e.z);
                outMax = Math3D::Vec3( e.x,  e.y,  e.z);
                return true;
            }
            case BoundsType::Sphere: {
                float r = bounds->radius;
                outMin = Math3D::Vec3(-r, -r, -r);
                outMax = Math3D::Vec3( r,  r,  r);
                return true;
            }
            case BoundsType::Capsule: {
                float r = bounds->radius;
                float half = bounds->height * 0.5f;
                outMin = Math3D::Vec3(-r, -(r + half), -r);
                outMax = Math3D::Vec3( r,  (r + half),  r);
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
    return ecsAPI->CreateGameObjectAndInstantiate(name, targetParent);
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

    auto snapshotStart = std::chrono::steady_clock::now();

    auto mainScreen = getMainScreen();
    PCamera activeCamera = mainScreen ? mainScreen->getCamera() : nullptr;
    if(!activeCamera && preferredCamera && mainScreen){
        mainScreen->setCamera(preferredCamera);
        activeCamera = preferredCamera;
    }

    const int backIndex = 1 - renderSnapshotIndex.load(std::memory_order_acquire);
    auto& snapshot = renderSnapshots[backIndex];
    snapshot.drawItems.clear();
    snapshot.lights.clear();

    auto* componentManager = ecsInstance->getComponentManager();
    auto& entities = ecsInstance->getEntityManager()->getEntities();
    snapshot.drawItems.reserve(entities.size());
    snapshot.lights.reserve(entities.size());
    NeoECS::ECSEntity* activeCameraEntity = nullptr;

    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(!entity) continue;

        auto* transform = componentManager->getECSComponent<TransformComponent>(entity);
        auto* renderer = componentManager->getECSComponent<MeshRendererComponent>(entity);
        auto* lightComponent = componentManager->getECSComponent<LightComponent>(entity);
        auto* boundsComp = componentManager->getECSComponent<BoundsComponent>(entity);
        auto* cameraComponent = componentManager->getECSComponent<CameraComponent>(entity);
        const bool needsWorld = (renderer && renderer->visible) ||
                                (lightComponent && (lightComponent->syncTransform || lightComponent->syncDirection)) ||
                                (cameraComponent && cameraComponent->camera && transform);

        Math3D::Mat4 world(1.0f);
        if(needsWorld){
            world = buildWorldMatrix(entity, componentManager);
        }

        if(renderer && renderer->visible){
            Math3D::Mat4 base = world * renderer->localOffset.toMat4();
            bool cull = renderer->enableBackfaceCulling;

            bool hasOverrideBounds = false;
            Math3D::Vec3 overrideMin;
            Math3D::Vec3 overrideMax;
            Math3D::Vec3 localMin;
            Math3D::Vec3 localMax;
            if(buildLocalBoundsFromComponent(boundsComp, localMin, localMax)){
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

        if(lightComponent){
            Light light = lightComponent->light;
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
            snapshot.lights.push_back(light);
        }

        if(cameraComponent && cameraComponent->camera){
            if(!preferredCamera){
                preferredCamera = cameraComponent->camera;
            }
            if(transform){
                cameraComponent->camera->setTransform(Math3D::Transform::fromMat4(world));
            }
            if(activeCamera && cameraComponent->camera == activeCamera){
                activeCameraEntity = entity;
            }
        }
    }

    updateActiveCameraEffects(activeCameraEntity, componentManager);

    renderSnapshotIndex.store(backIndex, std::memory_order_release);

    auto snapshotEnd = std::chrono::steady_clock::now();
    std::chrono::duration<float, std::milli> snapshotMs = snapshotEnd - snapshotStart;
    debugStats.snapshotMs.store(snapshotMs.count(), std::memory_order_relaxed);
    debugStats.drawCount.store(static_cast<int>(snapshot.drawItems.size()), std::memory_order_relaxed);
    debugStats.lightCount.store(static_cast<int>(snapshot.lights.size()), std::memory_order_relaxed);
}

void Scene::updateActiveCameraEffects(NeoECS::ECSEntity* activeCameraEntity, NeoECS::ECSComponentManager* manager){
    auto screen = getMainScreen();
    if(!screen){
        return;
    }

    screen->clearEffects();
    if(!activeCameraEntity || !manager){
        return;
    }

    auto* camComponent = manager->getECSComponent<CameraComponent>(activeCameraEntity);
    if(!camComponent || !camComponent->camera){
        return;
    }

    const CameraSettings& settings = camComponent->camera->getSettings();

    if(auto* ssao = manager->getECSComponent<SSAOComponent>(activeCameraEntity)){
        auto effect = ssao->getEffectForCamera(settings);
        if(effect){
            screen->addEffect(effect);
        }
    }

    if(auto* dof = manager->getECSComponent<DepthOfFieldComponent>(activeCameraEntity)){
        auto effect = dof->getEffectForCamera(settings);
        if(effect){
            screen->addEffect(effect);
        }
    }

    if(auto* aa = manager->getECSComponent<AntiAliasingComponent>(activeCameraEntity)){
        auto effect = aa->getEffectForCamera(settings);
        if(effect){
            screen->addEffect(effect);
        }
    }
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
            deferredLightShader = ShaderCacheManager::INSTANCE.getOrCompile("DeferredLightPass_v1", vertexShader->asString(), fragmentShader->asString());
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
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer->getID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawBuffer->getID());
        glBlitFramebuffer(
            0, 0, gBufferWidth, gBufferHeight,
            0, 0, drawBuffer->getWidth(), drawBuffer->getHeight(),
            GL_DEPTH_BUFFER_BIT, GL_NEAREST
        );
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
