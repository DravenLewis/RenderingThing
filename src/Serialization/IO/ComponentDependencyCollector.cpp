/**
 * @file src/Serialization/IO/ComponentDependencyCollector.cpp
 * @brief Implementation for ComponentDependencyCollector.
 */

#include "Serialization/IO/ComponentDependencyCollector.h"

#include "ECS/Core/ECSComponents.h"
#include "Assets/Core/Asset.h"
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
                    addDependencyIfValid(skyboxData.rightFaceRef, deps);
                    addDependencyIfValid(skyboxData.leftFaceRef, deps);
                    addDependencyIfValid(skyboxData.topFaceRef, deps);
                    addDependencyIfValid(skyboxData.bottomFaceRef, deps);
                    addDependencyIfValid(skyboxData.frontFaceRef, deps);
                    addDependencyIfValid(skyboxData.backFaceRef, deps);
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

                        addDependencyIfValid(element.textureRef, deps);
                    }
                }
            }
        }
    }

    outDependencies.assign(deps.begin(), deps.end());
}

} // namespace Serialization
