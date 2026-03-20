/**
 * @file src/Scene/Scene.cpp
 * @brief Implementation for Scene.
 */

#include "Scene/Scene.h"

#include "Rendering/Core/Screen.h"
#include "Rendering/Lighting/ShadowRenderer.h"
#include "Foundation/Logging/Logbot.h"
#include "Rendering/Textures/SkyBox.h"
#include "Assets/Descriptors/ImageAsset.h"
#include "Assets/Descriptors/MaterialAsset.h"
#include "Assets/Descriptors/ModelAsset.h"
#include "Assets/Descriptors/EnvironmentAsset.h"
#include "Assets/Descriptors/SkyboxAsset.h"
#include "Assets/Importers/OBJLoader.h"
#include "ECS/Core/ECSComponents.h"
#include "Foundation/Math/Color.h"
#include "Engine/Core/GameEngine.h"
#include "Rendering/Materials/ConstructedMaterial.h"
#include "Rendering/Materials/MaterialRegistry.h"
#include "Rendering/Materials/PBRMaterial.h"
#include "Rendering/Lighting/LightUtils.h"
#include "Rendering/PostFX/LensFlareEffect.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Assets/Core/Asset.h"
#include "Foundation/Util/StringUtils.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

namespace {
    const Math3D::Vec4 kSelectionOutlineColor(0.20392157f, 0.59607846f, 0.85882354f, 0.95f);
    constexpr int kDeferredLightTileSize = 16;
    constexpr int kDeferredSsrLocalProbeSize = 256;
    const std::array<Math3D::Vec3, 6> kDeferredSsrLocalProbeDirs = {{
        Math3D::Vec3( 1.0f,  0.0f,  0.0f),
        Math3D::Vec3(-1.0f,  0.0f,  0.0f),
        Math3D::Vec3( 0.0f,  1.0f,  0.0f),
        Math3D::Vec3( 0.0f, -1.0f,  0.0f),
        Math3D::Vec3( 0.0f,  0.0f,  1.0f),
        Math3D::Vec3( 0.0f,  0.0f, -1.0f)
    }};
    const std::array<Math3D::Vec3, 6> kDeferredSsrLocalProbeUps = {{
        Math3D::Vec3( 0.0f, -1.0f,  0.0f),
        Math3D::Vec3( 0.0f, -1.0f,  0.0f),
        Math3D::Vec3( 0.0f,  0.0f,  1.0f),
        Math3D::Vec3( 0.0f,  0.0f, -1.0f),
        Math3D::Vec3( 0.0f, -1.0f,  0.0f),
        Math3D::Vec3( 0.0f, -1.0f,  0.0f)
    }};

