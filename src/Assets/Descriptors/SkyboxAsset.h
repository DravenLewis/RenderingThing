#ifndef SKYBOX_ASSET_H
#define SKYBOX_ASSET_H

#include <filesystem>
#include <memory>
#include <string>

class SkyBox;

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
    bool IsSkyboxAssetPath(const std::filesystem::path& path);
    bool LoadFromAbsolutePath(const std::filesystem::path& path, SkyboxAssetData& outData, std::string* outError = nullptr);
    bool LoadFromAssetRef(const std::string& assetRef, SkyboxAssetData& outData, std::string* outError = nullptr);
    bool SaveToAbsolutePath(const std::filesystem::path& path, const SkyboxAssetData& data, std::string* outError = nullptr);
    bool SaveToAssetRef(const std::string& assetRef, const SkyboxAssetData& data, std::string* outError = nullptr);
    bool HasRequiredFaces(const SkyboxAssetData& data);
    std::shared_ptr<SkyBox> InstantiateSkyBox(const SkyboxAssetData& data, std::string* outError = nullptr);
    std::shared_ptr<SkyBox> InstantiateSkyBoxFromRef(const std::string& assetRef, std::string* outError = nullptr);
}

using SkyboxDescriptorData = SkyboxAssetData;
namespace SkyboxDescriptorIO = SkyboxAssetIO;

#endif // SKYBOX_ASSET_H
