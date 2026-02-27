#ifndef SERIALIZATION_IO_ENTITY_SNAPSHOT_IO_H
#define SERIALIZATION_IO_ENTITY_SNAPSHOT_IO_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Scene/Scene.h"
#include "Serialization/Schema/ComponentSerializationRegistry.h"
#include "Serialization/Schema/PrefabSceneSchemas.h"

namespace Serialization::SnapshotIO {

using EntityRecord = JsonSchema::EntitySnapshotSchemaBase::EntityRecord;

struct SnapshotBuildOptions {
    const ComponentSerializationRegistry* registry = nullptr;
};

struct SnapshotBuildResult {
    std::vector<std::uint64_t> rootEntityIds;
    std::vector<EntityRecord> entities;
    std::unordered_map<NeoECS::ECSEntity*, std::uint64_t> entityToSnapshotId;
};

struct SnapshotInstantiateOptions {
    NeoECS::GameObject* destinationParent = nullptr;
    const ComponentSerializationRegistry* registry = nullptr;
};

struct SnapshotInstantiateResult {
    std::vector<NeoECS::GameObject*> rootObjects;
    std::unordered_map<std::uint64_t, NeoECS::ECSEntity*> snapshotIdToEntity;
};

bool BuildSnapshotFromEntityRoots(
    PScene scene,
    const std::vector<NeoECS::ECSEntity*>& roots,
    SnapshotBuildResult& outResult,
    const SnapshotBuildOptions& options = SnapshotBuildOptions{},
    std::string* outError = nullptr
);

bool InstantiateSnapshotIntoScene(
    PScene scene,
    const std::vector<EntityRecord>& entities,
    const std::vector<std::uint64_t>& rootEntityIds,
    SnapshotInstantiateResult& outResult,
    const SnapshotInstantiateOptions& options = SnapshotInstantiateOptions{},
    std::string* outError = nullptr
);

bool DestroySceneRootChildren(PScene scene, std::string* outError = nullptr);

} // namespace Serialization::SnapshotIO

#endif // SERIALIZATION_IO_ENTITY_SNAPSHOT_IO_H
