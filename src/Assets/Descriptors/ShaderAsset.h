/**
 * @file src/Assets/Descriptors/ShaderAsset.h
 * @brief Declarations for ShaderAsset.
 */

#ifndef SHADER_ASSET_H
#define SHADER_ASSET_H

#include <filesystem>
#include <memory>
#include <string>

class ShaderProgram;

// Descriptor wrapper around shader program wiring (text metadata file, not Asset subclass).
/// @brief Holds data for ShaderAssetData.
struct ShaderAssetData {
    std::string cacheName;

    std::string vertexAssetRef;
    std::string fragmentAssetRef;

    // optional shaders
    std::string geometryAssetRef;
    std::string tesselationAssetRef;
    std::string computeAssetRef;
    std::string taskAssetRef;
    std::string rtAssetRef;
   
    /**
     * @brief Checks whether complete.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool isComplete() const {
        return !vertexAssetRef.empty() && !fragmentAssetRef.empty();
    }
};

// Legacy compatibility name. Prefer `ShaderDescriptorIO` in new code.
namespace ShaderAssetIO {
    /**
     * @brief Checks whether shader asset path.
     * @param path Filesystem path for path.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool IsShaderAssetPath(const std::filesystem::path& path);
    /**
     * @brief Converts an asset reference to an absolute path.
     * @param assetRef Reference to asset.
     * @return Result of this operation.
     */
    std::filesystem::path AssetRefToAbsolutePath(const std::string& assetRef);
    /**
     * @brief Converts an absolute path to an asset reference.
     * @param absolutePath Filesystem path for absolute path.
     * @return Resulting string value.
     */
    std::string AbsolutePathToAssetRef(const std::filesystem::path& absolutePath);

    /**
     * @brief Loads from absolute path.
     * @param path Filesystem path for path.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAbsolutePath(const std::filesystem::path& path, ShaderAssetData& outData, std::string* outError = nullptr);
    /**
     * @brief Loads from asset ref.
     * @param assetRef Reference to asset.
     * @param outData Buffer that receives data data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool LoadFromAssetRef(const std::string& assetRef, ShaderAssetData& outData, std::string* outError = nullptr);
    /**
     * @brief Saves to absolute path.
     * @param path Filesystem path for path.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveToAbsolutePath(const std::filesystem::path& path, const ShaderAssetData& data, std::string* outError = nullptr);
    /**
     * @brief Saves to asset ref.
     * @param assetRef Reference to asset.
     * @param data Value for data.
     * @param outError Output value for error.
     * @return True when the operation succeeds; otherwise false.
     */
    bool SaveToAssetRef(const std::string& assetRef, const ShaderAssetData& data, std::string* outError = nullptr);

    /**
     * @brief Compiles program.
     * @param data Value for data.
     * @param cacheNameOverride Value for cache name override.
     * @param forceRecompile Flag controlling force recompile.
     * @param outError Output value for error.
     * @return Pointer to the resulting object.
     */
    std::shared_ptr<ShaderProgram> CompileProgram(const ShaderAssetData& data,
                                                  const std::string& cacheNameOverride = "",
                                                  bool forceRecompile = true,
                                                  std::string* outError = nullptr);
}

// Clearer naming for new code (kept as aliases for backward compatibility).
using ShaderDescriptorData = ShaderAssetData;
namespace ShaderDescriptorIO = ShaderAssetIO;

#endif // SHADER_ASSET_H
