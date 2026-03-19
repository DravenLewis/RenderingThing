/**
 * @file src/Assets/Descriptors/ImageAsset.h
 * @brief Declarations for ImageAsset.
 */

#ifndef IMAGE_ASSET_H
#define IMAGE_ASSET_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "Rendering/Textures/Texture.h"

/// @brief Enumerates values for ImageAssetFilterMode.
enum class ImageAssetFilterMode {
    Nearest = 0,
    Linear,
    Trilinear
};

/// @brief Enumerates values for ImageAssetWrapMode.
enum class ImageAssetWrapMode {
    Repeat = 0,
    ClampEdge,
    ClampBorder
};

/// @brief Enumerates values for ImageAssetMapType.
enum class ImageAssetMapType {
    Color = 0,
    Normal,
    Height,
    Roughness,
    Metallic,
    Occlusion,
    Emissive,
    Opacity,
    Data
};

/// @brief Holds data for ImageAssetData.
struct ImageAssetData {
    std::string name;
    std::string linkParentRef;
    std::string sourceImageRef;
    ImageAssetFilterMode filterMode = ImageAssetFilterMode::Nearest;
    ImageAssetWrapMode wrapMode = ImageAssetWrapMode::Repeat;
    ImageAssetMapType mapType = ImageAssetMapType::Color;
    int supportsAlpha = 1;
    int flipVertical = 1;
};

// Legacy compatibility name. Prefer `ImageDescriptorIO` in new code.
namespace ImageAssetIO {
    bool IsImageAssetPath(const std::filesystem::path& path);
    bool IsRawImagePath(const std::filesystem::path& path);

    const char* FilterModeToString(ImageAssetFilterMode mode);
    ImageAssetFilterMode FilterModeFromString(const std::string& value);
    const char* WrapModeToString(ImageAssetWrapMode mode);
    ImageAssetWrapMode WrapModeFromString(const std::string& value);
    const char* MapTypeToString(ImageAssetMapType type);
    ImageAssetMapType MapTypeFromString(const std::string& value);

    bool ResolveImageAssetRef(const std::string& imageAssetRefOrPath, std::string& outAssetRef, std::string* outError = nullptr);
    bool ResolveTextureSourceAssetRef(const std::string& imageOrAssetRef,
                                      std::string& outSourceAssetRef,
                                      std::string* outResolvedImageAssetRef = nullptr,
                                      std::string* outError = nullptr);
    bool RefDependsOnAsset(const std::string& imageOrAssetRef, const std::string& changedAsset);
    std::uint64_t GetTextureRefRevision(const std::string& imageOrAssetRef);

    bool LoadFromAbsolutePath(const std::filesystem::path& path, ImageAssetData& outData, std::string* outError = nullptr);
    bool LoadFromAssetRef(const std::string& assetRef, ImageAssetData& outData, std::string* outError = nullptr);
    bool SaveToAbsolutePath(const std::filesystem::path& path, const ImageAssetData& data, std::string* outError = nullptr);
    bool SaveToAssetRef(const std::string& assetRef, const ImageAssetData& data, std::string* outError = nullptr);

    std::shared_ptr<Texture> InstantiateTexture(const ImageAssetData& data, std::string* outError = nullptr);
    std::shared_ptr<Texture> InstantiateTextureFromRef(const std::string& imageOrAssetRef,
                                                       std::string* outResolvedImageAssetRef = nullptr,
                                                       std::string* outError = nullptr);
}

using ImageDescriptorData = ImageAssetData;
namespace ImageDescriptorIO = ImageAssetIO;

#endif // IMAGE_ASSET_H