    Math3D::Vec3 safeNormalizeVec3(const Math3D::Vec3& value, const Math3D::Vec3& fallback){
        if(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)){
            const float len = value.length();
            if(len > Math3D::EPSILON){
                return value * (1.0f / len);
            }
        }
        return fallback;
    }

    Math3D::Vec3 transformDirection(const Math3D::Mat4& matrix, const Math3D::Vec3& direction){
        glm::mat3 linear = glm::mat3(static_cast<glm::mat4>(matrix));
        const float determinant = glm::determinant(linear);
        if(std::abs(determinant) <= 1e-8f){
            return safeNormalizeVec3(direction, Math3D::Vec3::up());
        }

        glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
        return safeNormalizeVec3(Math3D::Vec3(normalMatrix * static_cast<glm::vec3>(direction)), Math3D::Vec3::up());
    }

    Math3D::Vec3 reflectPointAcrossPlane(const Math3D::Vec3& point,
                                         const Math3D::Vec3& planePoint,
                                         const Math3D::Vec3& planeNormal){
        const float signedDistance = Math3D::Vec3::dot(point - planePoint, planeNormal);
        return point - (planeNormal * (2.0f * signedDistance));
    }

    Math3D::Vec3 reflectDirectionAcrossPlane(const Math3D::Vec3& direction,
                                             const Math3D::Vec3& planeNormal){
        return direction - (planeNormal * (2.0f * Math3D::Vec3::dot(direction, planeNormal)));
    }

    Math3D::Vec4 makePlaneEquation(const Math3D::Vec3& planePoint,
                                   const Math3D::Vec3& planeNormal,
                                   float clipOffset = 0.0f){
        return Math3D::Vec4(
            planeNormal.x,
            planeNormal.y,
            planeNormal.z,
            -Math3D::Vec3::dot(planeNormal, planePoint) - clipOffset
        );
    }

    float computePlanarReflectivityScore(const std::shared_ptr<PBRMaterial>& pbr){
        if(!pbr){
            return 0.0f;
        }

        const int bsdfModel = Math3D::Clamp(pbr->BsdfModel.get(), 0, 2);
        const float roughness = Math3D::Clamp(pbr->Roughness.get(), 0.0f, 1.0f);
        const float metallic = Math3D::Clamp(pbr->Metallic.get(), 0.0f, 1.0f);
        const float transmission = Math3D::Clamp(pbr->Transmission.get(), 0.0f, 1.0f);
        const float envStrength = Math3D::Max(pbr->EnvStrength.get(), 0.0f);

        float reflectivity =
            ((1.0f - roughness) * 1.15f) +
            (metallic * 0.90f) +
            (transmission * 0.35f) +
            (envStrength * 0.20f);
        if(bsdfModel == static_cast<int>(PBRBsdfModel::Glass)){
            reflectivity = Math3D::Max(reflectivity, 0.95f + ((1.0f - roughness) * 0.30f));
        }else if(bsdfModel == static_cast<int>(PBRBsdfModel::Water)){
            reflectivity = Math3D::Max(reflectivity, 0.75f + ((1.0f - roughness) * 0.25f));
        }

        return Math3D::Clamp(reflectivity, 0.0f, 4.0f);
    }

    int findSmallestExtentAxis(const Math3D::Vec3& extents){
        if(extents.x <= extents.y && extents.x <= extents.z){
            return 0;
        }
        if(extents.y <= extents.x && extents.y <= extents.z){
            return 1;
        }
        return 2;
    }

    float distancePointToAabb(const Math3D::Vec3& point, const Math3D::Vec3& minBounds, const Math3D::Vec3& maxBounds){
        const float dx = Math3D::Max(Math3D::Max(minBounds.x - point.x, 0.0f), point.x - maxBounds.x);
        const float dy = Math3D::Max(Math3D::Max(minBounds.y - point.y, 0.0f), point.y - maxBounds.y);
        const float dz = Math3D::Max(Math3D::Max(minBounds.z - point.z, 0.0f), point.z - maxBounds.z);
        return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    }

    struct DeferredLightUploadCandidate {
        Light light;
        int sourceIndex = -1;
        float priority = 0.0f;
        bool selected = false;
    };

    bool assetMatchesChanged(const std::string& candidate, const std::string& changedAsset){
        return !candidate.empty() && AssetManager::Instance.isSameAsset(candidate, changedAsset);
    }

    bool textureRefDependsOnAsset(const std::string& textureRef, const std::string& changedAsset){
        return ImageAssetIO::RefDependsOnAsset(textureRef, changedAsset);
    }

    bool materialDataDependsOnAsset(const MaterialAssetData& data, const std::string& changedAsset){
        return assetMatchesChanged(data.shaderAssetRef, changedAsset) ||
               textureRefDependsOnAsset(data.textureRef, changedAsset) ||
               textureRefDependsOnAsset(data.baseColorTexRef, changedAsset) ||
               textureRefDependsOnAsset(data.roughnessTexRef, changedAsset) ||
               textureRefDependsOnAsset(data.metallicRoughnessTexRef, changedAsset) ||
               textureRefDependsOnAsset(data.normalTexRef, changedAsset) ||
               textureRefDependsOnAsset(data.heightTexRef, changedAsset) ||
               textureRefDependsOnAsset(data.emissiveTexRef, changedAsset) ||
               textureRefDependsOnAsset(data.occlusionTexRef, changedAsset);
    }

    bool materialRefDependsOnAsset(const std::string& materialRef, const std::string& changedAsset){
        if(materialRef.empty()){
            return false;
        }
        if(assetMatchesChanged(materialRef, changedAsset)){
            return true;
        }

        std::string resolvedAssetRef;
        if(!MaterialAssetIO::ResolveMaterialAssetRef(materialRef, resolvedAssetRef, nullptr)){
            return false;
        }
        if(assetMatchesChanged(resolvedAssetRef, changedAsset)){
            return true;
        }

        MaterialAssetData data;
        return MaterialAssetIO::LoadFromAssetRef(resolvedAssetRef, data, nullptr) &&
               materialDataDependsOnAsset(data, changedAsset);
    }

    bool skyboxAssetDependsOnAsset(const std::string& skyboxAssetRef, const std::string& changedAsset){
        if(skyboxAssetRef.empty()){
            return false;
        }
        if(assetMatchesChanged(skyboxAssetRef, changedAsset)){
            return true;
        }

        SkyboxAssetData data;
        if(!SkyboxAssetIO::LoadFromAssetRef(skyboxAssetRef, data, nullptr)){
            return false;
        }

        return textureRefDependsOnAsset(data.rightFaceRef, changedAsset) ||
               textureRefDependsOnAsset(data.leftFaceRef, changedAsset) ||
               textureRefDependsOnAsset(data.topFaceRef, changedAsset) ||
               textureRefDependsOnAsset(data.bottomFaceRef, changedAsset) ||
               textureRefDependsOnAsset(data.frontFaceRef, changedAsset) ||
               textureRefDependsOnAsset(data.backFaceRef, changedAsset);
    }

    bool environmentAssetDependsOnAsset(const std::string& environmentAssetRef, const std::string& changedAsset){
        if(environmentAssetRef.empty()){
            return false;
        }
        if(assetMatchesChanged(environmentAssetRef, changedAsset)){
            return true;
        }

        EnvironmentAssetData data;
        if(!EnvironmentAssetIO::LoadFromAssetRef(environmentAssetRef, data, nullptr)){
            return false;
        }
        return skyboxAssetDependsOnAsset(StringUtils::Trim(data.skyboxAssetRef), changedAsset);
    }

    bool ensureRuntimeSkyboxLoaded(const std::string& skyboxAssetRefRaw,
                                   std::string& loadedSkyboxAssetRef,
                                   std::shared_ptr<SkyBox>& runtimeSkyBox,
                                   const char* contextLabel){
        const std::string skyboxAssetRef = StringUtils::Trim(skyboxAssetRefRaw);
        if(skyboxAssetRef.empty()){
            loadedSkyboxAssetRef.clear();
            runtimeSkyBox.reset();
            return false;
        }

        if(!runtimeSkyBox || loadedSkyboxAssetRef != skyboxAssetRef){
            std::string error;
            auto loadedSkybox = SkyboxAssetIO::InstantiateSkyBoxFromRef(skyboxAssetRef, &error);
            if(!loadedSkybox){
                if(!error.empty()){
                    LogBot.Log(LOG_WARN, "Failed to load %s '%s': %s",
                               contextLabel ? contextLabel : "skybox",
                               skyboxAssetRef.c_str(),
                               error.c_str());
                }
                loadedSkyboxAssetRef.clear();
                runtimeSkyBox.reset();
                return false;
            }

            runtimeSkyBox = loadedSkybox;
            loadedSkyboxAssetRef = skyboxAssetRef;
        }

        return (runtimeSkyBox != nullptr);
    }

    EnvironmentComponent* findFirstActiveEnvironmentComponent(NeoECS::ECSComponentManager* manager,
                                                              NeoECS::ECSEntityManager* entityManager){
        if(!manager || !entityManager){
            return nullptr;
        }

        const auto& entities = entityManager->getEntities();
        for(const auto& entityPtr : entities){
            auto* entity = entityPtr.get();
            if(!entity){
                continue;
            }

            auto* environment = manager->getECSComponent<EnvironmentComponent>(entity);
            if(IsComponentActive(environment)){
                return environment;
            }
        }
        return nullptr;
    }

    bool flareAssetDependsOnAsset(const std::string& flareAssetRef, const std::string& changedAsset){
        if(flareAssetRef.empty()){
            return false;
        }
        if(assetMatchesChanged(flareAssetRef, changedAsset)){
            return true;
        }

        LensFlareAssetData data;
        if(!LensFlareAssetIO::LoadFromAssetRef(flareAssetRef, data, nullptr)){
            return false;
        }

        for(const LensFlareElementData& element : data.elements){
            if(element.type == LensFlareElementType::Image &&
               textureRefDependsOnAsset(element.textureRef, changedAsset)){
                return true;
            }
        }
        return false;
    }

    bool modelAssetDependsOnAsset(const std::string& modelAssetRef, const std::string& changedAsset){
        if(modelAssetRef.empty()){
            return false;
        }
        if(assetMatchesChanged(modelAssetRef, changedAsset)){
            return true;
        }

        ModelAssetData data;
        if(!ModelAssetIO::LoadFromAssetRef(modelAssetRef, data, nullptr)){
            return false;
        }

        return assetMatchesChanged(data.sourceModelRef, changedAsset) ||
               materialRefDependsOnAsset(data.defaultMaterialRef, changedAsset);
    }

    void ensureModelPartMaterialState(MeshRendererComponent& renderer){
        if(!renderer.model){
            return;
        }

        const size_t partCount = renderer.model->getParts().size();
        renderer.modelPartMaterialAssetRefs.resize(partCount);
        renderer.modelPartMaterialOverrides.resize(partCount, 0);
    }

    void initializeModelPartMaterialRefsFromModelAsset(MeshRendererComponent& renderer){
        if(!renderer.model || renderer.modelAssetRef.empty()){
            return;
        }

        ModelAssetData modelAssetData;
        if(!ModelAssetIO::LoadFromAssetRef(renderer.modelAssetRef, modelAssetData, nullptr)){
            return;
        }
        if(modelAssetData.defaultMaterialRef.empty()){
            return;
        }

        ensureModelPartMaterialState(renderer);
        for(std::string& partMaterialRef : renderer.modelPartMaterialAssetRefs){
            if(partMaterialRef.empty()){
                partMaterialRef = modelAssetData.defaultMaterialRef;
            }
        }
    }

    std::vector<std::shared_ptr<Material>> captureOverriddenPartMaterials(const MeshRendererComponent& renderer){
        std::vector<std::shared_ptr<Material>> preserved;
        if(!renderer.model){
            return preserved;
        }

        const auto& parts = renderer.model->getParts();
        preserved.resize(parts.size());
        const size_t maxCount = std::min(parts.size(), renderer.modelPartMaterialOverrides.size());
        for(size_t i = 0; i < maxCount; ++i){
            if(renderer.modelPartMaterialOverrides[i] == 0){
                continue;
            }
            if(parts[i]){
                preserved[i] = parts[i]->material;
            }
        }
        return preserved;
    }

    void applyModelPartMaterials(MeshRendererComponent& renderer,
                                 const std::vector<std::shared_ptr<Material>>& preservedOverrides){
        if(!renderer.model){
            return;
        }

        ensureModelPartMaterialState(renderer);
        initializeModelPartMaterialRefsFromModelAsset(renderer);

        const auto& parts = renderer.model->getParts();
        for(size_t i = 0; i < parts.size(); ++i){
            const auto& part = parts[i];
            if(!part){
                continue;
            }

            const bool useOverride =
                (i < renderer.modelPartMaterialOverrides.size()) &&
                (renderer.modelPartMaterialOverrides[i] != 0);
            if(useOverride){
                if(i < preservedOverrides.size() && preservedOverrides[i]){
                    part->material = preservedOverrides[i];
                }
                continue;
            }

            if(i < renderer.modelPartMaterialAssetRefs.size() &&
               !renderer.modelPartMaterialAssetRefs[i].empty()){
                auto material = MaterialAssetIO::InstantiateMaterialFromRef(
                    renderer.modelPartMaterialAssetRefs[i],
                    nullptr,
                    nullptr
                );
                if(material){
                    part->material = material;
                }
            }
        }
    }

    std::shared_ptr<Model> instantiateModelFromSourceRef(const std::string& sourceRef,
                                                         bool forceSmoothNormals,
                                                         std::string* outError){
        if(sourceRef.empty()){
            if(outError){
                *outError = "Model source reference is empty.";
            }
            return nullptr;
        }

        const std::string lowerExt = StringUtils::ToLowerCase(std::filesystem::path(sourceRef).extension().string());
        auto sourceAsset = AssetManager::Instance.getOrLoad(sourceRef);
        if(!sourceAsset){
            if(outError){
                *outError = "Failed to load model source asset: " + sourceRef;
            }
            return nullptr;
        }

        if(lowerExt == ".obj"){
            auto model = OBJLoader::LoadFromAsset(sourceAsset, nullptr, forceSmoothNormals);
            if(model){
                model->setSourceAssetRef(sourceRef);
                model->setSourceForceSmoothNormals(forceSmoothNormals);
            }else if(outError){
                *outError = "OBJ model load failed for source: " + sourceRef;
            }
            return model;
        }

        if(outError){
            *outError = "Unsupported model source format: " + lowerExt;
        }
        return nullptr;
    }

    bool reloadTextureValueIfNeeded(ValueContainer<PTexture>& textureField, const std::string& changedAsset){
        PTexture currentTexture = textureField.get();
        if(!currentTexture){
            return false;
        }

        const std::string textureRef = currentTexture->getSourceAssetRef();
        if(!textureRefDependsOnAsset(textureRef, changedAsset)){
            return false;
        }

        textureField = ImageAssetIO::InstantiateTextureFromRef(textureRef, nullptr, nullptr);
        return true;
    }

    bool refreshInlineMaterialTextures(const std::shared_ptr<Material>& material, const std::string& changedAsset){
        if(!material){
            return false;
        }

        bool changed = false;
        if(auto pbr = Material::GetAs<PBRMaterial>(material)){
            changed |= reloadTextureValueIfNeeded(pbr->BaseColorTex, changedAsset);
            changed |= reloadTextureValueIfNeeded(pbr->RoughnessTex, changedAsset);
            changed |= reloadTextureValueIfNeeded(pbr->MetallicRoughnessTex, changedAsset);
            changed |= reloadTextureValueIfNeeded(pbr->NormalTex, changedAsset);
            changed |= reloadTextureValueIfNeeded(pbr->HeightTex, changedAsset);
            changed |= reloadTextureValueIfNeeded(pbr->EmissiveTex, changedAsset);
            changed |= reloadTextureValueIfNeeded(pbr->OcclusionTex, changedAsset);
            return changed;
        }
        if(auto image = Material::GetAs<MaterialDefaults::ImageMaterial>(material)){
            return reloadTextureValueIfNeeded(image->Tex, changedAsset);
        }
        if(auto litImage = Material::GetAs<MaterialDefaults::LitImageMaterial>(material)){
            return reloadTextureValueIfNeeded(litImage->Tex, changedAsset);
        }
        if(auto flatImage = Material::GetAs<MaterialDefaults::FlatImageMaterial>(material)){
            return reloadTextureValueIfNeeded(flatImage->Tex, changedAsset);
        }
        if(auto constructed = Material::GetAs<ConstructedMaterial>(material)){
            bool fieldChanged = false;
            for(auto& field : constructed->fields()){
                if(field.type != ConstructedMaterial::FieldType::Texture2D){
                    continue;
                }

                if(textureRefDependsOnAsset(field.textureAssetRef, changedAsset) ||
                   (field.texturePtr && textureRefDependsOnAsset(field.texturePtr->getSourceAssetRef(), changedAsset))){
                    field.loadedTextureRef.clear();
                    field.loadedTextureRevision = 0;
                    fieldChanged = true;
                }
            }
            if(fieldChanged){
                constructed->markFieldsDirty();
            }
            return fieldChanged;
        }

        return false;
    }

    bool reloadRendererModelFromAsset(MeshRendererComponent& renderer){
        if(renderer.modelAssetRef.empty()){
            return false;
        }

        const auto preservedOverrides = captureOverriddenPartMaterials(renderer);
        std::string resolvedAssetRef;
        std::string error;
        auto reloadedModel = ModelAssetIO::InstantiateModelFromRef(renderer.modelAssetRef, &resolvedAssetRef, &error);
        if(!reloadedModel){
            if(!error.empty()){
                LogBot.Log(LOG_WARN, "Failed to reload model asset '%s': %s", renderer.modelAssetRef.c_str(), error.c_str());
            }
            return false;
        }

        renderer.model = reloadedModel;
        if(!resolvedAssetRef.empty()){
            renderer.modelAssetRef = resolvedAssetRef;
        }
        renderer.modelSourceRef = reloadedModel->getSourceAssetRef();
        renderer.modelForceSmoothNormals = reloadedModel->getSourceForceSmoothNormals() ? 1 : 0;
        renderer.mesh.reset();
        renderer.material.reset();
        applyModelPartMaterials(renderer, preservedOverrides);
        return true;
    }

    bool reloadRendererModelFromSource(MeshRendererComponent& renderer){
        if(renderer.modelSourceRef.empty()){
            return false;
        }

        const auto preservedOverrides = captureOverriddenPartMaterials(renderer);
        std::string error;
        auto reloadedModel = instantiateModelFromSourceRef(
            renderer.modelSourceRef,
            renderer.modelForceSmoothNormals != 0,
            &error
        );
        if(!reloadedModel){
            if(!error.empty()){
                LogBot.Log(LOG_WARN, "Failed to reload model source '%s': %s", renderer.modelSourceRef.c_str(), error.c_str());
            }
            return false;
        }

        renderer.model = reloadedModel;
        renderer.mesh.reset();
        renderer.material.reset();
        applyModelPartMaterials(renderer, preservedOverrides);
        return true;
    }

    bool reinstantiateMaterialFromRef(const std::string& materialRef, std::shared_ptr<Material>& outMaterial){
        if(materialRef.empty()){
            return false;
        }

        std::string error;
        auto reloadedMaterial = MaterialAssetIO::InstantiateMaterialFromRef(materialRef, nullptr, &error);
        if(!reloadedMaterial){
            if(!error.empty()){
                LogBot.Log(LOG_WARN, "Failed to reload material '%s': %s", materialRef.c_str(), error.c_str());
            }
            return false;
        }

        outMaterial = reloadedMaterial;
        return true;
    }

    void fillAabbCorners(const Math3D::Vec3& minV, const Math3D::Vec3& maxV, std::array<glm::vec3, 8>& outCorners){
        outCorners[0] = glm::vec3(minV.x, minV.y, minV.z);
        outCorners[1] = glm::vec3(maxV.x, minV.y, minV.z);
        outCorners[2] = glm::vec3(minV.x, maxV.y, minV.z);
        outCorners[3] = glm::vec3(maxV.x, maxV.y, minV.z);
        outCorners[4] = glm::vec3(minV.x, minV.y, maxV.z);
        outCorners[5] = glm::vec3(maxV.x, minV.y, maxV.z);
        outCorners[6] = glm::vec3(minV.x, maxV.y, maxV.z);
        outCorners[7] = glm::vec3(maxV.x, maxV.y, maxV.z);
    }

    bool aabbIntersectsClipFrustum(const Math3D::Vec3& minV, const Math3D::Vec3& maxV, const Math3D::Mat4& clipMatrix){
        std::array<glm::vec3, 8> corners;
        fillAabbCorners(minV, maxV, corners);

        const glm::mat4 m = static_cast<glm::mat4>(clipMatrix);

        bool allLeft = true;
        bool allRight = true;
        bool allBottom = true;
        bool allTop = true;
        bool allNear = true;
        bool allFar = true;

        for(const glm::vec3& corner : corners){
            const glm::vec4 clip = m * glm::vec4(corner, 1.0f);
            if(!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w)){
                return true;
            }

            allLeft = allLeft && (clip.x < -clip.w);
            allRight = allRight && (clip.x > clip.w);
            allBottom = allBottom && (clip.y < -clip.w);
            allTop = allTop && (clip.y > clip.w);
            allNear = allNear && (clip.z < -clip.w);
            allFar = allFar && (clip.z > clip.w);
        }

        return !(allLeft || allRight || allBottom || allTop || allNear || allFar);
    }

    bool lightLikelyAffectsCamera(const Light& light, const PCamera& camera){
        if(!camera){
            return true;
        }
        if(light.type == LightType::DIRECTIONAL){
            return true;
        }
        if(light.intensity <= 0.0001f){
            return false;
        }

        float influenceRange = (light.shadowRange > 0.0f) ? light.shadowRange : light.range;
        influenceRange = Math3D::Max(influenceRange, 0.1f);

        Math3D::Vec3 extent(influenceRange, influenceRange, influenceRange);
        Math3D::Vec3 minV = light.position - extent;
        Math3D::Vec3 maxV = light.position + extent;
        Math3D::Mat4 clipMatrix = camera->getProjectionMatrix() * camera->getViewMatrix();
        return aabbIntersectsClipFrustum(minV, maxV, clipMatrix);
    }

    float computeLightUploadPriority(const Light& light, const PCamera& camera){
        if(light.type == LightType::DIRECTIONAL){
            return 1000000.0f + Math3D::Max(light.intensity, 0.0f);
        }

        const float intensityScore = Math3D::Max(light.intensity, 0.0f) * 8.0f;
        const float range = Math3D::Max((light.shadowRange > 0.0f) ? light.shadowRange : light.range, 0.1f);
        const float rangeScore = range * 0.05f;
        if(!camera){
            return intensityScore + rangeScore;
        }

        const float distanceToCamera = Math3D::Vec3::distance(light.position, camera->transform().position);
        const float normalizedDistance = distanceToCamera / range;
        const float distanceScore = 4.0f * (1.0f - Math3D::Clamp(normalizedDistance, 0.0f, 4.0f) * 0.25f);
        return intensityScore + rangeScore + distanceScore;
    }

    bool projectAabbToTileRect(const Math3D::Vec3& minV,
                               const Math3D::Vec3& maxV,
                               const Math3D::Mat4& clipMatrix,
                               int viewportWidth,
                               int viewportHeight,
                               int tileSize,
                               int& outMinTileX,
                               int& outMinTileY,
                               int& outMaxTileX,
                               int& outMaxTileY){
        if(viewportWidth <= 0 || viewportHeight <= 0 || tileSize <= 0){
            return false;
        }
        if(!aabbIntersectsClipFrustum(minV, maxV, clipMatrix)){
            return false;
        }

        std::array<glm::vec3, 8> corners;
        fillAabbCorners(minV, maxV, corners);

        const glm::mat4 m = static_cast<glm::mat4>(clipMatrix);
        float minNdcX = 1.0f;
        float minNdcY = 1.0f;
        float maxNdcX = -1.0f;
        float maxNdcY = -1.0f;
        bool anyValidCorner = false;
        bool fallbackToFullscreen = false;

        for(const glm::vec3& corner : corners){
            const glm::vec4 clip = m * glm::vec4(corner, 1.0f);
            if(!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w)){
                fallbackToFullscreen = true;
                break;
            }
            if(clip.w <= 1e-5f){
                fallbackToFullscreen = true;
                break;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            minNdcX = std::min(minNdcX, ndc.x);
            minNdcY = std::min(minNdcY, ndc.y);
            maxNdcX = std::max(maxNdcX, ndc.x);
            maxNdcY = std::max(maxNdcY, ndc.y);
            anyValidCorner = true;
        }

        const int tilesX = Math3D::Max(1, (viewportWidth + tileSize - 1) / tileSize);
        const int tilesY = Math3D::Max(1, (viewportHeight + tileSize - 1) / tileSize);
        if(fallbackToFullscreen || !anyValidCorner){
            outMinTileX = 0;
            outMinTileY = 0;
            outMaxTileX = tilesX - 1;
            outMaxTileY = tilesY - 1;
            return true;
        }

        if(maxNdcX < -1.0f || minNdcX > 1.0f || maxNdcY < -1.0f || minNdcY > 1.0f){
            return false;
        }

        minNdcX = Math3D::Clamp(minNdcX, -1.0f, 1.0f);
        minNdcY = Math3D::Clamp(minNdcY, -1.0f, 1.0f);
        maxNdcX = Math3D::Clamp(maxNdcX, -1.0f, 1.0f);
        maxNdcY = Math3D::Clamp(maxNdcY, -1.0f, 1.0f);
 
        
        // Expand by one tile in pixel space so small camera movements don't cause
        // lights to pop in/out of tiles at boundaries (reduces banding when camera
        // position/look flickers between two values).
        const float marginPx = static_cast<float>(tileSize);
        const float minPx = std::max(0.0f, (minNdcX * 0.5f + 0.5f) * static_cast<float>(viewportWidth) - marginPx);
        const float minPy = std::max(0.0f, (minNdcY * 0.5f + 0.5f) * static_cast<float>(viewportHeight) - marginPx);
        const float maxPx = std::min(static_cast<float>(viewportWidth), (maxNdcX * 0.5f + 0.5f) * static_cast<float>(viewportWidth) + marginPx);
        const float maxPy = std::min(static_cast<float>(viewportHeight), (maxNdcY * 0.5f + 0.5f) * static_cast<float>(viewportHeight) + marginPx);

        outMinTileX = Math3D::Clamp(static_cast<int>(std::floor(minPx / static_cast<float>(tileSize))), 0, tilesX - 1);
        outMinTileY = Math3D::Clamp(static_cast<int>(std::floor(minPy / static_cast<float>(tileSize))), 0, tilesY - 1);
        outMaxTileX = Math3D::Clamp(static_cast<int>(std::floor(Math3D::Max(maxPx - 1.0f, 0.0f) / static_cast<float>(tileSize))), 0, tilesX - 1);
        outMaxTileY = Math3D::Clamp(static_cast<int>(std::floor(Math3D::Max(maxPy - 1.0f, 0.0f) / static_cast<float>(tileSize))), 0, tilesY - 1);
        return true;
    }

    PostProcessingEffectEntry* findPostEffectEntry(PostProcessingStackComponent* stack, PostProcessingEffectKind kind){
        if(!stack){
            return nullptr;
        }
        for(auto& effect : stack->effects){
            if(effect.kind == kind){
                return &effect;
            }
        }
        return nullptr;
    }

    void applyStackAutoExposureBloomCoupling(PostProcessingEffectEntry* autoExposureEntry,
                                             PostProcessingEffectEntry* bloomEntry){
        if(!autoExposureEntry || !bloomEntry){
            return;
        }
        if(!autoExposureEntry->enabled || !bloomEntry->enabled){
            return;
        }

        auto& autoExposure = autoExposureEntry->autoExposure;
        auto& bloom = bloomEntry->bloom;
        if(!bloom.adaptiveBloom){
            bloom.adaptiveBloom = true;
        }
        if(!bloom.runtimeEffect){
            return;
        }

        float exposure = 1.0f;
        if(autoExposure.runtimeEffect){
            exposure = Math3D::Clamp(autoExposure.runtimeEffect->getCurrentExposure(), 0.01f, 64.0f);
        }

        const float intensityBias = Math3D::Clamp(std::pow(1.0f / exposure, 0.35f), 0.78f, 1.28f);
        const float thresholdBias = Math3D::Clamp(std::pow(exposure, 0.22f), 0.84f, 1.22f);

        bloom.runtimeEffect->adaptiveBloom = true;
        bloom.runtimeEffect->autoExposureIntensityScale = intensityBias;
        bloom.runtimeEffect->autoExposureThresholdScale = thresholdBias;
        bloom.liveIntensity = Math3D::Clamp(bloom.runtimeEffect->intensity * intensityBias, 0.0f, 6.0f);
        bloom.liveThreshold = Math3D::Clamp(bloom.runtimeEffect->threshold * thresholdBias, 0.0f, 4.0f);
        bloom.liveAutoExposureDriven = true;
    }

    std::vector<LensFlareEffect::FlareEmitter> collectLensFlareEmitters(NeoECS::ECSEntityManager* entityManager,
                                                                        NeoECS::ECSComponentManager* flareManager){
        std::vector<LensFlareEffect::FlareEmitter> emitters;
        if(!entityManager){
            return emitters;
        }

        const auto& entities = entityManager->getEntities();
        emitters.reserve(entities.size());
        for(const auto& entityPtr : entities){
            auto* entity = entityPtr.get();
            if(!entity){
                continue;
            }

            auto* lightComponent = flareManager ? flareManager->getECSComponent<LightComponent>(entity) : nullptr;
            if(!lightComponent || !IsComponentActive(lightComponent) || lightComponent->flareAssetRef.empty()){
                continue;
            }

            LensFlareEffect::FlareEmitter emitter;
            emitter.type = lightComponent->light.type;
            emitter.position = lightComponent->light.position;
            emitter.direction = lightComponent->light.direction;
            emitter.color = lightComponent->light.color;
            emitter.intensity = lightComponent->light.intensity;
            emitter.assetRef = lightComponent->flareAssetRef;
            emitters.push_back(std::move(emitter));
        }
        return emitters;
    }

    const std::string kSelectionOutlineMaskVertShader = R"(
        #version 330 core

        layout (location = 0) in vec3 aPos;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;

        void main(){
            gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
        }
    )";

    const std::string kSelectionOutlineMaskFragShader = R"(
        #version 330 core

        out vec4 FragColor;

        void main(){
            FragColor = vec4(1.0);
        }
    )";

    const std::string kSelectionOutlineCompositeFragShader = R"(
        #version 330 core

        in vec2 TexCoords;
        out vec4 FragColor;

        uniform sampler2D u_maskTexture;
        uniform vec4 u_outlineColor;

        void main(){
            ivec2 texSize = textureSize(u_maskTexture, 0);
            ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), texSize - ivec2(1));
            ivec2 offsets[8] = ivec2[8](
                ivec2(-1,  0),
                ivec2( 1,  0),
                ivec2( 0, -1),
                ivec2( 0,  1),
                ivec2(-1, -1),
                ivec2( 1, -1),
                ivec2(-1,  1),
                ivec2( 1,  1)
            );

            float center = texelFetch(u_maskTexture, pixel, 0).r;
            float ring = 0.0;
            for(int i = 0; i < 8; ++i){
                ivec2 pixelA = clamp(pixel + offsets[i], ivec2(0), texSize - ivec2(1));
                ivec2 pixelB = clamp(pixel + offsets[i] * 2, ivec2(0), texSize - ivec2(1));
                ring = max(ring, texelFetch(u_maskTexture, pixelA, 0).r);
                ring = max(ring, texelFetch(u_maskTexture, pixelB, 0).r);
            }

            float outline = ring * (1.0 - center);
            if(outline <= 0.0){
                discard;
            }

            FragColor = vec4(u_outlineColor.rgb, u_outlineColor.a * outline);
        }
    )";

    std::shared_ptr<ShaderProgram> compileInlineShaderProgram(const char* debugName,
                                                              const std::string& vertexShader,
                                                              const std::string& fragmentShader){
        auto program = std::make_shared<ShaderProgram>();
        program->setVertexShader(vertexShader);
        program->setFragmentShader(fragmentShader);
        if(program->compile() == 0){
            LogBot.Log(LOG_ERRO, "Failed to compile %s shader: \n%s", debugName, program->getLog().c_str());
        }
        return program;
    }

    std::shared_ptr<ModelPart> buildScreenQuadModelPart(){
        auto factory = ModelPartFactory::Create(nullptr);
        int v1 = 0;
        int v2 = 0;
        int v3 = 0;
        int v4 = 0;

        factory
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f, -1.0f, 0.0f)).UV(0, 0), &v1)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f, -1.0f, 0.0f)).UV(1, 0), &v2)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,  1.0f, 0.0f)).UV(1, 1), &v3)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,  1.0f, 0.0f)).UV(0, 1), &v4)
            .defineFace(v1, v2, v3, v4);

        return factory.assemble();
    }

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
    ensureAssetChangeListenerRegistered();
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

