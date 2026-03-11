/**
 * @file src/Assets/Descriptors/ModelAsset.h
 * @brief Declarations for ModelAsset.
 */

#ifndef MODEL_ASSET_H
#define MODEL_ASSET_H

#include <filesystem>
#include <memory>
#include <string>

#include "Rendering/Geometry/Model.h"

// Descriptor wrapper around reusable model-loading settings (text metadata file, not Asset subclass).
/// @brief Holds data for ModelAssetData.
struct ModelAssetData {
    std::string name;
    std::string linkParentRef;
    std::string sourceModelRef;
    std::string defaultMaterialRef;
    int forceSmoothNormals = 1;
};

// Legacy compatibility name. Prefer `ModelDescriptorIO` in new code.
namespace ModelAssetIO {
    /**
     * @brief Checks whether model asset path.
     * @param path Filesystem path for path.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool IsModelAssetPath(const std::filesystem::path& path);
    /**
     * @brief Checks whether resolve model asset ref.
     * @param modelAssetRefOrPath Filesystem path for model asset reference or path.
     * @param outAssetRef Output value for asset.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool ResolveModelAssetRef(const std::string& modelAssetRefOrPath, std::string& outAssetRef, std::string* outError = nullptr);

    /**
     * @brief Loads from absolute path.
     * @param path Filesystem path for path.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAbsolutePath(const std::filesystem::path& path, ModelAssetData& outData, std::string* outError = nullptr);
    /**
     * @brief Loads from asset ref.
     * @param assetRef Reference to asset.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAssetRef(const std::string& assetRef, ModelAssetData& outData, std::string* outError = nullptr);
    /**
     * @brief Saves to absolute path.
     * @param path Filesystem path for path.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveToAbsolutePath(const std::filesystem::path& path, const ModelAssetData& data, std::string* outError = nullptr);
    /**
     * @brief Saves to asset ref.
     * @param assetRef Reference to asset.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveToAssetRef(const std::string& assetRef, const ModelAssetData& data, std::string* outError = nullptr);

    /**
     * @brief Instantiates a model object.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return Pointer to the resulting object.
     */
    std::shared_ptr<Model> InstantiateModel(const ModelAssetData& data, std::string* outError = nullptr);
    /**
     * @brief Instantiates a model from an asset reference.
     * @param modelAssetRefOrPath Filesystem path for model asset reference or path.
     * @param outResolvedAssetRef Output value for resolved asset.
     * @param outError Output value for error.
     * @return Pointer to the resulting object.
     */
    std::shared_ptr<Model> InstantiateModelFromRef(const std::string& modelAssetRefOrPath, std::string* outResolvedAssetRef = nullptr, std::string* outError = nullptr);
}

// Clearer naming for new code (kept as aliases for backward compatibility).
using ModelDescriptorData = ModelAssetData;
namespace ModelDescriptorIO = ModelAssetIO;

#endif // MODEL_ASSET_H
