#ifndef MODEL_ASSET_H
#define MODEL_ASSET_H

#include <filesystem>
#include <memory>
#include <string>

#include "Rendering/Geometry/Model.h"

// Descriptor wrapper around reusable model-loading settings (text metadata file, not Asset subclass).
struct ModelAssetData {
    std::string name;
    std::string linkParentRef;
    std::string sourceModelRef;
    std::string defaultMaterialRef;
    int forceSmoothNormals = 1;
};

// Legacy compatibility name. Prefer `ModelDescriptorIO` in new code.
namespace ModelAssetIO {
    bool IsModelAssetPath(const std::filesystem::path& path);
    bool ResolveModelAssetRef(const std::string& modelAssetRefOrPath, std::string& outAssetRef, std::string* outError = nullptr);

    bool LoadFromAbsolutePath(const std::filesystem::path& path, ModelAssetData& outData, std::string* outError = nullptr);
    bool LoadFromAssetRef(const std::string& assetRef, ModelAssetData& outData, std::string* outError = nullptr);
    bool SaveToAbsolutePath(const std::filesystem::path& path, const ModelAssetData& data, std::string* outError = nullptr);
    bool SaveToAssetRef(const std::string& assetRef, const ModelAssetData& data, std::string* outError = nullptr);

    std::shared_ptr<Model> InstantiateModel(const ModelAssetData& data, std::string* outError = nullptr);
    std::shared_ptr<Model> InstantiateModelFromRef(const std::string& modelAssetRefOrPath, std::string* outResolvedAssetRef = nullptr, std::string* outError = nullptr);
}

// Clearer naming for new code (kept as aliases for backward compatibility).
using ModelDescriptorData = ModelAssetData;
namespace ModelDescriptorIO = ModelAssetIO;

#endif // MODEL_ASSET_H
