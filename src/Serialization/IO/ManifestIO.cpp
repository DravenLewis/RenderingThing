/**
 * @file src/Serialization/IO/ManifestIO.cpp
 * @brief Implementation for ManifestIO.
 */

#include "Serialization/IO/ManifestIO.h"

namespace {

void setManifestIoError(std::string* outError, const std::string& message){
    if(outError){
        *outError = message;
    }
}

} // namespace

namespace ManifestIO {

bool LoadGameManifestFromAbsolutePath(
    const std::filesystem::path& path,
    JsonSchema::GameManifestSchema& outManifest,
    std::string* outError)
{
    outManifest.Clear();
    return outManifest.LoadFromAbsolutePath(path, outError);
}

bool LoadGameManifestFromAssetRef(
    const std::string& assetRef,
    JsonSchema::GameManifestSchema& outManifest,
    std::string* outError)
{
    outManifest.Clear();
    return outManifest.LoadFromAssetRef(assetRef, outError);
}

bool ResolveStartupSceneRef(
    const JsonSchema::GameManifestSchema& manifest,
    std::string& outSceneRef,
    std::string* outError)
{
    outSceneRef.clear();
    if(manifest.startupScene.empty()){
        setManifestIoError(outError, "Manifest startupScene is empty.");
        return false;
    }

    for(const JsonSchema::GameManifestSchema::SceneEntry& scene : manifest.scenes){
        if(scene.id == manifest.startupScene || scene.ref == manifest.startupScene){
            if(scene.ref.empty()){
                setManifestIoError(outError, "Manifest startupScene resolved to an entry without scene ref.");
                return false;
            }
            outSceneRef = scene.ref;
            return true;
        }
    }

    setManifestIoError(
        outError,
        "Manifest startupScene '" + manifest.startupScene + "' did not resolve to any scene ref.");
    return false;
}

} // namespace ManifestIO