NeoECS::GameObject* Scene::createEnvironmentGameObject(const std::string& name, NeoECS::GameObject* parent){
    auto* root = createECSGameObject(name, parent);
    if(!root){
        return nullptr;
    }
    root->addComponent<EnvironmentComponent>();
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

void Scene::ensureAssetChangeListenerRegistered(){
    if(assetChangeListenerHandle >= 0){
        return;
    }

    assetChangeListenerHandle = AssetManager::Instance.addChangeListener(
        [this](const AssetManager::AssetChangeEvent& event){
            handleAssetChanged(event.request, event.cacheKey);
        }
    );
}

void Scene::handleAssetChanged(const std::string& assetRequest, const std::string& cacheKey){
    (void)assetRequest;
    if(!ecsInstance || cacheKey.empty()){
        return;
    }

    auto* componentManager = ecsInstance->getComponentManager();
    auto* entityManager = ecsInstance->getEntityManager();
    if(!componentManager || !entityManager){
        return;
    }

    const std::filesystem::path changedPath(cacheKey);
    if(MaterialAssetIO::IsMaterialPath(changedPath)){
        MaterialRegistry::Instance().Refresh(true);
    }

    bool renderStateDirty = false;
    const auto& entities = entityManager->getEntities();
    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(!entity){
            continue;
        }

        if(auto* skybox = componentManager->getECSComponent<SkyboxComponent>(entity)){
            if(skyboxAssetDependsOnAsset(skybox->skyboxAssetRef, cacheKey)){
                skybox->loadedSkyboxAssetRef.clear();
                skybox->runtimeSkyBox.reset();
                renderStateDirty = true;
            }
        }

        if(auto* environment = componentManager->getECSComponent<EnvironmentComponent>(entity)){
            bool environmentDirty = false;
            if(environmentAssetDependsOnAsset(environment->environmentAssetRef, cacheKey)){
                environment->loadedEnvironmentAssetRef.clear();
                std::string loadError;
                if(!StringUtils::Trim(environment->environmentAssetRef).empty() &&
                   !environment->loadFromAsset(&loadError) &&
                   !loadError.empty()){
                    LogBot.Log(LOG_WARN, "Failed to refresh environment asset '%s': %s",
                               environment->environmentAssetRef.c_str(),
                               loadError.c_str());
                }
                environmentDirty = true;
            }
            if(skyboxAssetDependsOnAsset(environment->skyboxAssetRef, cacheKey)){
                environment->loadedSkyboxAssetRef.clear();
                environment->runtimeSkyBox.reset();
                environmentDirty = true;
            }
            if(environmentDirty){
                renderStateDirty = true;
            }
        }

        if(auto* renderer = componentManager->getECSComponent<MeshRendererComponent>(entity)){
            bool rendererDirty = false;

            if(!renderer->modelAssetRef.empty() &&
               modelAssetDependsOnAsset(renderer->modelAssetRef, cacheKey)){
                rendererDirty |= reloadRendererModelFromAsset(*renderer);
            }else if(renderer->modelAssetRef.empty() &&
                     assetMatchesChanged(renderer->modelSourceRef, cacheKey)){
                rendererDirty |= reloadRendererModelFromSource(*renderer);
            }

            if(renderer->model){
                ensureModelPartMaterialState(*renderer);
                const auto& parts = renderer->model->getParts();
                for(size_t i = 0; i < parts.size(); ++i){
                    const auto& part = parts[i];
                    if(!part){
                        continue;
                    }

                    const bool usesSourceMaterial =
                        (i >= renderer->modelPartMaterialOverrides.size()) ||
                        (renderer->modelPartMaterialOverrides[i] == 0);
                    if(usesSourceMaterial &&
                       i < renderer->modelPartMaterialAssetRefs.size() &&
                       materialRefDependsOnAsset(renderer->modelPartMaterialAssetRefs[i], cacheKey)){
                        rendererDirty |= reinstantiateMaterialFromRef(
                            renderer->modelPartMaterialAssetRefs[i],
                            part->material
                        );
                        continue;
                    }

                    rendererDirty |= refreshInlineMaterialTextures(part->material, cacheKey);
                }
            }else{
                if(!renderer->materialAssetRef.empty() &&
                   !renderer->materialOverridesSource &&
                   materialRefDependsOnAsset(renderer->materialAssetRef, cacheKey)){
                    rendererDirty |= reinstantiateMaterialFromRef(renderer->materialAssetRef, renderer->material);
                }else{
                    rendererDirty |= refreshInlineMaterialTextures(renderer->material, cacheKey);
                }
            }

            renderStateDirty = renderStateDirty || rendererDirty;
        }
    }

    if(renderStateDirty){
        refreshRenderState();
    }
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
    ensureAssetChangeListenerRegistered();
    if(!ecsInstance) return;
    ecsInstance->update(deltaTime);
    refreshRenderState();
}

