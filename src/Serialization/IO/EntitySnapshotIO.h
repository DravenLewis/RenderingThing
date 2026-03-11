/**
 * @file src/Serialization/IO/EntitySnapshotIO.h
 * @brief Declarations for EntitySnapshotIO.
 */

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

/// @brief Holds data for SnapshotBuildOptions.
struct SnapshotBuildOptions {
    const ComponentSerializationRegistry* registry = nullptr;
};

/// @brief Holds data for SnapshotBuildResult.
struct SnapshotBuildResult {
    std::vector<std::uint64_t> rootEntityIds;
    std::vector<EntityRecord> entities;
    std::unordered_map<NeoECS::ECSEntity*, std::uint64_t> entityToSnapshotId;
};

/// @brief Holds data for SnapshotInstantiateOptions.
struct SnapshotInstantiateOptions {
    NeoECS::GameObject* destinationParent = nullptr;
    const ComponentSerializationRegistry* registry = nullptr;
};

/// @brief Holds data for SnapshotInstantiateResult.
struct SnapshotInstantiateResult {
    std::vector<NeoECS::GameObject*> rootObjects;
    std::unordered_map<std::uint64_t, NeoECS::ECSEntity*> snapshotIdToEntity;
};

/**
 * @brief Builds snapshot from entity roots.
 * @param scene Value for scene.
 * @param roots Value for roots.
 * @param outResult Output value for result.
 * @param options Configuration options.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool BuildSnapshotFromEntityRoots(
    PScene scene,
    const std::vector<NeoECS::ECSEntity*>& roots,
    SnapshotBuildResult& outResult,
    const SnapshotBuildOptions& options = SnapshotBuildOptions{},
    std::string* outError = nullptr
);

/**
 * @brief Checks whether instantiate snapshot into scene.
 * @param scene Value for scene.
 * @param entities Value for entities.
 * @param rootEntityIds Value for root entity ids.
 * @param outResult Output value for result.
 * @param options Configuration options.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool InstantiateSnapshotIntoScene(
    PScene scene,
    const std::vector<EntityRecord>& entities,
    const std::vector<std::uint64_t>& rootEntityIds,
    SnapshotInstantiateResult& outResult,
    const SnapshotInstantiateOptions& options = SnapshotInstantiateOptions{},
    std::string* outError = nullptr
);

/**
 * @brief Checks whether destroy scene root children.
 * @param scene Value for scene.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool DestroySceneRootChildren(PScene scene, std::string* outError = nullptr);

} // namespace Serialization::SnapshotIO

#endif // SERIALIZATION_IO_ENTITY_SNAPSHOT_IO_H
