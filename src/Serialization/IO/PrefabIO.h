/**
 * @file src/Serialization/IO/PrefabIO.h
 * @brief Declarations for PrefabIO.
 */

#ifndef SERIALIZATION_IO_PREFAB_IO_H
#define SERIALIZATION_IO_PREFAB_IO_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Scene/Scene.h"
#include "Serialization/Schema/ComponentSerializationRegistry.h"
#include "Serialization/Schema/PrefabSceneSchemas.h"

namespace PrefabIO {

/// @brief Holds data for PrefabVariantOptions.
struct PrefabVariantOptions {
    std::string basePrefabRef;
    bool inheritBaseChildren = true;
    bool inheritBaseComponents = true;
    // Optional JSON object payload.
    // At instantiate-time this is applied as an RFC 7386 JSON merge-patch
    // against the resolved prefab payload, enabling forward-compatible
    // variant override behavior without hardcoding every override type here.
    JsonSchema::RawJsonValue overrides;
};

/// @brief Holds data for PrefabSettingsOptions.
struct PrefabSettingsOptions {
    bool runtimeInstantiable = true;
    bool allowChildAdditions = true;
    bool allowChildRemovals = true;
    bool allowComponentOverrides = true;
    std::vector<std::string> exposedProperties;
};

/// @brief Holds data for PrefabSaveOptions.
struct PrefabSaveOptions {
    JsonSchema::DocumentMetadata metadata;
    bool autoCollectDependencies = true;
    std::vector<std::string> dependencies;
    PrefabSettingsOptions settings;
    PrefabVariantOptions variant;
    const Serialization::ComponentSerializationRegistry* registry = nullptr;
};

/// @brief Holds data for PrefabInstantiateOptions.
struct PrefabInstantiateOptions {
    NeoECS::GameObject* parent = nullptr;
    bool resolveVariants = true;
    std::size_t maxVariantDepth = 16;
    const Serialization::ComponentSerializationRegistry* registry = nullptr;
};

/// @brief Holds data for PrefabInstantiateResult.
struct PrefabInstantiateResult {
    std::vector<NeoECS::GameObject*> rootObjects;
    std::unordered_map<std::uint64_t, NeoECS::ECSEntity*> snapshotIdToEntity;
    std::string variantBasePrefabRef;
    std::vector<std::string> resolvedVariantBaseRefs;
};

/**
 * @brief Builds schema from entity subtree.
 * @param scene Value for scene.
 * @param rootEntity Value for root entity.
 * @param outSchema Output value for schema.
 * @param options Configuration options.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool BuildSchemaFromEntitySubtree(
    PScene scene,
    NeoECS::ECSEntity* rootEntity,
    JsonSchema::PrefabSchema& outSchema,
    const PrefabSaveOptions& options = PrefabSaveOptions{},
    std::string* outError = nullptr
);

/**
 * @brief Saves entity subtree to absolute path.
 * @param scene Value for scene.
 * @param rootEntity Value for root entity.
 * @param path Filesystem path for path.
 * @param options Configuration options.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool SaveEntitySubtreeToAbsolutePath(
    PScene scene,
    NeoECS::ECSEntity* rootEntity,
    const std::filesystem::path& path,
    const PrefabSaveOptions& options = PrefabSaveOptions{},
    std::string* outError = nullptr
);

/**
 * @brief Saves entity subtree to asset ref.
 * @param scene Value for scene.
 * @param rootEntity Value for root entity.
 * @param assetRef Reference to asset.
 * @param options Configuration options.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool SaveEntitySubtreeToAssetRef(
    PScene scene,
    NeoECS::ECSEntity* rootEntity,
    const std::string& assetRef,
    const PrefabSaveOptions& options = PrefabSaveOptions{},
    std::string* outError = nullptr
);

/**
 * @brief Loads schema from absolute path.
 * @param path Filesystem path for path.
 * @param outSchema Output value for schema.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool LoadSchemaFromAbsolutePath(
    const std::filesystem::path& path,
    JsonSchema::PrefabSchema& outSchema,
    std::string* outError = nullptr
);

/**
 * @brief Loads schema from asset ref.
 * @param assetRef Reference to asset.
 * @param outSchema Output value for schema.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool LoadSchemaFromAssetRef(
    const std::string& assetRef,
    JsonSchema::PrefabSchema& outSchema,
    std::string* outError = nullptr
);

/**
 * @brief Checks whether instantiate schema into scene.
 * @param scene Value for scene.
 * @param schema Value for schema.
 * @param options Configuration options.
 * @param outResult Output value for result.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool InstantiateSchemaIntoScene(
    PScene scene,
    const JsonSchema::PrefabSchema& schema,
    const PrefabInstantiateOptions& options = PrefabInstantiateOptions{},
    PrefabInstantiateResult* outResult = nullptr,
    std::string* outError = nullptr
);

/**
 * @brief Checks whether instantiate from absolute path.
 * @param scene Value for scene.
 * @param path Filesystem path for path.
 * @param options Configuration options.
 * @param outResult Output value for result.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool InstantiateFromAbsolutePath(
    PScene scene,
    const std::filesystem::path& path,
    const PrefabInstantiateOptions& options = PrefabInstantiateOptions{},
    PrefabInstantiateResult* outResult = nullptr,
    std::string* outError = nullptr
);

/**
 * @brief Checks whether instantiate from asset ref.
 * @param scene Value for scene.
 * @param assetRef Reference to asset.
 * @param options Configuration options.
 * @param outResult Output value for result.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool InstantiateFromAssetRef(
    PScene scene,
    const std::string& assetRef,
    const PrefabInstantiateOptions& options = PrefabInstantiateOptions{},
    PrefabInstantiateResult* outResult = nullptr,
    std::string* outError = nullptr
);

} // namespace PrefabIO

#endif // SERIALIZATION_IO_PREFAB_IO_H
