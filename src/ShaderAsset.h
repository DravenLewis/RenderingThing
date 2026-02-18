#ifndef SHADER_ASSET_H
#define SHADER_ASSET_H

#include <filesystem>
#include <memory>
#include <string>

class ShaderProgram;

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
   
    bool isComplete() const {
        return !vertexAssetRef.empty() && !fragmentAssetRef.empty();
    }
};

namespace ShaderAssetIO {
    bool IsShaderAssetPath(const std::filesystem::path& path);
    std::filesystem::path AssetRefToAbsolutePath(const std::string& assetRef);
    std::string AbsolutePathToAssetRef(const std::filesystem::path& absolutePath);

    bool LoadFromAbsolutePath(const std::filesystem::path& path, ShaderAssetData& outData, std::string* outError = nullptr);
    bool LoadFromAssetRef(const std::string& assetRef, ShaderAssetData& outData, std::string* outError = nullptr);
    bool SaveToAbsolutePath(const std::filesystem::path& path, const ShaderAssetData& data, std::string* outError = nullptr);
    bool SaveToAssetRef(const std::string& assetRef, const ShaderAssetData& data, std::string* outError = nullptr);

    std::shared_ptr<ShaderProgram> CompileProgram(const ShaderAssetData& data,
                                                  const std::string& cacheNameOverride = "",
                                                  bool forceRecompile = true,
                                                  std::string* outError = nullptr);
}

#endif // SHADER_ASSET_H