void Scene::refreshRenderState(){
    ensureAssetChangeListenerRegistered();
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
    NeoECS::ECSEntity* resolvedActiveCameraEntity = nullptr;
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
        auto* reflectionProbeComponent = componentManager->getECSComponent<ReflectionProbeComponent>(entity);
        const bool rendererActive = IsComponentActive(renderer);
        const bool lightActive = IsComponentActive(lightComponent);
        const bool boundsActive = IsComponentActive(boundsComp);
        const bool cameraActive = IsComponentActive(cameraComponent);
        const bool reflectionProbeActive = IsComponentActive(reflectionProbeComponent);
        const bool entityPropertiesActive = IsComponentActive(entityProperties);

        const bool needsWorld = (rendererActive && renderer && renderer->visible) ||
                                (lightActive && lightComponent && (lightComponent->syncTransform || lightComponent->syncDirection)) ||
                                (reflectionProbeActive && transform) ||
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
                cull = cull && renderer->model->isBackfaceCullingEnabled();
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
                    item.planarReflectionSource = renderer->planarReflectionSurface;
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
                item.planarReflectionSource = renderer->planarReflectionSurface;
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

        if(reflectionProbeActive && reflectionProbeComponent && transform){
            ReflectionProbeSnapshot probe;
            probe.entityId = entity->getNodeUniqueID();
            probe.resolution = Math3D::Clamp(reflectionProbeComponent->resolution, 64, 512);
            probe.priority = reflectionProbeComponent->priority;
            probe.autoUpdate = reflectionProbeComponent->autoUpdate;
            probe.updateIntervalFrames = Math3D::Clamp(reflectionProbeComponent->updateIntervalFrames, 1, 240);
            probe.center = world.getPosition();

            Math3D::Vec3 captureExtents = reflectionProbeComponent->captureExtents;
            Math3D::Vec3 influenceExtents = reflectionProbeComponent->influenceExtents;
            captureExtents.x = Math3D::Max(captureExtents.x, 0.25f);
            captureExtents.y = Math3D::Max(captureExtents.y, 0.25f);
            captureExtents.z = Math3D::Max(captureExtents.z, 0.25f);
            influenceExtents.x = Math3D::Max(influenceExtents.x, 0.25f);
            influenceExtents.y = Math3D::Max(influenceExtents.y, 0.25f);
            influenceExtents.z = Math3D::Max(influenceExtents.z, 0.25f);
            captureExtents.x = Math3D::Max(captureExtents.x, influenceExtents.x);
            captureExtents.y = Math3D::Max(captureExtents.y, influenceExtents.y);
            captureExtents.z = Math3D::Max(captureExtents.z, influenceExtents.z);

            probe.captureBoundsMin = probe.center - captureExtents;
            probe.captureBoundsMax = probe.center + captureExtents;
            probe.influenceBoundsMin = probe.center - influenceExtents;
            probe.influenceBoundsMax = probe.center + influenceExtents;
            snapshot.reflectionProbes.push_back(std::move(probe));
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
                    resolvedActiveCameraEntity = entity;
                }
            }
        }
    }

    NeoECS::ECSEntity* resolvedCameraEntity = nullptr;
    PCamera resolvedCamera = nullptr;
    if(resolvedActiveCameraEntity && activeCamera){
        resolvedCamera = activeCamera;
        resolvedCameraEntity = resolvedActiveCameraEntity;
    }else if(preferredEnabledCamera){
        resolvedCamera = preferredEnabledCamera;
        resolvedCameraEntity = preferredEnabledCameraEntity;
    }else if(firstEnabledCamera){
        resolvedCamera = firstEnabledCamera;
        resolvedCameraEntity = firstEnabledCameraEntity;
    }

    preferredCamera = resolvedCamera;
    this->activeCameraEntity = resolvedCameraEntity;
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
    NeoECS::ECSEntityManager* entityManager,
    NeoECS::ECSComponentManager* effectSourceManager
){
    if(!screen){
        return;
    }
    if(clearExisting){
        screen->clearEffects();
    }
    if(!cameraEntity || !manager){
        return;
    }

    NeoECS::ECSComponentManager* flareManager = effectSourceManager ? effectSourceManager : manager;
    NeoECS::ECSComponentManager* environmentManager = effectSourceManager ? effectSourceManager : manager;

    auto env = screen->getEnvironment();
    auto* camComponent = manager->getECSComponent<CameraComponent>(cameraEntity);
    if(!camComponent || !camComponent->camera || !IsComponentActive(camComponent)){
        if(env){
            env->setSkyBox(nullptr);
        }
        return;
    }

    const CameraSettings& settings = camComponent->camera->getSettings();

    EnvironmentComponent* activeEnvironment = findFirstActiveEnvironmentComponent(environmentManager, entityManager);
    if(!activeEnvironment && environmentManager != manager){
        activeEnvironment = findFirstActiveEnvironmentComponent(manager, entityManager);
    }

    if(env){
        if(activeEnvironment){
            env->setSettings(activeEnvironment->settings);
            if(ensureRuntimeSkyboxLoaded(activeEnvironment->skyboxAssetRef,
                                         activeEnvironment->loadedSkyboxAssetRef,
                                         activeEnvironment->runtimeSkyBox,
                                         "environment skybox")){
                env->setSkyBox(activeEnvironment->runtimeSkyBox);
            }else{
                env->setSkyBox(nullptr);
            }
        }else{
            env->setSettings(EnvironmentSettings{});
            env->setSkyBox(nullptr);
        }
    }

    bool isolateSsaoDebugView = false;
    if(auto* ssao = manager->getECSComponent<SSAOComponent>(cameraEntity)){
        if(IsComponentActive(ssao)){
            isolateSsaoDebugView = (ssao->debugView != 0);
        }
    }

    if(isolateSsaoDebugView){
        return;
    }

    auto* stack = manager->getECSComponent<PostProcessingStackComponent>(cameraEntity);
    if(!stack || !IsComponentActive(stack)){
        return;
    }

    for(size_t i = 0; i < stack->effects.size(); ++i){
        const PostProcessingEffectEntry effectCopy = stack->effects[i];
        stack->ensureDependenciesForEffect(effectCopy);
    }

    PostProcessingEffectEntry* autoExposureEntry = findPostEffectEntry(stack, PostProcessingEffectKind::AutoExposure);
    Graphics::PostProcessing::PPostProcessingEffect autoExposureEffect = nullptr;
    if(autoExposureEntry && autoExposureEntry->enabled){
        autoExposureEffect = stack->buildRuntimeEffect(*autoExposureEntry, settings, nullptr);
    }

    const std::vector<LensFlareEffect::FlareEmitter> flareEmitters =
        collectLensFlareEmitters(entityManager, flareManager);

    for(auto& effect : stack->effects){
        std::string error;
        Graphics::PostProcessing::PPostProcessingEffect runtimeEffect = nullptr;
        if(effect.kind == PostProcessingEffectKind::AutoExposure){
            runtimeEffect = autoExposureEffect;
        }else{
            runtimeEffect = stack->buildRuntimeEffect(effect, settings, &error);
        }

        if(!error.empty()){
            LogBot.Log(LOG_WARN,
                       "Failed to build post effect '%s' on camera '%s': %s",
                       stack->getEffectDisplayName(effect).c_str(),
                       cameraEntity->getName().c_str(),
                       error.c_str());
        }
        if(!runtimeEffect){
            continue;
        }

        if(effect.kind == PostProcessingEffectKind::Bloom && autoExposureEffect){
            applyStackAutoExposureBloomCoupling(autoExposureEntry, &effect);
        }

        if(effect.kind == PostProcessingEffectKind::LensFlare){
            if(effect.lensFlare.runtimeEffect){
                effect.lensFlare.runtimeEffect->setEmitters(flareEmitters);
            }
            if(flareEmitters.empty()){
                continue;
            }
        }

        screen->addEffect(runtimeEffect);
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

    /// @brief Represents Adaptive Focus Tap data.
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

bool Scene::resolveDeferredSsaoSettings(NeoECS::ECSComponentManager* manager,
                                        NeoECS::ECSEntity* cameraEntity,
                                        DeferredSSAOSettings& outSettings) const{
    if(hasDeferredSsaoOverride){
        outSettings = deferredSsaoOverrideSettings;
        return true;
    }
    if(!manager || !cameraEntity){
        return false;
    }

    auto* ssao = manager->getECSComponent<SSAOComponent>(cameraEntity);
    if(!IsComponentActive(ssao)){
        return false;
    }

    outSettings = ssao->buildDeferredSsaoSettings();
    return true;
}

bool Scene::resolveDeferredSsrSettings(NeoECS::ECSComponentManager* manager,
                                       NeoECS::ECSEntity* cameraEntity,
                                       DeferredSSRSettings& outSettings) const{
    if(hasDeferredSsrOverride){
        outSettings = deferredSsrOverrideSettings;
        return outSettings.enabled;
    }
    if(!manager || !cameraEntity){
        return false;
    }

    auto* ssr = manager->getECSComponent<SSRComponent>(cameraEntity);
    if(!IsComponentActive(ssr)){
        return false;
    }

    outSettings = ssr->buildDeferredSsrSettings();
    return outSettings.enabled;
}

void Scene::applyCameraEffectsToScreen(PScreen screen,
                                       NeoECS::ECSEntity* cameraEntity,
                                       bool clearExisting,
                                       const Scene* adaptiveFocusSourceScene){
    NeoECS::ECSComponentManager* manager = ecsInstance ? ecsInstance->getComponentManager() : nullptr;
    NeoECS::ECSEntityManager* entityManager = ecsInstance ? ecsInstance->getEntityManager() : nullptr;
    if(ecsInstance && cameraEntity){
        EnsurePostProcessingStackMigration(ecsInstance, cameraEntity, nullptr);
        manager = ecsInstance->getComponentManager();
        entityManager = ecsInstance->getEntityManager();
    }
    const Scene* focusSource = adaptiveFocusSourceScene ? adaptiveFocusSourceScene : this;
    Scene* ssaoRenderTargetScene = const_cast<Scene*>(adaptiveFocusSourceScene ? adaptiveFocusSourceScene : this);
    NeoECS::ECSEntityManager* effectSourceEntityManager = entityManager;
    NeoECS::ECSComponentManager* effectSourceManager = manager;
    if(adaptiveFocusSourceScene && adaptiveFocusSourceScene->ecsInstance){
        effectSourceEntityManager = adaptiveFocusSourceScene->ecsInstance->getEntityManager();
        effectSourceManager = adaptiveFocusSourceScene->ecsInstance->getComponentManager();
    }

    if(ssaoRenderTargetScene && clearExisting){
        ssaoRenderTargetScene->hasDeferredSsaoOverride = false;
        ssaoRenderTargetScene->hasDeferredSsrOverride = false;
    }

    if(manager && cameraEntity){
        auto* camComponent = manager->getECSComponent<CameraComponent>(cameraEntity);
        auto* stack = manager->getECSComponent<PostProcessingStackComponent>(cameraEntity);
        if(camComponent && IsComponentActive(camComponent) &&
           camComponent->camera &&
           stack && IsComponentActive(stack)){
            float adaptiveFocusDistance = 0.0f;
            bool hasAdaptiveFocus = false;
            if(focusSource == this){
                hasAdaptiveFocus = computeAdaptiveFocusDistanceFromSnapshot(cameraEntity, camComponent->camera, adaptiveFocusDistance);
            }else{
                hasAdaptiveFocus = focusSource->computeAdaptiveFocusDistanceFromSnapshotForCamera(camComponent->camera, adaptiveFocusDistance);
            }

            const CameraSettings& cameraSettings = camComponent->camera->getSettings();
            for(auto& effect : stack->effects){
                if(effect.kind != PostProcessingEffectKind::DepthOfField){
                    continue;
                }
                if(!effect.enabled || !effect.depthOfField.adaptiveFocus){
                    if(effect.depthOfField.runtimeEffect){
                        effect.depthOfField.runtimeEffect->externalAdaptiveFocusValid = false;
                    }
                    continue;
                }

                stack->buildRuntimeEffect(effect, cameraSettings, nullptr);
                if(!effect.depthOfField.runtimeEffect){
                    continue;
                }

                if(hasAdaptiveFocus){
                    effect.depthOfField.runtimeEffect->externalAdaptiveFocusDistance = adaptiveFocusDistance;
                    effect.depthOfField.runtimeEffect->externalAdaptiveFocusValid = true;
                }else{
                    effect.depthOfField.runtimeEffect->externalAdaptiveFocusValid = false;
                }
            }
        }else if(stack){
            for(auto& effect : stack->effects){
                if(effect.kind == PostProcessingEffectKind::DepthOfField &&
                   effect.depthOfField.runtimeEffect){
                    effect.depthOfField.runtimeEffect->externalAdaptiveFocusValid = false;
                }
            }
        }
    }

    if(ssaoRenderTargetScene && manager && cameraEntity){
        if(auto* ssao = manager->getECSComponent<SSAOComponent>(cameraEntity)){
            if(IsComponentActive(ssao)){
                ssaoRenderTargetScene->deferredSsaoOverrideSettings = ssao->buildDeferredSsaoSettings();
                ssaoRenderTargetScene->hasDeferredSsaoOverride = true;
            }
        }
        if(auto* ssr = manager->getECSComponent<SSRComponent>(cameraEntity)){
            if(IsComponentActive(ssr)){
                ssaoRenderTargetScene->deferredSsrOverrideSettings = ssr->buildDeferredSsrSettings();
                ssaoRenderTargetScene->hasDeferredSsrOverride = true;
            }
        }
    }

    ApplyCameraEffectsToScreen(
        screen,
        manager,
        cameraEntity,
        clearExisting,
        effectSourceEntityManager,
        effectSourceManager
    );
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
    PCamera camera = screen->getCamera();
    std::vector<DeferredLightUploadCandidate> uploadCandidates;
    uploadCandidates.reserve(snapshot.lights.size());
    for(size_t i = 0; i < snapshot.lights.size(); ++i){
        const Light& light = snapshot.lights[i];
        const bool isSelected = (static_cast<int>(i) == selectedLightUploadIndex);
        if(!isSelected && !lightLikelyAffectsCamera(light, camera)){
            continue;
        }

        DeferredLightUploadCandidate candidate;
        candidate.light = light;
        candidate.sourceIndex = static_cast<int>(i);
        candidate.priority = computeLightUploadPriority(light, camera) + (isSelected ? 10000000.0f : 0.0f);
        candidate.selected = isSelected;
        uploadCandidates.push_back(candidate);
    }

    auto priorityGreater = [](const DeferredLightUploadCandidate& a, const DeferredLightUploadCandidate& b){
        if(a.priority == b.priority){
            return a.sourceIndex < b.sourceIndex;
        }
        return a.priority > b.priority;
    };

    if(uploadCandidates.size() > MAX_LIGHTS){
        std::partial_sort(
            uploadCandidates.begin(),
            uploadCandidates.begin() + MAX_LIGHTS,
            uploadCandidates.end(),
            priorityGreater
        );
        uploadCandidates.resize(MAX_LIGHTS);
    }else{
        std::sort(uploadCandidates.begin(), uploadCandidates.end(), priorityGreater);
    }

    int remappedSelectedLightIndex = -1;
    for(const auto& candidate : uploadCandidates){
        const int uploadIndex = static_cast<int>(lightManager.getLightCount());
        lightManager.addLight(candidate.light);
        if(candidate.selected && uploadIndex < MAX_LIGHTS){
            remappedSelectedLightIndex = uploadIndex;
        }
    }

    const int uploadedCount = static_cast<int>(lightManager.getLightCount());
    const int selectedIndex =
        (remappedSelectedLightIndex >= 0 && remappedSelectedLightIndex < uploadedCount)
            ? remappedSelectedLightIndex
            : -1;
    ShadowRenderer::SetSelectedLightIndex(selectedIndex);
}

bool Scene::isMaterialTransparent(const std::shared_ptr<Material>& material) const{
    if(!material) return false;

    if(auto pbr = Material::GetAs<PBRMaterial>(material)){
        const int bsdfModel = Math3D::Clamp(pbr->BsdfModel.get(), 0, 2);
        if(bsdfModel == static_cast<int>(PBRBsdfModel::Glass) ||
           bsdfModel == static_cast<int>(PBRBsdfModel::Water)){
            // Keep transmissive materials blended so they read as actual glass/water.
            // Deferred still handles opaque surfaces; glass/water are composited in the transparent pass.
            return true;
        }
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

    return (Material::GetAs<PBRMaterial>(material) != nullptr) ||
           (Material::GetAs<MaterialDefaults::LitColorMaterial>(material) != nullptr) ||
           (Material::GetAs<MaterialDefaults::LitImageMaterial>(material) != nullptr) ||
           (Material::GetAs<MaterialDefaults::FlatColorMaterial>(material) != nullptr) ||
           (Material::GetAs<MaterialDefaults::FlatImageMaterial>(material) != nullptr) ||
           (Material::GetAs<MaterialDefaults::ColorMaterial>(material) != nullptr) ||
           (Material::GetAs<MaterialDefaults::ImageMaterial>(material) != nullptr);
}

void Scene::ensureDeferredResources(PScreen screen){
    if(!screen) return;

    int w = screen->getWidth();
    int h = screen->getHeight();
    if(!gBuffer || gBuffer->getGBufferCount() < 4){
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

    auto recreateLightingBuffer = [&](PFrameBuffer& buffer, int targetWidth, int targetHeight, GLint filter){
        if(buffer &&
           buffer->getWidth() == targetWidth &&
           buffer->getHeight() == targetHeight &&
           buffer->getTexture()){
            glBindTexture(GL_TEXTURE_2D, buffer->getTexture()->getID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glBindTexture(GL_TEXTURE_2D, 0);
            return;
        }

        buffer = FrameBuffer::Create(targetWidth, targetHeight);
        if(buffer){
            buffer->attachTexture(Texture::CreateRenderTarget(targetWidth, targetHeight, GL_RGBA16F, GL_RGBA, GL_FLOAT));
            if(buffer->getTexture()){
                glBindTexture(GL_TEXTURE_2D, buffer->getTexture()->getID());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
    };
    recreateLightingBuffer(
        deferredDirectLightBuffer,
        DeferredScreenGI::ComputeTargetWidth(w),
        DeferredScreenGI::ComputeTargetHeight(h),
        GL_LINEAR
    );

    if(!gBufferShader){
        auto vertexShader = AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Lit.vert");
        auto fragmentShader = AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_GBuffer.frag");
        if(vertexShader && fragmentShader){
            gBufferShader = ShaderCacheManager::INSTANCE.getOrCompile("GBufferPass_v4", vertexShader->asString(), fragmentShader->asString());
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
            deferredLightShader = ShaderCacheManager::INSTANCE.getOrCompile("DeferredLightPass_v8", vertexShader->asString(), fragmentShader->asString());
            if(deferredLightShader && deferredLightShader->getID() == 0){
                LogBot.Log(LOG_ERRO, "Failed to link DeferredLightPass shader: \n%s", deferredLightShader->getLog().c_str());
            }
        }else{
            LogBot.Log(LOG_ERRO, "DeferredLightPass shader assets missing.");
        }
    }

    if(!deferredQuad){
        try{
            deferredQuad = buildScreenQuadModelPart();
        }catch(const std::exception& e){
            LogBot.Log(LOG_ERRO, "Deferred quad creation failed: %s", e.what());
        }catch(...){
            LogBot.Log(LOG_ERRO, "Deferred quad creation failed: unknown exception");
        }
    }
}

void Scene::ensureDeferredLightTileResources(int width, int height){
    GLint maxTextureSize = 16384;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    if(maxTextureSize <= 0){
        maxTextureSize = 16384;
    }

    int tileSize = kDeferredLightTileSize;
    int tilesX = Math3D::Max(1, (width + tileSize - 1) / tileSize);
    int tilesY = Math3D::Max(1, (height + tileSize - 1) / tileSize);
    int tileCount = tilesX * tilesY;
    while(tileCount > maxTextureSize && tileSize < 256){
        tileSize += kDeferredLightTileSize;
        tilesX = Math3D::Max(1, (width + tileSize - 1) / tileSize);
        tilesY = Math3D::Max(1, (height + tileSize - 1) / tileSize);
        tileCount = tilesX * tilesY;
    }

    const int tileStride = MAX_LIGHTS + 1;

    if(!deferredLightTileTexture ||
       deferredLightTileGridWidth != tilesX ||
       deferredLightTileGridHeight != tilesY ||
       deferredLightTileTexture->getWidth() != tileStride ||
       deferredLightTileTexture->getHeight() != tileCount){
        deferredLightTileTexture = Texture::CreateRenderTarget(tileStride, tileCount, GL_R32I, GL_RED_INTEGER, GL_INT);
        deferredLightTileGridWidth = tilesX;
        deferredLightTileGridHeight = tilesY;
        deferredLightTileSize = tileSize;
    }else{
        deferredLightTileSize = tileSize;
    }

    deferredLightTileCpuData.assign(static_cast<size_t>(tileCount) * tileStride, -1);
    for(int tileIndex = 0; tileIndex < tileCount; ++tileIndex){
        deferredLightTileCpuData[static_cast<size_t>(tileIndex) * tileStride] = 0;
    }
}

bool Scene::buildDeferredLightTiles(PCamera cam, const std::vector<Light>& lights){
    if(!cam || gBufferWidth <= 0 || gBufferHeight <= 0){
        return false;
    }

    ensureDeferredLightTileResources(gBufferWidth, gBufferHeight);
    if(!deferredLightTileTexture || deferredLightTileTexture->getID() == 0 || deferredLightTileCpuData.empty()){
        return false;
    }

    const int tilesX = deferredLightTileGridWidth;
    const int tilesY = deferredLightTileGridHeight;
    const int tileStride = MAX_LIGHTS + 1;
    const Math3D::Mat4 clipMatrix = cam->getProjectionMatrix() * cam->getViewMatrix();

    for(size_t lightIndex = 0; lightIndex < lights.size() && lightIndex < MAX_LIGHTS; ++lightIndex){
        const Light& light = lights[lightIndex];
        int minTileX = 0;
        int minTileY = 0;
        int maxTileX = tilesX - 1;
        int maxTileY = tilesY - 1;

        if(light.type != LightType::DIRECTIONAL){
            float influenceRange = Math3D::Max(light.range, 0.1f);
            if(light.shadowRange > 0.0f){
                influenceRange = Math3D::Max(influenceRange, light.shadowRange);
            }

            const Math3D::Vec3 extent(influenceRange, influenceRange, influenceRange);
            if(!projectAabbToTileRect(
                light.position - extent,
                light.position + extent,
                clipMatrix,
                gBufferWidth,
                gBufferHeight,
                deferredLightTileSize,
                minTileX,
                minTileY,
                maxTileX,
                maxTileY
            )){
                continue;
            }
        }

        for(int tileY = minTileY; tileY <= maxTileY; ++tileY){
            for(int tileX = minTileX; tileX <= maxTileX; ++tileX){
                const int tileIndex = tileX + (tileY * tilesX);
                int& lightCount = deferredLightTileCpuData[static_cast<size_t>(tileIndex) * tileStride];
                if(lightCount < 0){
                    lightCount = 0;
                }
                if(lightCount >= MAX_LIGHTS){
                    continue;
                }
                const size_t writeIndex = (static_cast<size_t>(tileIndex) * tileStride) + 1 + static_cast<size_t>(lightCount);
                deferredLightTileCpuData[writeIndex] = static_cast<int>(lightIndex);
                ++lightCount;
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, deferredLightTileTexture->getID());
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        deferredLightTileTexture->getWidth(),
        deferredLightTileTexture->getHeight(),
        GL_RED_INTEGER,
        GL_INT,
        deferredLightTileCpuData.data()
    );
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Scene::clearLocalReflectionProbe(){
    deferredLocalReflectionProbe.valid = false;
    deferredLocalReflectionProbe.anchorEntityId.clear();
    deferredLocalReflectionProbe.center = Math3D::Vec3(0.0f, 0.0f, 0.0f);
    deferredLocalReflectionProbe.captureBoundsMin = Math3D::Vec3(0.0f, 0.0f, 0.0f);
    deferredLocalReflectionProbe.captureBoundsMax = Math3D::Vec3(0.0f, 0.0f, 0.0f);
    deferredLocalReflectionProbe.influenceBoundsMin = Math3D::Vec3(0.0f, 0.0f, 0.0f);
    deferredLocalReflectionProbe.influenceBoundsMax = Math3D::Vec3(0.0f, 0.0f, 0.0f);
    deferredLocalReflectionProbe.lastUpdateFrame = 0;
}

void Scene::releaseLocalReflectionProbeResources(){
    clearLocalReflectionProbe();
    deferredLocalReflectionProbe.cubeMap.reset();
    if(deferredLocalReflectionProbe.captureDepthRenderBuffer != 0){
        glDeleteRenderbuffers(1, &deferredLocalReflectionProbe.captureDepthRenderBuffer);
        deferredLocalReflectionProbe.captureDepthRenderBuffer = 0;
    }
    if(deferredLocalReflectionProbe.captureFbo != 0){
        glDeleteFramebuffers(1, &deferredLocalReflectionProbe.captureFbo);
        deferredLocalReflectionProbe.captureFbo = 0;
    }
    deferredLocalReflectionProbe.faceSize = 0;
}

bool Scene::ensureLocalReflectionProbeResources(int faceSize){
    if(faceSize <= 0){
        return false;
    }

    auto& probe = deferredLocalReflectionProbe;
    const bool needsProbeTexture =
        !probe.cubeMap ||
        probe.faceSize != faceSize ||
        probe.cubeMap->getID() == 0 ||
        probe.cubeMap->getSize() != faceSize;
    if(needsProbeTexture){
        probe.cubeMap = CubeMap::CreateRenderTarget(faceSize, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);
        probe.faceSize = faceSize;
    }

    if(probe.captureFbo == 0){
        glGenFramebuffers(1, &probe.captureFbo);
    }
    if(probe.captureDepthRenderBuffer == 0){
        glGenRenderbuffers(1, &probe.captureDepthRenderBuffer);
    }
    if(!probe.cubeMap || probe.captureFbo == 0 || probe.captureDepthRenderBuffer == 0){
        return false;
    }

    glBindRenderbuffer(GL_RENDERBUFFER, probe.captureDepthRenderBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, faceSize, faceSize);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, probe.captureFbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        probe.cubeMap->getID(),
        0
    );
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER,
        probe.captureDepthRenderBuffer
    );
    const GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, drawBuffers);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    const bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return complete;
}

void Scene::clearPlanarReflection(){
    activePlanarReflection.valid = false;
    activePlanarReflection.entityId.clear();
    activePlanarReflection.center = Math3D::Vec3(0.0f, 0.0f, 0.0f);
    activePlanarReflection.normal = Math3D::Vec3(0.0f, 1.0f, 0.0f);
    activePlanarReflection.viewProjection = Math3D::Mat4();
    activePlanarReflection.strength = 1.0f;
    activePlanarReflection.receiverFadeDistance = 1.0f;
}

bool Scene::ensurePlanarReflectionResources(int width, int height){
    if(width <= 0 || height <= 0){
        return false;
    }

    const bool needsResize =
        !activePlanarReflection.buffer ||
        activePlanarReflection.buffer->getWidth() != width ||
        activePlanarReflection.buffer->getHeight() != height ||
        !activePlanarReflection.buffer->getTexture();

    if(needsResize){
        activePlanarReflection.buffer = FrameBuffer::Create(width, height);
        if(activePlanarReflection.buffer){
            activePlanarReflection.buffer->attachTexture(
                Texture::CreateRenderTarget(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT)
            );
            if(activePlanarReflection.buffer->getTexture()){
                glBindTexture(GL_TEXTURE_2D, activePlanarReflection.buffer->getTexture()->getID());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glGenerateMipmap(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            if(!activePlanarReflection.buffer->validate()){
                activePlanarReflection.buffer.reset();
            }
        }
    }

    return activePlanarReflection.buffer && activePlanarReflection.buffer->getTexture();
}

bool Scene::updatePlanarReflection(PScreen screen, PCamera cam){
    clearPlanarReflection();
    if(!screen || !cam || cam->getSettings().isOrtho){
        return false;
    }
    if(!ensurePlanarReflectionResources(screen->getWidth(), screen->getHeight())){
        return false;
    }

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    const Math3D::Mat4 viewMatrix = cam->getViewMatrix();
    const Math3D::Mat4 projectionMatrix = cam->getProjectionMatrix();
    const Math3D::Mat4 clipMatrix = projectionMatrix * viewMatrix;
    const Math3D::Vec3 cameraPosition = cam->transform().position;

    const RenderItem* bestItem = nullptr;
    Math3D::Vec3 bestCenter;
    Math3D::Vec3 bestNormal = Math3D::Vec3(0.0f, 1.0f, 0.0f);
    float bestReflectivity = 1.0f;
    float bestReceiverFadeDistance = 1.0f;
    float bestScore = -1.0f;
    auto resolvePlanarReflectionSurface = [&](const RenderItem& item,
                                              Math3D::Vec3& outCenter,
                                              Math3D::Vec3& outNormal,
                                              float& outReceiverFadeDistance) -> bool {
        if(!item.hasBounds){
            return false;
        }

        outCenter = (item.boundsMin + item.boundsMax) * 0.5f;
        Math3D::Vec3 worldExtents = item.boundsMax - item.boundsMin;
        Math3D::Vec3 worldNormal = Math3D::Vec3::up();
        float dominantExtent = Math3D::Max(
            Math3D::Max(worldExtents.x, worldExtents.y),
            Math3D::Max(worldExtents.z, 0.25f)
        );

        Math3D::Vec3 localMin;
        Math3D::Vec3 localMax;
        if(item.mesh && item.mesh->getLocalBounds(localMin, localMax)){
            const Math3D::Vec3 localExtents = localMax - localMin;
            dominantExtent = Math3D::Max(
                dominantExtent,
                Math3D::Max(Math3D::Max(localExtents.x, localExtents.y), localExtents.z)
            );

            Math3D::Vec3 localNormal = Math3D::Vec3::up();
            switch(findSmallestExtentAxis(localExtents)){
                case 0:
                    localNormal = Math3D::Vec3::right();
                    break;
                case 1:
                    localNormal = Math3D::Vec3::up();
                    break;
                default:
                    localNormal = Math3D::Vec3::forward();
                    break;
            }
            worldNormal = transformDirection(item.model, localNormal);
        }else{
            switch(findSmallestExtentAxis(worldExtents)){
                case 0:
                    worldNormal = Math3D::Vec3::right();
                    break;
                case 1:
                    worldNormal = Math3D::Vec3::up();
                    break;
                default:
                    worldNormal = Math3D::Vec3::forward();
                    break;
            }
        }

        worldNormal = safeNormalizeVec3(worldNormal, Math3D::Vec3::up());
        outNormal = worldNormal;
        outReceiverFadeDistance = Math3D::Clamp(Math3D::Max(dominantExtent * 0.45f, 1.0f), 1.0f, 30.0f);
        return true;
    };

    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material || !item.planarReflectionSource || !item.hasBounds){
            continue;
        }

        Math3D::Vec3 center;
        Math3D::Vec3 worldNormal;
        float receiverFadeDistance = 1.0f;
        if(!resolvePlanarReflectionSurface(item, center, worldNormal, receiverFadeDistance)){
            continue;
        }

        const Math3D::Vec3 worldExtents = item.boundsMax - item.boundsMin;
        if(Math3D::Max(Math3D::Max(worldExtents.x, worldExtents.y), worldExtents.z) <= 0.10f){
            continue;
        }

        Math3D::Vec3 toCamera = cameraPosition - center;
        const float distanceToCenter = Math3D::Max(toCamera.length(), 0.25f);
        Math3D::Vec3 viewDirection = safeNormalizeVec3(toCamera, worldNormal);
        const float facingWeight = Math3D::Clamp(std::abs(Math3D::Vec3::dot(worldNormal, viewDirection)), 0.0f, 1.0f);
        if(facingWeight < 0.02f){
            continue;
        }

        const glm::vec4 clipCenter = static_cast<glm::mat4>(clipMatrix) * glm::vec4(center.x, center.y, center.z, 1.0f);
        if(clipCenter.w <= 1e-4f){
            continue;
        }

        const glm::vec3 ndc = glm::vec3(clipCenter) / clipCenter.w;
        if(ndc.z < -0.2f || ndc.z > 1.2f){
            continue;
        }

        const float screenDistance = std::sqrt((ndc.x * ndc.x) + (ndc.y * ndc.y));
        if(screenDistance > 1.75f){
            continue;
        }

        const float centerWeight = Math3D::Clamp(1.0f - (screenDistance / 1.75f), 0.0f, 1.0f);
        const float projectedRadius = Math3D::Clamp(((item.boundsMax - item.boundsMin).length() * 0.5f / distanceToCenter) * 6.0f, 0.10f, 2.50f);
        const float score =
            (0.25f + (0.75f * facingWeight)) *
            projectedRadius *
            (0.25f + (0.75f * centerWeight));
        if(score <= bestScore){
            continue;
        }

        bestItem = &item;
        bestCenter = center;
        bestNormal = worldNormal;
        bestReceiverFadeDistance = receiverFadeDistance;
        bestReflectivity = 1.0f;
        if(auto pbr = Material::GetAs<PBRMaterial>(item.material)){
            bestReflectivity = Math3D::Max(1.0f, computePlanarReflectivityScore(pbr));
        }
        bestScore = score;
    }

    if(!bestItem || !activePlanarReflection.buffer || !activePlanarReflection.buffer->getTexture()){
        return false;
    }

    activePlanarReflection.entityId = bestItem->entityId;
    activePlanarReflection.center = bestCenter;
    activePlanarReflection.normal = bestNormal;
    activePlanarReflection.strength = Math3D::Clamp(0.75f + (bestReflectivity * 0.18f), 0.85f, 1.35f);
    activePlanarReflection.receiverFadeDistance = bestReceiverFadeDistance;
    Math3D::Vec3 captureNormal = bestNormal;
    if(Math3D::Vec3::dot(captureNormal, cameraPosition - bestCenter) < 0.0f){
        captureNormal = captureNormal * -1.0f;
    }

    auto reflectionCamera = Camera::CreatePerspective(
        cam->getSettings().fov,
        Math3D::Vec2(static_cast<float>(screen->getWidth()), static_cast<float>(screen->getHeight())),
        cam->getSettings().nearPlane,
        cam->getSettings().farPlane
    );
    if(!reflectionCamera){
        clearPlanarReflection();
        return false;
    }

    reflectionCamera->getSettings() = cam->getSettings();
    reflectionCamera->resize(static_cast<float>(screen->getWidth()), static_cast<float>(screen->getHeight()));

    const Math3D::Vec3 sourceForward = safeNormalizeVec3(cam->transform().forward(), Math3D::Vec3::forward());
    const Math3D::Vec3 sourceUp = safeNormalizeVec3(cam->transform().up(), Math3D::Vec3::up());
    const Math3D::Vec3 reflectedPosition = reflectPointAcrossPlane(cameraPosition, bestCenter, bestNormal);
    const Math3D::Vec3 reflectedForward = safeNormalizeVec3(
        reflectDirectionAcrossPlane(sourceForward, bestNormal),
        sourceForward * -1.0f
    );
    Math3D::Vec3 reflectedUp = safeNormalizeVec3(
        reflectDirectionAcrossPlane(sourceUp, bestNormal),
        Math3D::Vec3::up()
    );
    if(std::abs(Math3D::Vec3::dot(reflectedForward, reflectedUp)) > 0.995f){
        reflectedUp = safeNormalizeVec3(reflectDirectionAcrossPlane(cam->transform().right(), bestNormal), Math3D::Vec3::up());
        reflectedUp = safeNormalizeVec3(Math3D::Vec3::cross(reflectedUp, reflectedForward), Math3D::Vec3::up());
    }

    reflectionCamera->transform().position = reflectedPosition;
    reflectionCamera->transform().lookAt(reflectedPosition + reflectedForward, reflectedUp);

    auto previousCamera = Screen::GetCurrentCamera();
    auto previousEnvironment = Screen::GetCurrentEnvironment();
    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    GLint previousCullFace = GL_BACK;
    GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean wasCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean wasBlend = glIsEnabled(GL_BLEND);
    GLboolean wasClipDistance0 = glIsEnabled(GL_CLIP_DISTANCE0);
    GLboolean previousDepthMask = GL_TRUE;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullFace);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

    activePlanarReflection.buffer->bind();
    activePlanarReflection.buffer->clear(screen->getClearColor());

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    Screen::MakeCameraCurrent(reflectionCamera);
    Screen::MakeEnvironmentCurrent(screen->getEnvironment());

    drawSkybox(reflectionCamera, true);

    userClipPlaneActive = true;
    userClipPlane = makePlaneEquation(
        bestCenter,
        captureNormal,
        Math3D::Max(reflectionCamera->getSettings().nearPlane * 0.35f, 0.01f)
    );
    glEnable(GL_CLIP_DISTANCE0);
    drawModels3D(reflectionCamera, RenderFilter::Opaque, false, &activePlanarReflection.entityId);
    userClipPlaneActive = false;
    if(!wasClipDistance0){
        glDisable(GL_CLIP_DISTANCE0);
    }

    if(activePlanarReflection.buffer->getTexture()){
        glBindTexture(GL_TEXTURE_2D, activePlanarReflection.buffer->getTexture()->getID());
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    activePlanarReflection.viewProjection = reflectionCamera->getProjectionMatrix() * reflectionCamera->getViewMatrix();
    activePlanarReflection.valid = true;

    if(previousFramebuffer != 0){
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    }else{
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    if(wasDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if(wasCullFace){
        glEnable(GL_CULL_FACE);
        glCullFace(previousCullFace);
    }else{
        glDisable(GL_CULL_FACE);
    }
    if(wasBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glDepthMask(previousDepthMask);
    Screen::MakeCameraCurrent(previousCamera);
    Screen::MakeEnvironmentCurrent(previousEnvironment);
    return true;
}

bool Scene::updateLocalReflectionProbe(PScreen screen, PCamera cam){
    if(!screen || !cam || cam->getSettings().isOrtho){
        clearLocalReflectionProbe();
        return false;
    }

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    if(snapshot.reflectionProbes.empty()){
        clearLocalReflectionProbe();
        return false;
    }

    const Math3D::Vec3 cameraPosition = cam->transform().position;
    const ReflectionProbeSnapshot* bestProbe = nullptr;
    float bestScore = -FLT_MAX;
    for(const auto& candidate : snapshot.reflectionProbes){
        const float influenceDistance = distancePointToAabb(
            cameraPosition,
            candidate.influenceBoundsMin,
            candidate.influenceBoundsMax
        );
        const bool insideInfluence = influenceDistance <= 1e-4f;
        const float centerDistance = Math3D::Max((candidate.center - cameraPosition).length(), 0.1f);
        float score = static_cast<float>(candidate.priority) * 1000.0f;
        score += insideInfluence ? 500.0f : 0.0f;
        score += 220.0f / (1.0f + influenceDistance);
        score += 25.0f / (1.0f + centerDistance);
        if(score > bestScore){
            bestScore = score;
            bestProbe = &candidate;
        }
    }

    if(!bestProbe){
        clearLocalReflectionProbe();
        return false;
    }

    const int requestedFaceSize = Math3D::Clamp(bestProbe->resolution, 64, 512);
    if(!ensureLocalReflectionProbeResources(requestedFaceSize)){
        clearLocalReflectionProbe();
        return false;
    }

    auto& probe = deferredLocalReflectionProbe;
    auto nearlyEqualVec3 = [](const Math3D::Vec3& a, const Math3D::Vec3& b) -> bool {
        return Math3D::Vec3::distance(a, b) <= 0.01f;
    };

    auto captureProbeFaces = [&](const std::string* excludedEntityId) -> bool {
        const Math3D::Vec3 captureExtent = (probe.captureBoundsMax - probe.captureBoundsMin) * 0.5f;
        const float captureRadius = captureExtent.length();
        const float nearPlane = Math3D::Clamp(captureRadius * 0.035f, 0.03f, 0.30f);
        const float farPlane = Math3D::Min(
            Math3D::Max(captureRadius * 10.0f, 12.0f),
            Math3D::Max(cam->getSettings().farPlane, 12.0f)
        );

        GLint previousFramebuffer = 0;
        GLint previousViewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        auto previousCamera = Screen::GetCurrentCamera();
        auto previousEnvironment = Screen::GetCurrentEnvironment();

        glBindFramebuffer(GL_FRAMEBUFFER, probe.captureFbo);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            GL_RENDERBUFFER,
            probe.captureDepthRenderBuffer
        );

        localReflectionProbeCaptureActive = true;
        const GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
        for(int face = 0; face < 6; ++face){
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                probe.cubeMap->getID(),
                0
            );
            glDrawBuffers(1, drawBuffers);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
                localReflectionProbeCaptureActive = false;
                glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
                glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
                Screen::MakeCameraCurrent(previousCamera);
                Screen::MakeEnvironmentCurrent(previousEnvironment);
                clearLocalReflectionProbe();
                return false;
            }

            auto faceCamera = Camera::CreatePerspective(
                90.0f,
                Math3D::Vec2(static_cast<float>(probe.faceSize), static_cast<float>(probe.faceSize)),
                nearPlane,
                farPlane
            );
            if(!faceCamera){
                continue;
            }

            faceCamera->transform().position = probe.center;
            faceCamera->transform().lookAt(probe.center + kDeferredSsrLocalProbeDirs[face], kDeferredSsrLocalProbeUps[face]);
            Screen::MakeCameraCurrent(faceCamera);
            Screen::MakeEnvironmentCurrent(screen->getEnvironment());

            glViewport(0, 0, probe.faceSize, probe.faceSize);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glClearColor(
                screen->getClearColor().getRed(),
                screen->getClearColor().getGreen(),
                screen->getClearColor().getBlue(),
                screen->getClearColor().getAlpha()
            );
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            drawSkybox(faceCamera, true);
            drawModels3D(faceCamera, RenderFilter::Opaque, false, excludedEntityId);
        }

        localReflectionProbeCaptureActive = false;
        probe.cubeMap->generateMipmaps();
        glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        Screen::MakeCameraCurrent(previousCamera);
        Screen::MakeEnvironmentCurrent(previousEnvironment);
        probe.valid = true;
        probe.lastUpdateFrame = reflectionCaptureFrameCounter;
        return true;
    };

    const bool sameProbe = (probe.anchorEntityId == bestProbe->entityId);
    const bool boundsChanged =
        !sameProbe ||
        !nearlyEqualVec3(probe.center, bestProbe->center) ||
        !nearlyEqualVec3(probe.captureBoundsMin, bestProbe->captureBoundsMin) ||
        !nearlyEqualVec3(probe.captureBoundsMax, bestProbe->captureBoundsMax) ||
        !nearlyEqualVec3(probe.influenceBoundsMin, bestProbe->influenceBoundsMin) ||
        !nearlyEqualVec3(probe.influenceBoundsMax, bestProbe->influenceBoundsMax);
    const bool intervalElapsed =
        bestProbe->autoUpdate &&
        (!probe.valid ||
         ((reflectionCaptureFrameCounter - probe.lastUpdateFrame) >=
          static_cast<unsigned long long>(Math3D::Clamp(bestProbe->updateIntervalFrames, 1, 240))));

    probe.anchorEntityId = bestProbe->entityId;
    probe.center = bestProbe->center;
    probe.captureBoundsMin = bestProbe->captureBoundsMin;
    probe.captureBoundsMax = bestProbe->captureBoundsMax;
    probe.influenceBoundsMin = bestProbe->influenceBoundsMin;
    probe.influenceBoundsMax = bestProbe->influenceBoundsMax;

    if(probe.valid && !boundsChanged && !intervalElapsed && probe.faceSize == requestedFaceSize){
        return true;
    }

    const std::string* excludedEntityId = probe.anchorEntityId.empty() ? nullptr : &probe.anchorEntityId;
    return captureProbeFaces(excludedEntityId);
}

Scene::~Scene(){
    clearPlanarReflection();
    activePlanarReflection.buffer.reset();
    releaseLocalReflectionProbeResources();
    if(assetChangeListenerHandle >= 0){
        AssetManager::Instance.removeChangeListener(assetChangeListenerHandle);
        assetChangeListenerHandle = -1;
    }
}

void Scene::ensureOutlineResources(PScreen screen){
    if(!screen){
        return;
    }

    const int w = screen->getWidth();
    const int h = screen->getHeight();
    if(w <= 0 || h <= 0){
        return;
    }

    if(!outlineMaskBuffer){
        outlineMaskBuffer = FrameBuffer::Create(w, h);
        outlineMaskWidth = 0;
        outlineMaskHeight = 0;
    }else if(outlineMaskWidth != w || outlineMaskHeight != h){
        outlineMaskBuffer->resize(w, h);
    }

    if(!outlineMaskBuffer->getTexture() || outlineMaskWidth != w || outlineMaskHeight != h){
        outlineMaskBuffer->attachTexture(Texture::CreateEmpty(w, h));
        outlineMaskWidth = w;
        outlineMaskHeight = h;
        if(!outlineMaskBuffer->validate()){
            LogBot.Log(LOG_ERRO, "Selection outline mask framebuffer is invalid.");
        }
    }

    if(!outlineMaskShader){
        outlineMaskShader = compileInlineShaderProgram(
            "SelectionOutlineMask",
            kSelectionOutlineMaskVertShader,
            kSelectionOutlineMaskFragShader
        );
    }

    if(!outlineCompositeShader){
        outlineCompositeShader = compileInlineShaderProgram(
            "SelectionOutlineComposite",
            Graphics::ShaderDefaults::SCREEN_VERT_SRC,
            kSelectionOutlineCompositeFragShader
        );
    }

    if(!outlineCompositeQuad){
        try{
            outlineCompositeQuad = buildScreenQuadModelPart();
        }catch(const std::exception& e){
            LogBot.Log(LOG_ERRO, "Selection outline quad creation failed: %s", e.what());
        }catch(...){
            LogBot.Log(LOG_ERRO, "Selection outline quad creation failed: unknown exception");
        }
    }
}

void Scene::drawDeferredGeometry(PCamera cam, const std::string* excludedEntityId){
    if(!cam || !gBuffer || !gBufferShader || gBufferShader->getID() == 0) return;

    gBuffer->bind();
    gBuffer->clear(Color::CLEAR);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    gBufferShader->bind();
    const Math3D::Mat4 viewMatrix = cam->getViewMatrix();
    const Math3D::Mat4 projectionMatrix = cam->getProjectionMatrix();
    const Math3D::Mat4 clipMatrix = projectionMatrix * viewMatrix;
    gBufferShader->setUniformFast("u_view", Uniform<Math3D::Mat4>(viewMatrix));
    gBufferShader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(projectionMatrix));
    gBufferShader->setUniformFast("u_viewPos", Uniform<Math3D::Vec3>(cam->transform().position));
    static const std::chrono::steady_clock::time_point kWaveStartTime = std::chrono::steady_clock::now();
    const float shaderTimeSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - kWaveStartTime).count();
    gBufferShader->setUniformFast("u_time", Uniform<float>(shaderTimeSeconds));

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    std::vector<const RenderItem*> deferredItems;
    deferredItems.reserve(snapshot.drawItems.size());
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material) continue;
        if(excludedEntityId && !excludedEntityId->empty() && item.entityId == *excludedEntityId) continue;
        if(item.isTransparent) continue;
        if(!item.isDeferredCompatible) continue;
        if(item.hasBounds && !aabbIntersectsClipFrustum(item.boundsMin, item.boundsMax, clipMatrix)) continue;
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
            PTexture emissiveTex = nullptr;
            int useEmissiveTex = 0;
            Math3D::Vec3 emissiveColor(0.0f, 0.0f, 0.0f);
            float emissiveStrength = 1.0f;
            float envStrength = 0.55f;
            Math3D::Vec2 uvScale(1.0f, 1.0f);
            Math3D::Vec2 uvOffset(0.0f, 0.0f);
            int useAlphaClip = 0;
            float alphaCutoff = 0.5f;
            float transmission = 0.0f;
            int enableWaveDisplacement = 0;
            float waveAmplitude = 0.0f;
            float waveFrequency = 1.0f;
            float waveSpeed = 0.75f;
            float waveChoppiness = 0.25f;
            float waveSecondaryScale = 1.0f;
            Math3D::Vec2 waveDirection(0.85f, 0.45f);
            float waveTextureInfluence = 0.6f;
            Math3D::Vec2 waveTextureSpeed(0.03f, 0.01f);
            int bsdfModel = static_cast<int>(PBRBsdfModel::Standard);
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
                emissiveTex = pbr->EmissiveTex.get();
                useEmissiveTex = emissiveTex ? 1 : 0;
                emissiveColor = pbr->EmissiveColor.get();
                emissiveStrength = pbr->EmissiveStrength.get();
                envStrength = pbr->EnvStrength.get();
                uvScale = pbr->UVScale.get();
                uvOffset = pbr->UVOffset.get();
                useAlphaClip = pbr->UseAlphaClip.get();
                alphaCutoff = pbr->AlphaCutoff.get();
                transmission = pbr->Transmission.get();
                enableWaveDisplacement = pbr->EnableWaveDisplacement.get();
                waveAmplitude = pbr->WaveAmplitude.get();
                waveFrequency = pbr->WaveFrequency.get();
                waveSpeed = pbr->WaveSpeed.get();
                waveChoppiness = pbr->WaveChoppiness.get();
                waveSecondaryScale = pbr->WaveSecondaryScale.get();
                waveDirection = pbr->WaveDirection.get();
                waveTextureInfluence = pbr->WaveTextureInfluence.get();
                waveTextureSpeed = pbr->WaveTextureSpeed.get();
                bsdfModel = Math3D::Clamp(pbr->BsdfModel.get(), 0, 2);
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

            if(bsdfModel != static_cast<int>(PBRBsdfModel::Standard) && transmission <= 0.0001f){
                transmission = Math3D::Clamp(1.0f - baseColor.w, 0.0f, 1.0f);
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

            gBufferShader->setUniformFast("u_useEmissiveTex", Uniform<int>(useEmissiveTex));
            gBufferShader->setUniformFast("u_emissiveTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(emissiveTex, 6)));
            gBufferShader->setUniformFast("u_emissiveColor", Uniform<Math3D::Vec3>(emissiveColor));
            gBufferShader->setUniformFast("u_emissiveStrength", Uniform<float>(emissiveStrength));
            gBufferShader->setUniformFast("u_envStrength", Uniform<float>(envStrength));

            gBufferShader->setUniformFast("u_uvScale", Uniform<Math3D::Vec2>(uvScale));
            gBufferShader->setUniformFast("u_uvOffset", Uniform<Math3D::Vec2>(uvOffset));
            gBufferShader->setUniformFast("u_useAlphaClip", Uniform<int>(useAlphaClip));
            gBufferShader->setUniformFast("u_alphaCutoff", Uniform<float>(alphaCutoff));
            gBufferShader->setUniformFast("u_transmission", Uniform<float>(Math3D::Clamp(transmission, 0.0f, 1.0f)));
            gBufferShader->setUniformFast("u_enableWaveDisplacement", Uniform<int>(enableWaveDisplacement != 0 ? 1 : 0));
            gBufferShader->setUniformFast("u_waveAmplitude", Uniform<float>(Math3D::Max(0.0f, waveAmplitude)));
            gBufferShader->setUniformFast("u_waveFrequency", Uniform<float>(Math3D::Max(0.0f, waveFrequency)));
            gBufferShader->setUniformFast("u_waveSpeed", Uniform<float>(waveSpeed));
            gBufferShader->setUniformFast("u_waveChoppiness", Uniform<float>(Math3D::Clamp(waveChoppiness, 0.0f, 1.0f)));
            gBufferShader->setUniformFast("u_waveSecondaryScale", Uniform<float>(Math3D::Max(0.01f, waveSecondaryScale)));
            gBufferShader->setUniformFast("u_waveDirection", Uniform<Math3D::Vec2>(waveDirection));
            gBufferShader->setUniformFast("u_waveTextureInfluence", Uniform<float>(Math3D::Max(0.0f, waveTextureInfluence)));
            gBufferShader->setUniformFast("u_waveTextureSpeed", Uniform<Math3D::Vec2>(waveTextureSpeed));
            gBufferShader->setUniformFast("u_bsdfModel", Uniform<int>(bsdfModel));
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

void Scene::drawDeferredLighting(PFrameBuffer targetBuffer,
                                 Color clearColor,
                                 PCamera cam,
                                 const std::shared_ptr<DeferredSSAO>& ssaoPass,
                                 const DeferredSSAOSettings* ssaoSettings,
                                 PTexture giTexture,
                                  int lightPassMode){
    if(!targetBuffer || !cam || !gBuffer || !deferredLightShader || deferredLightShader->getID() == 0 || !deferredQuad) return;

    targetBuffer->bind();
    targetBuffer->clear(clearColor);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    deferredLightShader->bind();

    const Math3D::Mat4 viewMatrix = cam->getViewMatrix();
    const Math3D::Mat4 projectionMatrix = cam->getProjectionMatrix();
    const Math3D::Mat4 inverseProjectionMatrix = Math3D::Mat4(glm::inverse(glm::mat4(projectionMatrix)));
    const Math3D::Mat4 inverseViewMatrix = Math3D::Mat4(glm::inverse(glm::mat4(viewMatrix)));
    const int ssaoDebugView = (lightPassMode == 0 && ssaoSettings) ? Math3D::Clamp(ssaoSettings->debugView, 0, 4) : 0;
    deferredLightShader->setUniformFast("u_viewPos", Uniform<Math3D::Vec3>(cam->transform().position));
    deferredLightShader->setUniformFast("u_cameraView", Uniform<Math3D::Mat4>(viewMatrix));
    deferredLightShader->setUniformFast("u_invProjection", Uniform<Math3D::Mat4>(inverseProjectionMatrix));
    deferredLightShader->setUniformFast("u_invView", Uniform<Math3D::Mat4>(inverseViewMatrix));
    deferredLightShader->setUniformFast("gAlbedo", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(gBuffer->getGBufferTexture(0), 0)));
    deferredLightShader->setUniformFast("gNormal", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(gBuffer->getGBufferTexture(1), 1)));
    deferredLightShader->setUniformFast("gMaterial", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(gBuffer->getGBufferTexture(2), 2)));
    // Use a dedicated slot for gSurface. It must not alias u_envMap or shadow samplers.
    deferredLightShader->setUniformFast("gSurface", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(gBuffer->getGBufferTexture(3), 8)));
    deferredLightShader->setUniformFast("gDepth", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(gBuffer->getDepthTexture(), 3)));
    deferredLightShader->setUniformFast("gTileLightData", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(deferredLightTileTexture, 4)));
    PTexture ssaoRawTex = ssaoPass ? ssaoPass->getRawAoTexture() : nullptr;
    PTexture ssaoBlurTex = ssaoPass ? ssaoPass->getBlurAoTexture() : nullptr;
    int useSsao = (ssaoRawTex && ssaoBlurTex) ? 1 : 0;
    PTexture sharedAuxTexture = giTexture;
    if(!sharedAuxTexture || ssaoDebugView == 2){
        sharedAuxTexture = ssaoRawTex;
    }
    deferredLightShader->setUniformFast("gSsaoRaw", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(sharedAuxTexture, 6)));
    deferredLightShader->setUniformFast("gSsao", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(ssaoBlurTex, 5)));
    deferredLightShader->setUniformFast("gGi", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(sharedAuxTexture, 6)));
    deferredLightShader->setUniformFast("u_useSsao", Uniform<int>(useSsao));
    deferredLightShader->setUniformFast("u_ssaoIntensity", Uniform<float>(ssaoSettings ? Math3D::Clamp(ssaoSettings->intensity, 0.0f, 10.0f) : 0.0f));
    deferredLightShader->setUniformFast("u_useGi", Uniform<int>(giTexture ? 1 : 0));
    deferredLightShader->setUniformFast("u_ssaoDebugView", Uniform<int>(ssaoDebugView));
    deferredLightShader->setUniformFast("u_lightPassMode", Uniform<int>(lightPassMode));
    // Tile-light culling can introduce visible screen-space boundaries on broad surfaces
    // when light bounds projection misses edge tiles. Keep visual output stable by
    // using the full light list in shading until tiled culling is reworked.
    deferredLightShader->setUniformFast("u_useLightTiles", Uniform<int>(0));
    deferredLightShader->setUniformFast("u_tileGrid", Uniform<Math3D::Vec2>(Math3D::Vec2(
        static_cast<float>(deferredLightTileGridWidth),
        static_cast<float>(deferredLightTileGridHeight)
    )));
    deferredLightShader->setUniformFast("u_tileSize", Uniform<int>(deferredLightTileSize));

    auto env = Screen::GetCurrentEnvironment();
    EnvironmentSettings environmentSettings;
    if(env){
        environmentSettings = env->getSettings();
    }
    PCubeMap envMap = (env && env->getSkyBox()) ? env->getSkyBox()->getCubeMap() : nullptr;
    const bool useLocalProbe =
        deferredLocalReflectionProbe.valid &&
        deferredLocalReflectionProbe.cubeMap &&
        deferredLocalReflectionProbe.cubeMap->getID() != 0;
    deferredLightShader->setUniformFast("u_useEnvMap", Uniform<int>(envMap ? 1 : 0));
    deferredLightShader->setUniformFast("u_envMap", Uniform<GLUniformUpload::CubeMapSlot>(GLUniformUpload::CubeMapSlot(envMap, 7)));
    deferredLightShader->setUniformFast("u_useLocalProbe", Uniform<int>(useLocalProbe ? 1 : 0));
    deferredLightShader->setUniformFast("u_localProbe", Uniform<GLUniformUpload::CubeMapSlot>(GLUniformUpload::CubeMapSlot(
        useLocalProbe ? deferredLocalReflectionProbe.cubeMap : nullptr,
        9
    )));
    deferredLightShader->setUniformFast("u_localProbeCenter", Uniform<Math3D::Vec3>(deferredLocalReflectionProbe.center));
    deferredLightShader->setUniformFast("u_localProbeCaptureMin", Uniform<Math3D::Vec3>(deferredLocalReflectionProbe.captureBoundsMin));
    deferredLightShader->setUniformFast("u_localProbeCaptureMax", Uniform<Math3D::Vec3>(deferredLocalReflectionProbe.captureBoundsMax));
    deferredLightShader->setUniformFast("u_localProbeInfluenceMin", Uniform<Math3D::Vec3>(deferredLocalReflectionProbe.influenceBoundsMin));
    deferredLightShader->setUniformFast("u_localProbeInfluenceMax", Uniform<Math3D::Vec3>(deferredLocalReflectionProbe.influenceBoundsMax));
    deferredLightShader->setUniformFast("u_ambientColor", Uniform<Math3D::Vec4>(environmentSettings.ambientColor));
    deferredLightShader->setUniformFast("u_ambientIntensity", Uniform<float>(environmentSettings.ambientIntensity));
    deferredLightShader->setUniformFast("u_fogEnabled", Uniform<int>(environmentSettings.fogEnabled ? 1 : 0));
    deferredLightShader->setUniformFast("u_fogColor", Uniform<Math3D::Vec4>(environmentSettings.fogColor));
    deferredLightShader->setUniformFast("u_fogStart", Uniform<float>(environmentSettings.fogStart));
    deferredLightShader->setUniformFast("u_fogStop", Uniform<float>(environmentSettings.fogStop));
    deferredLightShader->setUniformFast("u_fogEnd", Uniform<float>(environmentSettings.fogEnd));
    static const std::vector<Light> EMPTY_LIGHTS;
    const std::vector<Light>& lights = (env && env->isLightingEnabled()) ? env->getLightsForUpload() : EMPTY_LIGHTS;
    LightUniformUploader::UploadLights(deferredLightShader, lights);
    ShadowRenderer::BindShadowSamplers(deferredLightShader);

    static const Math3D::Mat4 IDENTITY;
    deferredLightShader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
    deferredLightShader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
    deferredLightShader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));

    deferredQuad->draw(IDENTITY, IDENTITY, IDENTITY);
}

