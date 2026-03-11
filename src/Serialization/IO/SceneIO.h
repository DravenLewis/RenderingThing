/**
 * @file src/Serialization/IO/SceneIO.h
 * @brief Declarations for SceneIO.
 */

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

/// @brief Holds data for SceneSaveOptions.
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

/// @brief Holds data for SceneLoadOptions.
struct SceneLoadOptions {
    bool clearExistingScene = true;
    bool applySceneSettings = true;
    const Serialization::ComponentSerializationRegistry* registry = nullptr;
};

/// @brief Holds data for SceneLoadResult.
struct SceneLoadResult {
    std::vector<NeoECS::GameObject*> rootObjects;
    std::unordered_map<std::uint64_t, NeoECS::ECSEntity*> snapshotIdToEntity;
    JsonSchema::RawJsonValue editorState;
};

/**
 * @brief Builds schema from scene.
 * @param scene Value for scene.
 * @param outSchema Output value for schema.
 * @param options Configuration options.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool BuildSchemaFromScene(
    PScene scene,
    JsonSchema::SceneSchema& outSchema,
    const SceneSaveOptions& options = SceneSaveOptions{},
    std::string* outError = nullptr
);

/**
 * @brief Saves scene to absolute path.
 * @param scene Value for scene.
 * @param path Filesystem path for path.
 * @param options Configuration options.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool SaveSceneToAbsolutePath(
    PScene scene,
    const std::filesystem::path& path,
    const SceneSaveOptions& options = SceneSaveOptions{},
    std::string* outError = nullptr
);

/**
 * @brief Saves scene to asset ref.
 * @param scene Value for scene.
 * @param assetRef Reference to asset.
 * @param options Configuration options.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool SaveSceneToAssetRef(
    PScene scene,
    const std::string& assetRef,
    const SceneSaveOptions& options = SceneSaveOptions{},
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
    JsonSchema::SceneSchema& outSchema,
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
    JsonSchema::SceneSchema& outSchema,
    std::string* outError = nullptr
);

/**
 * @brief Applies schema to scene.
 * @param scene Value for scene.
 * @param schema Value for schema.
 * @param options Configuration options.
 * @param outResult Output value for result.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool ApplySchemaToScene(
    PScene scene,
    const JsonSchema::SceneSchema& schema,
    const SceneLoadOptions& options = SceneLoadOptions{},
    SceneLoadResult* outResult = nullptr,
    std::string* outError = nullptr
);

/**
 * @brief Loads scene from absolute path.
 * @param scene Value for scene.
 * @param path Filesystem path for path.
 * @param options Configuration options.
 * @param outResult Output value for result.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool LoadSceneFromAbsolutePath(
    PScene scene,
    const std::filesystem::path& path,
    const SceneLoadOptions& options = SceneLoadOptions{},
    SceneLoadResult* outResult = nullptr,
    std::string* outError = nullptr
);

/**
 * @brief Loads scene from asset ref.
 * @param scene Value for scene.
 * @param assetRef Reference to asset.
 * @param options Configuration options.
 * @param outResult Output value for result.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool LoadSceneFromAssetRef(
    PScene scene,
    const std::string& assetRef,
    const SceneLoadOptions& options = SceneLoadOptions{},
    SceneLoadResult* outResult = nullptr,
    std::string* outError = nullptr
);

} // namespace SceneIO

#endif // SERIALIZATION_IO_SCENE_IO_H
