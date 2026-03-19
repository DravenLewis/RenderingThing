/**
 * @file src/Assets/Descriptors/EnvironmentAsset.h
 * @brief Declarations for EnvironmentAsset.
 */

#ifndef ENVIRONMENT_ASSET_H
#define ENVIRONMENT_ASSET_H

#include <filesystem>
#include <string>

#include "Rendering/Lighting/Environment.h"

/// @brief Holds serializable data for an environment asset.
struct EnvironmentAssetData {
    std::string name;
    std::string skyboxAssetRef;
    EnvironmentSettings settings;
};

namespace EnvironmentAssetIO {
    /**
     * @brief Checks whether a path is an environment asset file.
     * @param path Filesystem path for path.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool IsEnvironmentAssetPath(const std::filesystem::path& path);
    /**
     * @brief Loads environment asset data from an absolute path.
     * @param path Filesystem path for path.
     * @param outData Output value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAbsolutePath(const std::filesystem::path& path, EnvironmentAssetData& outData, std::string* outError = nullptr);
    /**
     * @brief Loads environment asset data from an asset ref.
     * @param assetRef Reference to asset.
     * @param outData Output value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAssetRef(const std::string& assetRef, EnvironmentAssetData& outData, std::string* outError = nullptr);
    /**
     * @brief Saves environment asset data to an absolute path.
     * @param path Filesystem path for path.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveToAbsolutePath(const std::filesystem::path& path, const EnvironmentAssetData& data, std::string* outError = nullptr);
    /**
     * @brief Saves environment asset data to an asset ref.
     * @param assetRef Reference to asset.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveToAssetRef(const std::string& assetRef, const EnvironmentAssetData& data, std::string* outError = nullptr);
}

using EnvironmentDescriptorData = EnvironmentAssetData;
namespace EnvironmentDescriptorIO = EnvironmentAssetIO;

#endif // ENVIRONMENT_ASSET_H
