/**
 * @file src/Serialization/IO/ComponentDependencyCollector.cpp
 * @brief Implementation for ComponentDependencyCollector.
 */

#include "Serialization/IO/ComponentDependencyCollector.h"

#include "ECS/Core/ECSComponents.h"
#include "Assets/Core/Asset.h"
#include "Assets/Descriptors/ImageAsset.h"
#include "Assets/Descriptors/LensFlareAsset.h"
#include "Assets/Descriptors/SkyboxAsset.h"

#include <algorithm>
#include <set>

namespace {

bool isAssetLikeRef(const std::string& value){
    if(value.empty()){
        return false;
    }
    return value.find(ASSET_DELIMITER) != std::string::npos;
}

void addDependencyIfValid(const std::string& candidate, std::set<std::string>& deps){
    if(!isAssetLikeRef(candidate)){
        return;
    }
    deps.insert(candidate);
}

void addTextureDependencyIfValid(const std::string& candidate, std::set<std::string>& deps){
    if(candidate.empty()){
        return;
    }

    addDependencyIfValid(candidate, deps);

    std::string sourceAssetRef;
    std::string imageAssetRef;
    if(!ImageAssetIO::ResolveTextureSourceAssetRef(candidate, sourceAssetRef, &imageAssetRef, nullptr)){
        return;
    }

    if(!imageAssetRef.empty()){
        addDependencyIfValid(imageAssetRef, deps);
    }
    if(!sourceAssetRef.empty()){
        addDependencyIfValid(sourceAssetRef, deps);
    }
}

} // namespace

namespace Serialization {

void CollectAssetDependenciesFromEntities(
    NeoECS::ECSComponentManager* manager,
    const std::vector<NeoECS::ECSEntity*>& entities,
    std::vector<std::string>& outDependencies
){
    outDependencies.clear();
    if(!manager || entities.empty()){
        return;
    }

    std::set<std::string> deps;
    for(NeoECS::ECSEntity* entity : entities){
        if(!entity){
            continue;
        }

        if(auto* renderer = manager->getECSComponent<MeshRendererComponent>(entity)){
            addDependencyIfValid(renderer->modelAssetRef, deps);
            addDependencyIfValid(renderer->modelSourceRef, deps);
            addDependencyIfValid(renderer->materialAssetRef, deps);
            for(const std::string& partMatRef : renderer->modelPartMaterialAssetRefs){
                addDependencyIfValid(partMatRef, deps);
            }
        }

        if(auto* scripts = manager->getECSComponent<ScriptComponent>(entity)){
            for(const std::string& scriptRef : scripts->scriptAssetRefs){
                addDependencyIfValid(scriptRef, deps);
            }
        }

        if(auto* skybox = manager->getECSComponent<SkyboxComponent>(entity)){
            addDependencyIfValid(skybox->skyboxAssetRef, deps);

            if(!skybox->skyboxAssetRef.empty()){
                SkyboxAssetData skyboxData;
                if(SkyboxAssetIO::LoadFromAssetRef(skybox->skyboxAssetRef, skyboxData, nullptr)){
                    addTextureDependencyIfValid(skyboxData.rightFaceRef, deps);
                    addTextureDependencyIfValid(skyboxData.leftFaceRef, deps);
                    addTextureDependencyIfValid(skyboxData.topFaceRef, deps);
                    addTextureDependencyIfValid(skyboxData.bottomFaceRef, deps);
                    addTextureDependencyIfValid(skyboxData.frontFaceRef, deps);
                    addTextureDependencyIfValid(skyboxData.backFaceRef, deps);
                }
            }
        }

        if(auto* light = manager->getECSComponent<LightComponent>(entity)){
            addDependencyIfValid(light->flareAssetRef, deps);

            if(!light->flareAssetRef.empty()){
                LensFlareAssetData flareData;
                if(LensFlareAssetIO::LoadFromAssetRef(light->flareAssetRef, flareData, nullptr)){
                    for(const LensFlareElementData& element : flareData.elements){
                        if(element.type != LensFlareElementType::Image){
                            continue;
                        }

                        addTextureDependencyIfValid(element.textureRef, deps);
                    }
                }
            }
        }
    }

    outDependencies.assign(deps.begin(), deps.end());
}

} // namespace Serialization
