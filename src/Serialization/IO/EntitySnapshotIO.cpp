#include "Serialization/IO/EntitySnapshotIO.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_set>

namespace {

void setSnapshotError(std::string* outError, const std::string& message){
    if(outError){
        *outError = message;
    }
}

bool tryParseUInt64String(const std::string& value, std::uint64_t& outId){
    if(value.empty()){
        return false;
    }
    for(char c : value){
        if(!std::isdigit(static_cast<unsigned char>(c))){
            return false;
        }
    }
    try{
        outId = static_cast<std::uint64_t>(std::stoull(value));
        return outId != 0;
    }catch(...){
        return false;
    }
}

std::uint64_t hashEntityId(const std::string& value){
    constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;
    std::uint64_t hash = kFnvOffset;
    for(unsigned char c : value){
        hash ^= static_cast<std::uint64_t>(c);
        hash *= kFnvPrime;
    }
    if(hash == 0){
        hash = 1;
    }
    return hash;
}

const Serialization::ComponentSerializationRegistry& resolveRegistry(const Serialization::ComponentSerializationRegistry* registry){
    if(registry){
        return *registry;
    }
    return Serialization::DefaultComponentSerializationRegistry();
}

std::uint64_t assignSnapshotId(
    NeoECS::ECSEntity* entity,
    std::unordered_map<NeoECS::ECSEntity*, std::uint64_t>& entityToId,
    std::unordered_set<std::uint64_t>& usedIds
){
    auto existing = entityToId.find(entity);
    if(existing != entityToId.end()){
        return existing->second;
    }

    std::uint64_t candidate = 0;
    const std::string uniqueId = entity ? entity->getNodeUniqueID() : std::string();
    if(!tryParseUInt64String(uniqueId, candidate)){
        candidate = hashEntityId(uniqueId);
    }
    if(candidate == 0){
        candidate = 1;
    }
    while(usedIds.find(candidate) != usedIds.end()){
        ++candidate;
        if(candidate == 0){
            candidate = 1;
        }
    }
    usedIds.insert(candidate);
    entityToId[entity] = candidate;
    return candidate;
}

bool compareEntityUniqueIdAsc(const NeoECS::ECSEntity* a, const NeoECS::ECSEntity* b){
    if(a == b){
        return false;
    }
    if(!a){
        return false;
    }
    if(!b){
        return true;
    }

    std::uint64_t aNumeric = 0;
    std::uint64_t bNumeric = 0;
    const bool aParsed = tryParseUInt64String(a->getNodeUniqueID(), aNumeric);
    const bool bParsed = tryParseUInt64String(b->getNodeUniqueID(), bNumeric);
    if(aParsed && bParsed){
        if(aNumeric != bNumeric){
            return aNumeric < bNumeric;
        }
    }else if(aParsed != bParsed){
        return aParsed;
    }

    if(a->getNodeUniqueID() != b->getNodeUniqueID()){
        return a->getNodeUniqueID() < b->getNodeUniqueID();
    }
    return a->getName() < b->getName();
}

void collectSortedChildren(NeoECS::ECSEntity* entity, std::vector<NeoECS::ECSEntity*>& outChildren){
    outChildren.clear();
    if(!entity){
        return;
    }
    outChildren.reserve(entity->children().size());
    for(const auto& childKv : entity->children()){
        if(childKv.second){
            outChildren.push_back(childKv.second);
        }
    }
    std::sort(outChildren.begin(), outChildren.end(), compareEntityUniqueIdAsc);
}

} // namespace

