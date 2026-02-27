#ifndef SERIALIZATION_IO_SCENE_IO_H
#define SERIALIZATION_IO_SCENE_IO_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Scene/Scene.h"
#include "Serialization/Schema/ComponentSerializationRegistry.h"
#include "Serialization/Schema/PrefabSceneSchemas.h"

namespace SceneIO {

struct SceneSaveOptions {
    JsonSchema::DocumentMetadata metadata;
    bool autoCollectDependencies = true;
    std::vector<std::string> dependencies;
    bool includeSceneSettings = true;
    JsonSchema::RawJsonValue sceneSettingsOverride; // Optional object payload.
    bool includeEditorState = false;
    JsonSchema::RawJsonValue editorState; // Optional object payload.
    const Serialization::ComponentSerializationRegistry* registry = nullptr;
};

struct SceneLoadOptions {
    bool clearExistingScene = true;
    bool applySceneSettings = true;
    const Serialization::ComponentSerializationRegistry* registry = nullptr;
};

struct SceneLoadResult {
    std::vector<NeoECS::GameObject*> rootObjects;
    std::unordered_map<std::uint64_t, NeoECS::ECSEntity*> snapshotIdToEntity;
    JsonSchema::RawJsonValue editorState;
};

bool BuildSchemaFromScene(
    PScene scene,
    JsonSchema::SceneSchema& outSchema,
    const SceneSaveOptions& options = SceneSaveOptions{},
    std::string* outError = nullptr
);

bool SaveSceneToAbsolutePath(
    PScene scene,
    const std::filesystem::path& path,
    const SceneSaveOptions& options = SceneSaveOptions{},
    std::string* outError = nullptr
);

bool SaveSceneToAssetRef(
    PScene scene,
    const std::string& assetRef,
    const SceneSaveOptions& options = SceneSaveOptions{},
    std::string* outError = nullptr
);

bool LoadSchemaFromAbsolutePath(
    const std::filesystem::path& path,
    JsonSchema::SceneSchema& outSchema,
    std::string* outError = nullptr
);

bool LoadSchemaFromAssetRef(
    const std::string& assetRef,
    JsonSchema::SceneSchema& outSchema,
    std::string* outError = nullptr
);

bool ApplySchemaToScene(
    PScene scene,
    const JsonSchema::SceneSchema& schema,
    const SceneLoadOptions& options = SceneLoadOptions{},
    SceneLoadResult* outResult = nullptr,
    std::string* outError = nullptr
);

bool LoadSceneFromAbsolutePath(
    PScene scene,
    const std::filesystem::path& path,
    const SceneLoadOptions& options = SceneLoadOptions{},
    SceneLoadResult* outResult = nullptr,
    std::string* outError = nullptr
);

bool LoadSceneFromAssetRef(
    PScene scene,
    const std::string& assetRef,
    const SceneLoadOptions& options = SceneLoadOptions{},
    SceneLoadResult* outResult = nullptr,
    std::string* outError = nullptr
);

} // namespace SceneIO

#endif // SERIALIZATION_IO_SCENE_IO_H