void Scene::drawOutlines(PScreen screen, PCamera cam){
    if(!screen || !cam){
        return;
    }
    if(!outlineEnabled || selectedEntityId.empty()){
        return;
    }

    ensureOutlineResources(screen);
    if(!outlineMaskBuffer ||
       !outlineMaskShader ||
       outlineMaskShader->getID() == 0 ||
       !outlineCompositeShader ||
       outlineCompositeShader->getID() == 0 ||
       !outlineCompositeQuad){
        return;
    }

    auto drawBuffer = screen->getDrawBuffer();
    auto outlineMaskTexture = outlineMaskBuffer->getTexture();
    if(!drawBuffer || !outlineMaskTexture){
        return;
    }

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    const Math3D::Mat4 viewMatrix = cam->getViewMatrix();
    const Math3D::Mat4 projectionMatrix = cam->getProjectionMatrix();

    outlineMaskBuffer->bind();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, drawBuffer->getID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, outlineMaskBuffer->getID());
    glBlitFramebuffer(
        0, 0, drawBuffer->getWidth(), drawBuffer->getHeight(),
        0, 0, outlineMaskBuffer->getWidth(), outlineMaskBuffer->getHeight(),
        GL_DEPTH_BUFFER_BIT, GL_NEAREST
    );
    outlineMaskBuffer->bind();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // Keep mask coverage stable even when depth differs slightly from the main pass
    // (for example from deferred/forward path differences or animated surfaces).
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -2.0f);

    outlineMaskShader->bind();
    outlineMaskShader->setUniformFast("u_view", Uniform<Math3D::Mat4>(viewMatrix));
    outlineMaskShader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(projectionMatrix));

    bool drewMask = false;
    bool cullStateKnown = false;
    bool cullEnabled = true;
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material){
            continue;
        }
        if(item.entityId != selectedEntityId){
            continue;
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

        outlineMaskShader->setUniformFast("u_model", Uniform<Math3D::Mat4>(item.model));
        item.mesh->draw();
        drewMask = true;
    }

    drawBuffer->bind();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_CULL_FACE);
    if(!drewMask){
        glDepthFunc(GL_LESS);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    outlineCompositeShader->bind();
    outlineCompositeShader->setUniformFast(
        "u_maskTexture",
        Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(outlineMaskTexture, 0))
    );
    outlineCompositeShader->setUniformFast("u_outlineColor", Uniform<Math3D::Vec4>(kSelectionOutlineColor));

    static const Math3D::Mat4 IDENTITY;
    outlineCompositeShader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
    outlineCompositeShader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
    outlineCompositeShader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));
    outlineCompositeQuad->draw(IDENTITY, IDENTITY, IDENTITY);

    glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
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
    if(!gBuffer || gBuffer->getGBufferCount() < 4){
        invalid = true;
    }else if(!gBuffer->getGBufferTexture(0) ||
             !gBuffer->getGBufferTexture(1) ||
             !gBuffer->getGBufferTexture(2) ||
             !gBuffer->getGBufferTexture(3) ||
             !gBuffer->getDepthTexture()){
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

    const std::string* deferredExcludedEntityId =
        (activePlanarReflection.valid && !activePlanarReflection.entityId.empty())
            ? &activePlanarReflection.entityId
            : nullptr;
    drawDeferredGeometry(cam, deferredExcludedEntityId);
    checkGlError("geometry pass");
    if(deferredDisabled){
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    NeoECS::ECSEntity* deferredCameraEntity = activeCameraEntity;
    if(ecsInstance && cam){
        auto* manager = ecsInstance->getComponentManager();
        auto* entityManager = ecsInstance->getEntityManager();
        auto cameraMatches = [&](NeoECS::ECSEntity* entity) -> bool {
            if(!manager || !entity){
                return false;
            }
            auto* cameraComponent = manager->getECSComponent<CameraComponent>(entity);
            return cameraComponent &&
                   IsComponentActive(cameraComponent) &&
                   cameraComponent->camera == cam;
        };

        if(!cameraMatches(deferredCameraEntity) && manager && entityManager){
            const auto& entities = entityManager->getEntities();
            for(const auto& entityPtr : entities){
                auto* entity = entityPtr.get();
                if(cameraMatches(entity)){
                    deferredCameraEntity = entity;
                    this->activeCameraEntity = entity;
                    break;
                }
            }
        }
    }

    reflectionCaptureFrameCounter++;

    DeferredSSRSettings ssrSettings;
    bool useSsr = resolveDeferredSsrSettings(
        ecsInstance ? ecsInstance->getComponentManager() : nullptr,
        deferredCameraEntity,
        ssrSettings
    );
    if(!useSsr && ecsInstance){
        auto* manager = ecsInstance->getComponentManager();
        auto* entityManager = ecsInstance->getEntityManager();
        if(manager && entityManager){
            const auto& entities = entityManager->getEntities();
            for(const auto& entityPtr : entities){
                auto* entity = entityPtr.get();
                if(!entity){
                    continue;
                }
                auto* cameraComponent = manager->getECSComponent<CameraComponent>(entity);
                auto* ssrComponent = manager->getECSComponent<SSRComponent>(entity);
                if(!IsComponentActive(cameraComponent) ||
                   !cameraComponent->camera ||
                   !IsComponentActive(ssrComponent)){
                    continue;
                }
                const DeferredSSRSettings candidate = ssrComponent->buildDeferredSsrSettings();
                if(!candidate.enabled){
                    continue;
                }
                ssrSettings = candidate;
                useSsr = true;
                deferredCameraEntity = entity;
                this->activeCameraEntity = entity;
                break;
            }
        }
    }
    if(!useSsr){
        static bool loggedSsrDisabledNoComponent = false;
        if(!loggedSsrDisabledNoComponent){
            LogBot.Log(LOG_WARN, "Deferred SSR disabled: no enabled SSR component resolved for the active camera.");
            loggedSsrDisabledNoComponent = true;
        }
    }
    ssrSettings.enabled = useSsr;

    DeferredSSAOSettings ssaoSettings;
    const bool useSsao = resolveDeferredSsaoSettings(
        ecsInstance ? ecsInstance->getComponentManager() : nullptr,
        deferredCameraEntity,
        ssaoSettings
    );
    std::shared_ptr<DeferredSSAO> ssaoPass = nullptr;
    if(useSsao && deferredQuad){
        if(!deferredSsaoPass){
            deferredSsaoPass = std::make_shared<DeferredSSAO>();
        }
        if(deferredSsaoPass->renderAoMap(
            gBufferWidth,
            gBufferHeight,
            deferredQuad,
            gBuffer->getGBufferTexture(1),
            gBuffer->getDepthTexture(),
            cam->getViewMatrix(),
            cam->getProjectionMatrix(),
            ssaoSettings
        )){
            ssaoPass = deferredSsaoPass;
        }
    }

    PTexture giTexture = nullptr;
    auto env = screen ? screen->getEnvironment() : nullptr;
    PCubeMap giEnvMap = (env && env->getSkyBox()) ? env->getSkyBox()->getCubeMap() : nullptr;
    static const std::vector<Light> EMPTY_LIGHTS;
    const std::vector<Light>& uploadedLights = (env && env->isLightingEnabled()) ? env->getLightsForUpload() : EMPTY_LIGHTS;
    buildDeferredLightTiles(cam, uploadedLights);
    const Math3D::Mat4 inverseViewMatrix = Math3D::Mat4(glm::inverse(glm::mat4(cam->getViewMatrix())));
    if(useSsao &&
       deferredQuad &&
       deferredDirectLightBuffer &&
       deferredDirectLightBuffer->getTexture() &&
       ssaoSettings.giBoost > 0.0f){
        drawDeferredLighting(
            deferredDirectLightBuffer,
            Color::BLACK,
            cam,
            nullptr,
            nullptr,
            nullptr,
            1
        );
        checkGlError("direct light prepass");
        if(deferredDisabled){
            drawSkybox(cam);
            drawModels3D(cam);
            return;
        }

        if(!deferredScreenGiPass){
            deferredScreenGiPass = std::make_shared<DeferredScreenGI>();
        }
        if(deferredScreenGiPass->renderGiMap(
            gBufferWidth,
            gBufferHeight,
            deferredQuad,
            gBuffer->getGBufferTexture(1),
            gBuffer->getDepthTexture(),
            deferredDirectLightBuffer->getTexture(),
            giEnvMap,
            cam->getViewMatrix(),
            inverseViewMatrix,
            cam->getProjectionMatrix(),
            ssaoSettings
        )){
            giTexture = deferredScreenGiPass->getBlurGiTexture();
        }
    }

    updateLocalReflectionProbe(screen, cam);

    auto drawBuffer = screen ? screen->getDrawBuffer() : nullptr;
    drawDeferredLighting(
        drawBuffer,
        screen ? screen->getClearColor() : Color::BLACK,
        cam,
        ssaoPass,
        useSsao ? &ssaoSettings : nullptr,
        giTexture,
        0
    );
    checkGlError("lighting pass");
    if(deferredDisabled){
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }
    if((ssaoPass || giTexture) && ssaoSettings.debugView != 0){
        return;
    }

    transparentSsrSettings = ssrSettings;
    transparentSsrEnabled = false;

    const bool ssrInputsReady =
        drawBuffer &&
        drawBuffer->getTexture() &&
        deferredQuad &&
        gBuffer &&
        gBuffer->getGBufferCount() > 3 &&
        gBuffer->getGBufferTexture(1) &&
        gBuffer->getGBufferTexture(3) &&
        gBuffer->getDepthTexture() &&
        drawBuffer->getWidth() == gBufferWidth &&
        drawBuffer->getHeight() == gBufferHeight;

    if(useSsr && ssrInputsReady){
        if(!deferredSsrPass){
            deferredSsrPass = std::make_shared<DeferredSSR>();
        }

        auto ssrEnv = screen ? screen->getEnvironment() : nullptr;
        PCubeMap ssrEnvMap = (ssrEnv && ssrEnv->getSkyBox()) ? ssrEnv->getSkyBox()->getCubeMap() : nullptr;

        static bool loggedSsrCompositeFailure = false;
        const bool usePlanarReflectionForSsr =
            activePlanarReflection.valid &&
            activePlanarReflection.buffer &&
            activePlanarReflection.buffer->getTexture();
        const bool renderedSsr = deferredSsrPass->renderComposite(
            gBufferWidth,
            gBufferHeight,
            deferredQuad,
            drawBuffer->getTexture(),
            nullptr,
            deferredLocalReflectionProbe.valid ? deferredLocalReflectionProbe.cubeMap : nullptr,
            deferredLocalReflectionProbe.center,
            deferredLocalReflectionProbe.captureBoundsMin,
            deferredLocalReflectionProbe.captureBoundsMax,
            deferredLocalReflectionProbe.influenceBoundsMin,
            deferredLocalReflectionProbe.influenceBoundsMax,
            usePlanarReflectionForSsr ? activePlanarReflection.buffer->getTexture() : nullptr,
            usePlanarReflectionForSsr ? activePlanarReflection.viewProjection : Math3D::Mat4(),
            usePlanarReflectionForSsr ? activePlanarReflection.center : Math3D::Vec3(0.0f, 0.0f, 0.0f),
            usePlanarReflectionForSsr ? activePlanarReflection.normal : Math3D::Vec3(0.0f, 1.0f, 0.0f),
            usePlanarReflectionForSsr ? activePlanarReflection.strength : 1.0f,
            usePlanarReflectionForSsr ? activePlanarReflection.receiverFadeDistance : 1.0f,
            gBuffer->getGBufferTexture(0),
            gBuffer->getGBufferTexture(1),
            gBuffer->getDepthTexture(),
            gBuffer->getGBufferTexture(3),
            cam->getViewMatrix(),
            cam->getProjectionMatrix(),
            cam->getProjectionMatrix(),
            ssrEnvMap,
            ssrSettings
        );
        if(renderedSsr){
            bool copiedSsr = false;
            PTexture ssrTexture = deferredSsrPass->getCompositeTexture();
            PFrameBuffer ssrBuffer = deferredSsrPass->getCompositeBuffer();
            if(ssrTexture && ssrTexture->getID() != 0 && drawBuffer->getTexture() && drawBuffer->getTexture()->getID() != 0){
                if(glCopyImageSubData){
                    glCopyImageSubData(
                        ssrTexture->getID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                        drawBuffer->getTexture()->getID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                        gBufferWidth, gBufferHeight, 1
                    );
                    copiedSsr = (glGetError() == GL_NO_ERROR);
                }

                if(!copiedSsr && ssrBuffer){
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, ssrBuffer->getID());
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glBindTexture(GL_TEXTURE_2D, drawBuffer->getTexture()->getID());
                    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, gBufferWidth, gBufferHeight);
                    copiedSsr = (glGetError() == GL_NO_ERROR);
                    glBindTexture(GL_TEXTURE_2D, 0);
                }

                if(!copiedSsr && ssrBuffer){
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, ssrBuffer->getID());
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawBuffer->getID());
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glDrawBuffer(GL_COLOR_ATTACHMENT0);
                    glBlitFramebuffer(
                        0, 0, gBufferWidth, gBufferHeight,
                        0, 0, drawBuffer->getWidth(), drawBuffer->getHeight(),
                        GL_COLOR_BUFFER_BIT, GL_NEAREST
                    );
                    copiedSsr = (glGetError() == GL_NO_ERROR);
                }
            }
            if(!copiedSsr){
                LogBot.Log(LOG_WARN, "Deferred SSR composite copy failed; using base deferred lighting result.");
            }
            glBindFramebuffer(GL_FRAMEBUFFER, drawBuffer->getID());
        }else if(!loggedSsrCompositeFailure){
            LogBot.Log(
                LOG_WARN,
                "Deferred SSR enabled but composite pass did not render (shader compile/target setup likely failed)."
            );
            loggedSsrCompositeFailure = true;
        }
    }else if(useSsr && !ssrInputsReady){
        static bool loggedSsrPrereqFailure = false;
        if(!loggedSsrPrereqFailure){
            LogBot.Log(
                LOG_WARN,
                "Deferred SSR enabled but required inputs are missing or size-mismatched."
            );
            loggedSsrPrereqFailure = true;
        }
    }

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
    drawModels3D(cam, RenderFilter::Opaque, true);
    checkGlError("forward fallback opaque pass");
    if(deferredDisabled){
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }

    drawSkybox(cam, true);

    if(drawBuffer && drawBuffer->getTexture()){
        const int drawWidth = drawBuffer->getWidth();
        const int drawHeight = drawBuffer->getHeight();
        const bool needsNewTransparentSsrBuffer =
            !transparentSsrSourceBuffer ||
            transparentSsrSourceBuffer->getWidth() != drawWidth ||
            transparentSsrSourceBuffer->getHeight() != drawHeight ||
            !transparentSsrSourceBuffer->getTexture();

        if(needsNewTransparentSsrBuffer){
            transparentSsrSourceBuffer = FrameBuffer::Create(drawWidth, drawHeight);
            if(transparentSsrSourceBuffer){
                transparentSsrSourceBuffer->attachTexture(
                    Texture::CreateRenderTarget(drawWidth, drawHeight, GL_RGBA16F, GL_RGBA, GL_FLOAT)
                );
                if(!transparentSsrSourceBuffer->validate()){
                    transparentSsrSourceBuffer = nullptr;
                }
            }
        }

        if(transparentSsrSourceBuffer && transparentSsrSourceBuffer->getTexture()){
            glBindTexture(GL_TEXTURE_2D, transparentSsrSourceBuffer->getTexture()->getID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
            bool copiedSceneColor = false;
            PTexture srcColor = drawBuffer->getTexture();
            PTexture dstColor = transparentSsrSourceBuffer->getTexture();
            if(srcColor && dstColor && srcColor->getID() != 0 && dstColor->getID() != 0){
                if(glCopyImageSubData){
                    glCopyImageSubData(
                        srcColor->getID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                        dstColor->getID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                        drawWidth, drawHeight, 1
                    );
                    copiedSceneColor = (glGetError() == GL_NO_ERROR);
                }

                if(!copiedSceneColor){
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, drawBuffer->getID());
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glBindTexture(GL_TEXTURE_2D, dstColor->getID());
                    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, drawWidth, drawHeight);
                    copiedSceneColor = (glGetError() == GL_NO_ERROR);
                    glBindTexture(GL_TEXTURE_2D, 0);
                }

                if(!copiedSceneColor){
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, drawBuffer->getID());
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, transparentSsrSourceBuffer->getID());
                    glBlitFramebuffer(
                        0, 0, drawWidth, drawHeight,
                        0, 0, drawWidth, drawHeight,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST
                    );
                    copiedSceneColor = (glGetError() == GL_NO_ERROR);
                }
            }

            if(!copiedSceneColor){
                static bool loggedTransparentSsrCopyFailure = false;
                if(!loggedTransparentSsrCopyFailure){
                    LogBot.Log(LOG_WARN, "Transparent scene-color copy failed; disabling transparent glass/water compositing for this frame.");
                    loggedTransparentSsrCopyFailure = true;
                }
            }
            transparentSsrEnabled = copiedSceneColor;
            glBindFramebuffer(GL_FRAMEBUFFER, drawBuffer->getID());
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    drawModels3D(cam, RenderFilter::Transparent);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    checkGlError("transparent pass");
    if(deferredDisabled){
        drawSkybox(cam);
        drawModels3D(cam);
        return;
    }
}