namespace Serialization::SnapshotIO {

bool BuildSnapshotFromEntityRoots(
    PScene scene,
    const std::vector<NeoECS::ECSEntity*>& roots,
    SnapshotBuildResult& outResult,
    const SnapshotBuildOptions& options,
    std::string* outError
){
    outResult = SnapshotBuildResult{};
    if(!scene || !scene->getECS()){
        setSnapshotError(outError, "Cannot build snapshot: scene/ECS is null.");
        return false;
    }
    auto* manager = scene->getECS()->getComponentManager();
    if(!manager){
        setSnapshotError(outError, "Cannot build snapshot: missing ECS component manager.");
        return false;
    }

    std::vector<NeoECS::ECSEntity*> filteredRoots;
    filteredRoots.reserve(roots.size());
    for(NeoECS::ECSEntity* root : roots){
        if(root){
            filteredRoots.push_back(root);
        }
    }
    if(filteredRoots.empty()){
        setSnapshotError(outError, "Cannot build snapshot from an empty root set.");
        return false;
    }

    std::sort(filteredRoots.begin(), filteredRoots.end(), compareEntityUniqueIdAsc);
    filteredRoots.erase(std::unique(filteredRoots.begin(), filteredRoots.end()), filteredRoots.end());

    const ComponentSerializationRegistry& registry = resolveRegistry(options.registry);

    std::unordered_set<std::uint64_t> usedSnapshotIds;
    std::unordered_map<NeoECS::ECSEntity*, std::uint64_t> entityToId;
    std::unordered_set<NeoECS::ECSEntity*> visited;
    std::vector<NeoECS::ECSEntity*> orderedEntities;
    orderedEntities.reserve(filteredRoots.size());

    std::function<void(NeoECS::ECSEntity*)> visitEntity = [&](NeoECS::ECSEntity* entity){
        if(!entity){
            return;
        }
        if(visited.find(entity) != visited.end()){
            return;
        }
        visited.insert(entity);
        assignSnapshotId(entity, entityToId, usedSnapshotIds);
        orderedEntities.push_back(entity);

        std::vector<NeoECS::ECSEntity*> children;
        collectSortedChildren(entity, children);
        for(NeoECS::ECSEntity* child : children){
            visitEntity(child);
        }
    };

    outResult.rootEntityIds.reserve(filteredRoots.size());
    for(NeoECS::ECSEntity* root : filteredRoots){
        visitEntity(root);
        outResult.rootEntityIds.push_back(assignSnapshotId(root, entityToId, usedSnapshotIds));
    }

    outResult.entities.reserve(orderedEntities.size());
    for(NeoECS::ECSEntity* entity : orderedEntities){
        EntityRecord record;
        record.id = assignSnapshotId(entity, entityToId, usedSnapshotIds);
        record.name = entity->getName();
        record.enabled = true;

        NeoECS::ECSEntity* parent = entity->getParent();
        auto parentIt = entityToId.find(parent);
        if(parent && parentIt != entityToId.end()){
            record.hasParentId = true;
            record.parentId = parentIt->second;
        }else{
            record.hasParentId = false;
            record.parentId = 0;
        }

        if(!registry.serializeEntityComponents(manager, entity, record.components, outError)){
            if(outError && !outError->empty()){
                *outError = "Entity '" + entity->getName() + "' component serialization failed: " + *outError;
            }
            return false;
        }
        outResult.entities.push_back(std::move(record));
    }

    outResult.entityToSnapshotId = std::move(entityToId);
    return true;
}

bool InstantiateSnapshotIntoScene(
    PScene scene,
    const std::vector<EntityRecord>& entities,
    const std::vector<std::uint64_t>& rootEntityIds,
    SnapshotInstantiateResult& outResult,
    const SnapshotInstantiateOptions& options,
    std::string* outError
){
    outResult = SnapshotInstantiateResult{};
    if(!scene || !scene->getECS()){
        setSnapshotError(outError, "Cannot instantiate snapshot: scene/ECS is null.");
        return false;
    }
    auto* manager = scene->getECS()->getComponentManager();
    if(!manager){
        setSnapshotError(outError, "Cannot instantiate snapshot: missing ECS component manager.");
        return false;
    }

    const ComponentSerializationRegistry& registry = resolveRegistry(options.registry);

    std::unordered_map<std::uint64_t, const EntityRecord*> recordsById;
    recordsById.reserve(entities.size());
    for(const EntityRecord& entityRecord : entities){
        if(entityRecord.id == 0){
            setSnapshotError(outError, "Snapshot entity has invalid id (0).");
            return false;
        }
        if(recordsById.find(entityRecord.id) != recordsById.end()){
            setSnapshotError(outError, "Snapshot contains duplicate entity id: " + std::to_string(entityRecord.id));
            return false;
        }
        recordsById[entityRecord.id] = &entityRecord;
    }

    for(std::uint64_t rootId : rootEntityIds){
        if(recordsById.find(rootId) == recordsById.end()){
            setSnapshotError(outError, "Snapshot root id not found in entity records: " + std::to_string(rootId));
            return false;
        }
    }

    std::vector<const EntityRecord*> pending;
    pending.reserve(entities.size());
    for(const EntityRecord& record : entities){
        pending.push_back(&record);
    }

    std::unordered_map<std::uint64_t, NeoECS::GameObject*> gameObjectBySnapshotId;
    gameObjectBySnapshotId.reserve(entities.size());

    while(!pending.empty()){
        size_t createdThisPass = 0;
        for(size_t i = 0; i < pending.size();){
            const EntityRecord* record = pending[i];
            if(!record){
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }

            NeoECS::GameObject* parentObject = options.destinationParent;
            if(record->hasParentId){
                auto parentCreated = gameObjectBySnapshotId.find(record->parentId);
                if(parentCreated == gameObjectBySnapshotId.end()){
                    auto parentExists = recordsById.find(record->parentId);
                    if(parentExists == recordsById.end()){
                        setSnapshotError(outError, "Snapshot entity references missing parent id: " + std::to_string(record->parentId));
                        return false;
                    }
                    ++i;
                    continue;
                }
                parentObject = parentCreated->second;
            }

            const std::string entityName = record->name.empty() ? std::string("GameObject") : record->name;
            NeoECS::GameObject* createdObject = scene->createECSGameObject(entityName, parentObject);
            if(!createdObject || !createdObject->gameobject()){
                setSnapshotError(outError, "Failed to instantiate snapshot entity: " + entityName);
                return false;
            }

            gameObjectBySnapshotId[record->id] = createdObject;
            outResult.snapshotIdToEntity[record->id] = createdObject->gameobject();
            if(!record->hasParentId){
                outResult.rootObjects.push_back(createdObject);
            }

            pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
            ++createdThisPass;
        }

        if(createdThisPass == 0){
            setSnapshotError(outError, "Failed to resolve parent dependencies while instantiating snapshot (possible parent cycle).");
            return false;
        }
    }

    for(const EntityRecord& record : entities){
        auto entityIt = outResult.snapshotIdToEntity.find(record.id);
        if(entityIt == outResult.snapshotIdToEntity.end() || !entityIt->second){
            setSnapshotError(outError, "Internal snapshot instantiate error: missing runtime entity for id " + std::to_string(record.id));
            return false;
        }

        if(!registry.deserializeEntityComponents(
                scene->getECS()->getContext(),
                manager,
                entityIt->second,
                record.components,
                outError)){
            if(outError && !outError->empty()){
                *outError = "Entity '" + record.name + "' component restore failed: " + *outError;
            }
            return false;
        }
    }

    return true;
}

bool DestroySceneRootChildren(PScene scene, std::string* outError){
    if(!scene || !scene->getECS()){
        setSnapshotError(outError, "Cannot clear scene root children: scene/ECS is null.");
        return false;
    }
    NeoECS::ECSEntity* sceneRoot = scene->getSceneRootEntity();
    if(!sceneRoot){
        return true;
    }

    std::vector<NeoECS::ECSEntity*> children;
    children.reserve(sceneRoot->children().size());
    for(const auto& kv : sceneRoot->children()){
        if(kv.second){
            children.push_back(kv.second);
        }
    }
    std::sort(children.begin(), children.end(), compareEntityUniqueIdAsc);

    for(NeoECS::ECSEntity* child : children){
        std::unique_ptr<NeoECS::GameObject> wrapper(
            NeoECS::GameObject::CreateFromECSEntity(scene->getECS()->getContext(), child)
        );
        if(!wrapper){
            setSnapshotError(outError, "Failed to create wrapper while removing scene-root child.");
            return false;
        }
        if(!scene->destroyECSGameObject(wrapper.get())){
            setSnapshotError(outError, "Failed to destroy scene-root child entity '" + child->getName() + "'.");
            return false;
        }
    }
    return true;
}

} // namespace Serialization::SnapshotIO
