#include "Serialization/IO/ComponentDependencyCollector.h"

#include "ECS/Core/ECSComponents.h"
#include "Assets/Core/Asset.h"

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
    }

    outDependencies.assign(deps.begin(), deps.end());
}

} // namespace Serialization