void Scene::render3DPass(){
    auto screen = getMainScreen();
    if(!screen) return;

    screen->bind();

    updateSceneLights();

    auto cam = screen->getCamera();
    if(cam){
        bool ssaoDebugViewActive = hasDeferredSsaoOverride && (deferredSsaoOverrideSettings.debugView != 0);
        if(!ssaoDebugViewActive){
            if(auto* manager = ecsInstance ? ecsInstance->getComponentManager() : nullptr){
                if(activeCameraEntity){
                    if(auto* ssao = manager->getECSComponent<SSAOComponent>(activeCameraEntity)){
                        ssaoDebugViewActive = IsComponentActive(ssao) && (ssao->debugView != 0);
                    }
                }
            }
        }

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

        updatePlanarReflection(screen, cam);

        bool useDeferred = (GameEngine::Engine && GameEngine::Engine->getRenderStrategy() == EngineRenderStrategy::Deferred);
        auto drawStart = std::chrono::steady_clock::now();
        if(useDeferred){
            renderDeferred(screen, cam);
        }else{
            drawSkybox(cam);
            drawModels3D(cam);
        }
        if(!ssaoDebugViewActive){
            drawOutlines(screen, cam);
        }
        auto drawEnd = std::chrono::steady_clock::now();
        std::chrono::duration<float, std::milli> drawMs = drawEnd - drawStart;
        debugStats.drawMs.store(drawMs.count(), std::memory_order_relaxed);
    }

    screen->unbind();
    debugStats.postFxMs.store(screen->getLastPostProcessMs(), std::memory_order_relaxed);
    debugStats.postFxEffectCount.store(screen->getLastPostProcessEffectCount(), std::memory_order_relaxed);
}

