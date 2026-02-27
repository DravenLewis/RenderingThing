#ifndef SERIALIZATION_IO_MANIFEST_IO_H
#define SERIALIZATION_IO_MANIFEST_IO_H

#include <filesystem>
#include <string>

#include "Serialization/Schema/ManifestSchemas.h"

namespace ManifestIO {

bool LoadGameManifestFromAbsolutePath(
    const std::filesystem::path& path,
    JsonSchema::GameManifestSchema& outManifest,
    std::string* outError = nullptr
);

bool LoadGameManifestFromAssetRef(
    const std::string& assetRef,
    JsonSchema::GameManifestSchema& outManifest,
    std::string* outError = nullptr
);

bool ResolveStartupSceneRef(
    const JsonSchema::GameManifestSchema& manifest,
    std::string& outSceneRef,
    std::string* outError = nullptr
);

} // namespace ManifestIO

#endif // SERIALIZATION_IO_MANIFEST_IO_H
