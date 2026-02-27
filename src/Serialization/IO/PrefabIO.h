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

struct PrefabSettingsOptions {
    bool runtimeInstantiable = true;
    bool allowChildAdditions = true;
    bool allowChildRemovals = true;
    bool allowComponentOverrides = true;
    std::vector<std::string> exposedProperties;
};

struct PrefabSaveOptions {
    JsonSchema::DocumentMetadata metadata;
    bool autoCollectDependencies = true;
    std::vector<std::string> dependencies;
    PrefabSettingsOptions settings;
    PrefabVariantOptions variant;
    const Serialization::ComponentSerializationRegistry* registry = nullptr;
};

struct PrefabInstantiateOptions {
    NeoECS::GameObject* parent = nullptr;
    bool resolveVariants = true;
    std::size_t maxVariantDepth = 16;
    const Serialization::ComponentSerializationRegistry* registry = nullptr;
};

struct PrefabInstantiateResult {
    std::vector<NeoECS::GameObject*> rootObjects;
    std::unordered_map<std::uint64_t, NeoECS::ECSEntity*> snapshotIdToEntity;
    std::string variantBasePrefabRef;
    std::vector<std::string> resolvedVariantBaseRefs;
};

bool BuildSchemaFromEntitySubtree(
    PScene scene,
    NeoECS::ECSEntity* rootEntity,
    JsonSchema::PrefabSchema& outSchema,
    const PrefabSaveOptions& options = PrefabSaveOptions{},
    std::string* outError = nullptr
);

bool SaveEntitySubtreeToAbsolutePath(
    PScene scene,
    NeoECS::ECSEntity* rootEntity,
    const std::filesystem::path& path,
    const PrefabSaveOptions& options = PrefabSaveOptions{},
    std::string* outError = nullptr
);

bool SaveEntitySubtreeToAssetRef(
    PScene scene,
    NeoECS::ECSEntity* rootEntity,
    const std::string& assetRef,
    const PrefabSaveOptions& options = PrefabSaveOptions{},
    std::string* outError = nullptr
);

bool LoadSchemaFromAbsolutePath(
    const std::filesystem::path& path,
    JsonSchema::PrefabSchema& outSchema,
    std::string* outError = nullptr
);

bool LoadSchemaFromAssetRef(
    const std::string& assetRef,
    JsonSchema::PrefabSchema& outSchema,
    std::string* outError = nullptr
);

bool InstantiateSchemaIntoScene(
    PScene scene,
    const JsonSchema::PrefabSchema& schema,
    const PrefabInstantiateOptions& options = PrefabInstantiateOptions{},
    PrefabInstantiateResult* outResult = nullptr,
    std::string* outError = nullptr
);

bool InstantiateFromAbsolutePath(
    PScene scene,
    const std::filesystem::path& path,
    const PrefabInstantiateOptions& options = PrefabInstantiateOptions{},
    PrefabInstantiateResult* outResult = nullptr,
    std::string* outError = nullptr
);

bool InstantiateFromAssetRef(
    PScene scene,
    const std::string& assetRef,
    const PrefabInstantiateOptions& options = PrefabInstantiateOptions{},
    PrefabInstantiateResult* outResult = nullptr,
    std::string* outError = nullptr
);

} // namespace PrefabIO

#endif // SERIALIZATION_IO_PREFAB_IO_H