void Scene::drawModels3D(PCamera cam, RenderFilter filter, bool skipDeferredCompatible, const std::string* excludedEntityId){
    if(!cam) return;

    const int frontIndex = renderSnapshotIndex.load(std::memory_order_acquire);
    const auto& snapshot = renderSnapshots[frontIndex];
    const Math3D::Mat4 viewMatrix = cam->getViewMatrix();
    const Math3D::Mat4 projectionMatrix = cam->getProjectionMatrix();
    const Math3D::Mat4 inverseProjectionMatrix = Math3D::Mat4(glm::inverse(glm::mat4(projectionMatrix)));
    const Math3D::Mat4 clipMatrix = projectionMatrix * viewMatrix;
    const bool hasTransparentSceneColor =
        (filter == RenderFilter::Transparent) &&
        transparentSsrEnabled &&
        transparentSsrSourceBuffer &&
        transparentSsrSourceBuffer->getTexture();
    const bool hasTransparentSceneDepth =
        hasTransparentSceneColor &&
        gBuffer &&
        gBuffer->getDepthTexture() &&
        transparentSsrSourceBuffer &&
        transparentSsrSourceBuffer->getWidth() == gBuffer->getWidth() &&
        transparentSsrSourceBuffer->getHeight() == gBuffer->getHeight();
    const bool useTransparentSsr = hasTransparentSceneColor && transparentSsrSettings.enabled;
    PTexture transparentSsrColor = hasTransparentSceneColor ? transparentSsrSourceBuffer->getTexture() : nullptr;
    PTexture transparentSceneDepth = hasTransparentSceneDepth ? gBuffer->getDepthTexture() : nullptr;
    const float transparentSsrIntensity = useTransparentSsr ? Math3D::Clamp(transparentSsrSettings.intensity, 0.0f, 4.0f) : 0.0f;
    const float transparentSsrMaxDistance = useTransparentSsr ? Math3D::Clamp(transparentSsrSettings.maxDistance, 0.5f, 2500.0f) : 0.0f;
    const float transparentSsrThickness = useTransparentSsr ? Math3D::Clamp(transparentSsrSettings.thickness, 0.005f, 4.0f) : 0.18f;
    const float transparentSsrStride = useTransparentSsr ? Math3D::Clamp(transparentSsrSettings.stride, 0.1f, 8.0f) : 0.75f;
    const float transparentSsrJitter = useTransparentSsr ? Math3D::Clamp(transparentSsrSettings.jitter, 0.0f, 1.0f) : 0.35f;
    const int transparentSsrMaxSteps = useTransparentSsr ? Math3D::Clamp(transparentSsrSettings.maxSteps, 8, 256) : 56;
    const float transparentSsrRoughnessCutoff = useTransparentSsr ? Math3D::Clamp(transparentSsrSettings.roughnessCutoff, 0.05f, 1.0f) : 0.05f;
    const float transparentSsrEdgeFade = useTransparentSsr ? Math3D::Clamp(transparentSsrSettings.edgeFade, 0.001f, 0.5f) : 0.001f;
    const bool hasPlanarReflection =
        activePlanarReflection.valid &&
        activePlanarReflection.buffer &&
        activePlanarReflection.buffer->getTexture();
    const bool hasLocalReflectionProbe =
        !localReflectionProbeCaptureActive &&
        deferredLocalReflectionProbe.valid &&
        deferredLocalReflectionProbe.cubeMap &&
        deferredLocalReflectionProbe.cubeMap->getID() != 0;
    static const Math3D::Mat4 IDENTITY;
    std::vector<const RenderItem*> drawItems;
    drawItems.reserve(snapshot.drawItems.size());
    for(const auto& item : snapshot.drawItems){
        if(!item.mesh || !item.material) continue;
        if(excludedEntityId && !excludedEntityId->empty() && item.entityId == *excludedEntityId) continue;
        if(filter == RenderFilter::Opaque && item.isTransparent) continue;
        if(filter == RenderFilter::Transparent && !item.isTransparent) continue;
        const bool isPlanarReflectorItem = hasPlanarReflection && item.entityId == activePlanarReflection.entityId;
        if(skipDeferredCompatible && item.isDeferredCompatible && !isPlanarReflectorItem) continue;
        if(item.hasBounds && !aabbIntersectsClipFrustum(item.boundsMin, item.boundsMax, clipMatrix)) continue;
        drawItems.push_back(&item);
    }
    if(filter == RenderFilter::Transparent){
        std::sort(drawItems.begin(), drawItems.end(), [&](const RenderItem* a, const RenderItem* b){
            Math3D::Vec3 centerA = a->model.getPosition();
            Math3D::Vec3 centerB = b->model.getPosition();
            if(a->hasBounds){
                centerA = (a->boundsMin + a->boundsMax) * 0.5f;
            }
            if(b->hasBounds){
                centerB = (b->boundsMin + b->boundsMax) * 0.5f;
            }

            const glm::vec4 viewCenterA = static_cast<glm::mat4>(viewMatrix) * static_cast<glm::vec4>(Math3D::Vec4(centerA, 1.0f));
            const glm::vec4 viewCenterB = static_cast<glm::mat4>(viewMatrix) * static_cast<glm::vec4>(Math3D::Vec4(centerB, 1.0f));
            const float depthA = -viewCenterA.z;
            const float depthB = -viewCenterB.z;
            if(std::abs(depthA - depthB) > 1e-4f){
                return depthA > depthB;
            }
            if(a->enableBackfaceCulling != b->enableBackfaceCulling){
                return a->enableBackfaceCulling > b->enableBackfaceCulling;
            }
            if(a->material.get() != b->material.get()){
                return a->material.get() < b->material.get();
            }
            return a->mesh.get() < b->mesh.get();
        });
    }
    bool cullStateKnown = false;
    bool cullEnabled = true;
    std::shared_ptr<Material> lastBoundMaterial = nullptr;
    for(const RenderItem* itemPtr : drawItems){
        if(!itemPtr){
            continue;
        }
        const RenderItem& item = *itemPtr;

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

        auto shader = item.material ? item.material->getShader() : nullptr;
        if(item.material != lastBoundMaterial){
            item.material->bind();
            lastBoundMaterial = item.material;
            if(shader && shader->getID() != 0){
                shader->setUniformFast("u_view", Uniform<Math3D::Mat4>(viewMatrix));
                shader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(projectionMatrix));
                shader->setUniformFast("u_useSceneColor", Uniform<int>(hasTransparentSceneColor ? 1 : 0));
                shader->setUniformFast("u_useSceneDepth", Uniform<int>(hasTransparentSceneDepth ? 1 : 0));
                shader->setUniformFast("u_useSsr", Uniform<int>(useTransparentSsr ? 1 : 0));
                shader->setUniformFast(
                    "u_ssrColor",
                    Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(transparentSsrColor, 8))
                );
                shader->setUniformFast(
                    "u_sceneDepth",
                    Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(transparentSceneDepth, 9))
                );
                shader->setUniformFast("u_ssrIntensity", Uniform<float>(transparentSsrIntensity));
                shader->setUniformFast("u_ssrMaxDistance", Uniform<float>(transparentSsrMaxDistance));
                shader->setUniformFast("u_ssrThickness", Uniform<float>(transparentSsrThickness));
                shader->setUniformFast("u_ssrStride", Uniform<float>(transparentSsrStride));
                shader->setUniformFast("u_ssrJitter", Uniform<float>(transparentSsrJitter));
                shader->setUniformFast("u_ssrMaxSteps", Uniform<int>(transparentSsrMaxSteps));
                shader->setUniformFast("u_ssrRoughnessCutoff", Uniform<float>(transparentSsrRoughnessCutoff));
                shader->setUniformFast("u_ssrEdgeFade", Uniform<float>(transparentSsrEdgeFade));
                shader->setUniformFast("u_invProjection", Uniform<Math3D::Mat4>(inverseProjectionMatrix));
                shader->setUniformFast("u_useUserClipPlane", Uniform<int>(userClipPlaneActive ? 1 : 0));
                shader->setUniformFast("u_userClipPlane", Uniform<Math3D::Vec4>(userClipPlane));
            }
        }
        if(shader && shader->getID() != 0){
            shader->setUniformFast("u_model", Uniform<Math3D::Mat4>(item.model));
            shader->setUniformFast("u_useLocalProbe", Uniform<int>(hasLocalReflectionProbe ? 1 : 0));
            shader->setUniformFast(
                "u_localProbe",
                Uniform<GLUniformUpload::CubeMapSlot>(GLUniformUpload::CubeMapSlot(
                    hasLocalReflectionProbe ? deferredLocalReflectionProbe.cubeMap : nullptr,
                    11
                ))
            );
            shader->setUniformFast(
                "u_localProbeCenter",
                Uniform<Math3D::Vec3>(hasLocalReflectionProbe ? deferredLocalReflectionProbe.center : Math3D::Vec3(0.0f, 0.0f, 0.0f))
            );
            shader->setUniformFast(
                "u_localProbeCaptureMin",
                Uniform<Math3D::Vec3>(hasLocalReflectionProbe ? deferredLocalReflectionProbe.captureBoundsMin : Math3D::Vec3(0.0f, 0.0f, 0.0f))
            );
            shader->setUniformFast(
                "u_localProbeCaptureMax",
                Uniform<Math3D::Vec3>(hasLocalReflectionProbe ? deferredLocalReflectionProbe.captureBoundsMax : Math3D::Vec3(0.0f, 0.0f, 0.0f))
            );
            shader->setUniformFast(
                "u_localProbeInfluenceMin",
                Uniform<Math3D::Vec3>(hasLocalReflectionProbe ? deferredLocalReflectionProbe.influenceBoundsMin : Math3D::Vec3(0.0f, 0.0f, 0.0f))
            );
            shader->setUniformFast(
                "u_localProbeInfluenceMax",
                Uniform<Math3D::Vec3>(hasLocalReflectionProbe ? deferredLocalReflectionProbe.influenceBoundsMax : Math3D::Vec3(0.0f, 0.0f, 0.0f))
            );
            shader->setUniformFast("u_usePlanarReflection", Uniform<int>(hasPlanarReflection ? 1 : 0));
            shader->setUniformFast(
                "u_planarReflectionTex",
                Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(
                    hasPlanarReflection ? activePlanarReflection.buffer->getTexture() : nullptr,
                    10
                ))
            );
            shader->setUniformFast(
                "u_planarReflectionMatrix",
                Uniform<Math3D::Mat4>(hasPlanarReflection ? activePlanarReflection.viewProjection : IDENTITY)
            );
            shader->setUniformFast(
                "u_planarReflectionStrength",
                Uniform<float>(hasPlanarReflection ? activePlanarReflection.strength : 1.0f)
            );
            shader->setUniformFast(
                "u_planarReflectionCenter",
                Uniform<Math3D::Vec3>(hasPlanarReflection ? activePlanarReflection.center : Math3D::Vec3(0.0f, 0.0f, 0.0f))
            );
            shader->setUniformFast(
                "u_planarReflectionNormal",
                Uniform<Math3D::Vec3>(hasPlanarReflection ? activePlanarReflection.normal : Math3D::Vec3(0.0f, 1.0f, 0.0f))
            );
            shader->setUniformFast(
                "u_planarReflectionReceiverFadeDistance",
                Uniform<float>(hasPlanarReflection ? activePlanarReflection.receiverFadeDistance : 1.0f)
            );
        }

        // The current transmissive shader solves a single screen-space composite.
        // Drawing both front and back faces double-applies that composite and causes
        // view-dependent opacity swings on closed glass meshes, so keep one stable pass.
        item.mesh->draw();
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
    auto env = Screen::GetCurrentEnvironment();
    if(!env){
        auto screen = getMainScreen();
        if(!screen) return;
        env = screen->getEnvironment();
    }
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
