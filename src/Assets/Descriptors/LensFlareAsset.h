/**
 * @file src/Assets/Descriptors/LensFlareAsset.h
 * @brief Declarations for LensFlareAsset.
 */

#ifndef LENS_FLARE_ASSET_H
#define LENS_FLARE_ASSET_H

#include <filesystem>
#include <string>
#include <vector>

#include "Foundation/Math/Math3D.h"

/// @brief Enumerates element shapes for flare composition.
enum class LensFlareElementType {
    Image,
    Polygon,
    Circle
};

/// @brief Holds data for an explicit flare element.
struct LensFlareElementData {
    LensFlareElementType type = LensFlareElementType::Image;
    std::string textureRef;
    Math3D::Vec3 tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
    float intensity = 1.0f;
    float axisPosition = 1.0f; // 1.0 = source, 0.0 = center, negative = mirrored past center
    float sizeScale = 1.0f;
    int polygonSides = 6;
};

/// @brief Holds data for LensFlareAssetData.
struct LensFlareAssetData {
    std::string name;
    std::string textureRef;
    Math3D::Vec3 tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
    float intensity = 1.0f;
    float spriteScale = 132.0f;
    float ghostIntensity = 0.55f;
    float ghostSpacing = 0.38f;
    float haloIntensity = 0.28f;
    float haloScale = 1.55f;
    float glareThreshold = 1.0f;
    float glareIntensity = 0.14f;
    float glareLengthPx = 96.0f;
    float glareFalloff = 1.35f;
    std::vector<LensFlareElementData> elements;
};

namespace LensFlareAssetIO {
    bool IsLensFlareAssetPath(const std::filesystem::path& path);
    bool LoadFromAbsolutePath(const std::filesystem::path& path, LensFlareAssetData& outData, std::string* outError = nullptr);
    bool LoadFromAssetRef(const std::string& assetRef, LensFlareAssetData& outData, std::string* outError = nullptr);
    bool SaveToAbsolutePath(const std::filesystem::path& path, const LensFlareAssetData& data, std::string* outError = nullptr);
    bool SaveToAssetRef(const std::string& assetRef, const LensFlareAssetData& data, std::string* outError = nullptr);
}

using LensFlareDescriptorData = LensFlareAssetData;
namespace LensFlareDescriptorIO = LensFlareAssetIO;

#endif // LENS_FLARE_ASSET_H
