/**
 * @file src/Serialization/IO/ManifestIO.h
 * @brief Declarations for ManifestIO.
 */

#ifndef SERIALIZATION_IO_MANIFEST_IO_H
#define SERIALIZATION_IO_MANIFEST_IO_H

#include <filesystem>
#include <string>

#include "Serialization/Schema/ManifestSchemas.h"

namespace ManifestIO {

/**
 * @brief Loads game manifest from absolute path.
 * @param path Filesystem path for path.
 * @param outManifest Output value for manifest.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool LoadGameManifestFromAbsolutePath(
    const std::filesystem::path& path,
    JsonSchema::GameManifestSchema& outManifest,
    std::string* outError = nullptr
);

/**
 * @brief Loads game manifest from asset ref.
 * @param assetRef Reference to asset.
 * @param outManifest Output value for manifest.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool LoadGameManifestFromAssetRef(
    const std::string& assetRef,
    JsonSchema::GameManifestSchema& outManifest,
    std::string* outError = nullptr
);

/**
 * @brief Checks whether resolve startup scene ref.
 * @param manifest Value for manifest.
 * @param outSceneRef Output value for scene.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool ResolveStartupSceneRef(
    const JsonSchema::GameManifestSchema& manifest,
    std::string& outSceneRef,
    std::string* outError = nullptr
);

} // namespace ManifestIO

#endif // SERIALIZATION_IO_MANIFEST_IO_H
