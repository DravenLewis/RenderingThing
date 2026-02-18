#include "Scene.h"

#include "Screen.h"
#include "ShadowRenderer.h"
#include "Logbot.h"
#include "SkyBox.h"
#include "ECSComponents.h"
#include "Color.h"
#include "GameEngine.h"
#include "PBRMaterial.h"
#include "LightUtils.h"
#include "ShaderProgram.h"
#include "Asset.h"
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
        auto* boundsComp = componentManager->getECSComponent<BoundsComponent>(entity);
        const bool needsWorld = (renderer && renderer->visible) ||
                                (lightComponent && (lightComponent->syncTransform || lightComponent->syncDirection));

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
    }else if(gBufferWidth != w || gBufferHeight != h){
        gBuffer->resize(w, h);
        gBufferWidth = w;
        gBufferHeight = h;
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
    gBufferShader->setUniformFast("u_view", Uniform<Math3D::Mat4>(cam->getViewMatrix()));
    gBufferShader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(cam->getProjectionMatrix()));

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];

    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material) continue;
        if(isMaterialTransparent(item.material)) continue;
        if(!isDeferredCompatibleMaterial(item.material)) continue;

        if(item.enableBackfaceCulling){
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }else{
            glDisable(GL_CULL_FACE);
        }

        gBufferShader->setUniformFast("u_model", Uniform<Math3D::Mat4>(item.model));

        Math3D::Vec4 baseColor = Color::WHITE;
        PTexture baseColorTex = nullptr;
        int useBaseColorTex = 0;
        float metallic = 0.0f;
        float roughness = 1.0f;
        PTexture metallicRoughnessTex = nullptr;
        int useMetallicRoughnessTex = 0;
        PTexture normalTex = nullptr;
        int useNormalTex = 0;
        float normalScale = 1.0f;
        PTexture occlusionTex = nullptr;
        int useOcclusionTex = 0;
        float aoStrength = 1.0f;
        Math3D::Vec2 uvScale(1.0f, 1.0f);
        Math3D::Vec2 uvOffset(0.0f, 0.0f);
        int useAlphaClip = 0;
        float alphaCutoff = 0.5f;
        int surfaceMode = 0; // 0=PBR, 1=LegacyLit, 2=LegacyUnlit

        if(auto pbr = Material::GetAs<PBRMaterial>(item.material)){
            baseColor = pbr->BaseColor.get();
            baseColorTex = pbr->BaseColorTex.get();
            useBaseColorTex = baseColorTex ? 1 : 0;
            metallic = pbr->Metallic.get();
            roughness = pbr->Roughness.get();
            metallicRoughnessTex = pbr->MetallicRoughnessTex.get();
            useMetallicRoughnessTex = metallicRoughnessTex ? 1 : 0;
            normalTex = pbr->NormalTex.get();
            useNormalTex = normalTex ? 1 : 0;
            normalScale = pbr->NormalScale.get();
            occlusionTex = pbr->OcclusionTex.get();
            useOcclusionTex = occlusionTex ? 1 : 0;
            aoStrength = pbr->OcclusionStrength.get();
            uvScale = pbr->UVScale.get();
            uvOffset = pbr->UVOffset.get();
            useAlphaClip = pbr->UseAlphaClip.get();
            alphaCutoff = pbr->AlphaCutoff.get();
            surfaceMode = 0;
        }else if(auto litImage = Material::GetAs<MaterialDefaults::LitImageMaterial>(item.material)){
            baseColor = litImage->Color.get();
            baseColorTex = litImage->Tex.get();
            useBaseColorTex = baseColorTex ? 1 : 0;
            roughness = 0.85f;
            surfaceMode = 1;
        }else if(auto flatImage = Material::GetAs<MaterialDefaults::FlatImageMaterial>(item.material)){
            baseColor = flatImage->Color.get();
            baseColorTex = flatImage->Tex.get();
            useBaseColorTex = baseColorTex ? 1 : 0;
            roughness = 0.95f;
            surfaceMode = 1;
        }else if(auto imageMat = Material::GetAs<MaterialDefaults::ImageMaterial>(item.material)){
            baseColor = imageMat->Color.get();
            baseColorTex = imageMat->Tex.get();
            useBaseColorTex = baseColorTex ? 1 : 0;
            surfaceMode = 2;
        }else if(auto litColor = Material::GetAs<MaterialDefaults::LitColorMaterial>(item.material)){
            baseColor = litColor->Color.get();
            roughness = 0.85f;
            surfaceMode = 1;
        }else if(auto flatColor = Material::GetAs<MaterialDefaults::FlatColorMaterial>(item.material)){
            baseColor = flatColor->Color.get();
            roughness = 0.95f;
            surfaceMode = 1;
        }else if(auto colorMat = Material::GetAs<MaterialDefaults::ColorMaterial>(item.material)){
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
        gBufferShader->setUniformFast("u_useMetallicRoughnessTex", Uniform<int>(useMetallicRoughnessTex));
        gBufferShader->setUniformFast("u_metallicRoughnessTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(metallicRoughnessTex, 1)));

        gBufferShader->setUniformFast("u_useNormalTex", Uniform<int>(useNormalTex));
        gBufferShader->setUniformFast("u_normalScale", Uniform<float>(normalScale));
        gBufferShader->setUniformFast("u_normalTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(normalTex, 2)));

        gBufferShader->setUniformFast("u_useOcclusionTex", Uniform<int>(useOcclusionTex));
        gBufferShader->setUniformFast("u_aoStrength", Uniform<float>(aoStrength));
        gBufferShader->setUniformFast("u_occlusionTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(occlusionTex, 3)));

        gBufferShader->setUniformFast("u_uvScale", Uniform<Math3D::Vec2>(uvScale));
        gBufferShader->setUniformFast("u_uvOffset", Uniform<Math3D::Vec2>(uvOffset));
        gBufferShader->setUniformFast("u_useAlphaClip", Uniform<int>(useAlphaClip));
        gBufferShader->setUniformFast("u_alphaCutoff", Uniform<float>(alphaCutoff));
        gBufferShader->setUniformFast("u_surfaceMode", Uniform<int>(surfaceMode));

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
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material) continue;
        if(item.entityId != selectedEntityId) continue;

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

    if(!gBuffer->validate()){
        if(!loggedInvalid){
            LogBot.Log(LOG_ERRO, "GBuffer framebuffer invalid; falling back to forward rendering.");
            loggedInvalid = true;
        }
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    while(glGetError() != GL_NO_ERROR) {}

    auto checkGlError = [&](const char* stage){
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
}

void Scene::drawModels3D(PCamera cam, RenderFilter filter, bool drawOutlines, bool skipDeferredCompatible){
    if(!cam) return;

    if(!outlineMaterial){
        outlineMaterial = MaterialDefaults::ColorMaterial::Create(Color::fromRGBA32(0x3498dbFF));
    }

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    const bool markDeferredIncompatible = (GameEngine::Engine &&
                                           GameEngine::Engine->getRenderStrategy() == EngineRenderStrategy::Deferred);
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material) continue;

        bool isTransparent = isMaterialTransparent(item.material);
        if(filter == RenderFilter::Opaque && isTransparent) continue;
        if(filter == RenderFilter::Transparent && !isTransparent) continue;
        if(skipDeferredCompatible && isDeferredCompatibleMaterial(item.material)) continue;

        std::shared_ptr<Material> drawMaterial = item.material;
        if(markDeferredIncompatible && !isDeferredCompatibleMaterial(item.material)){
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

        if(item.enableBackfaceCulling){
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }else{
            glDisable(GL_CULL_FACE);
        }

        drawMaterial->set<Math3D::Mat4>("u_model", item.model);
        drawMaterial->set<Math3D::Mat4>("u_view", cam->getViewMatrix());
        drawMaterial->set<Math3D::Mat4>("u_projection", cam->getProjectionMatrix());
        drawMaterial->bind();
        item.mesh->draw();
        drawMaterial->unbind();

        if(drawOutlines && outlineEnabled && !selectedEntityId.empty() && item.entityId == selectedEntityId && outlineMaterial){
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
