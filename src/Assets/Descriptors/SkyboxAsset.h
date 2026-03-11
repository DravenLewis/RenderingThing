/**
 * @file src/Assets/Descriptors/SkyboxAsset.h
 * @brief Declarations for SkyboxAsset.
 */

#ifndef SKYBOX_ASSET_H
#define SKYBOX_ASSET_H

#include <filesystem>
#include <memory>
#include <string>

class SkyBox;

/// @brief Holds data for SkyboxAssetData.
struct SkyboxAssetData {
    std::string name;
    std::string rightFaceRef;
    std::string leftFaceRef;
    std::string topFaceRef;
    std::string bottomFaceRef;
    std::string frontFaceRef;
    std::string backFaceRef;
};

namespace SkyboxAssetIO {
    /**
     * @brief Checks whether skybox asset path.
     * @param path Filesystem path for path.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool IsSkyboxAssetPath(const std::filesystem::path& path);
    /**
     * @brief Loads from absolute path.
     * @param path Filesystem path for path.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAbsolutePath(const std::filesystem::path& path, SkyboxAssetData& outData, std::string* outError = nullptr);
    /**
     * @brief Loads from asset ref.
     * @param assetRef Reference to asset.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAssetRef(const std::string& assetRef, SkyboxAssetData& outData, std::string* outError = nullptr);
    /**
     * @brief Saves to absolute path.
     * @param path Filesystem path for path.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveToAbsolutePath(const std::filesystem::path& path, const SkyboxAssetData& data, std::string* outError = nullptr);
    /**
     * @brief Saves to asset ref.
     * @param assetRef Reference to asset.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveToAssetRef(const std::string& assetRef, const SkyboxAssetData& data, std::string* outError = nullptr);
    /**
     * @brief Checks whether required faces.
     * @param data Value for data.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool HasRequiredFaces(const SkyboxAssetData& data);
    /**
     * @brief Instantiates a skybox object.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return Pointer to the resulting object.
     */
    std::shared_ptr<SkyBox> InstantiateSkyBox(const SkyboxAssetData& data, std::string* outError = nullptr);
    /**
     * @brief Instantiates a skybox from an asset reference.
     * @param assetRef Reference to asset.
     * @param outError Output value for error.
     * @return Pointer to the resulting object.
     */
    std::shared_ptr<SkyBox> InstantiateSkyBoxFromRef(const std::string& assetRef, std::string* outError = nullptr);
}

using SkyboxDescriptorData = SkyboxAssetData;
namespace SkyboxDescriptorIO = SkyboxAssetIO;

#endif // SKYBOX_ASSET_H
