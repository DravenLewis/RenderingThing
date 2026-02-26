#ifndef ASSET_DESCRIPTOR_UTILS_H
#define ASSET_DESCRIPTOR_UTILS_H

#include <filesystem>
#include <string>

namespace AssetDescriptorUtils {

// Shared helpers for text-based descriptor/wrapper files (not runtime Asset subclasses).
std::filesystem::path GetAssetRootPath();
bool IsAssetRef(const std::string& value);
std::string MakeAssetRefFromRelative(const std::string& relative);
bool AssetRefToAbsolutePath(const std::string& assetRef, std::filesystem::path& outPath);
std::string AbsolutePathToAssetRef(const std::filesystem::path& absolutePath);

bool ReadTextAsset(const std::string& assetRef, std::string& outText, std::string* outError = nullptr);
bool ReadTextPath(const std::filesystem::path& path, std::string& outText, std::string* outError = nullptr);
bool ReadTextRefOrPath(const std::string& refOrPath, std::string& outText, std::string* outError = nullptr);

} // namespace AssetDescriptorUtils

#endif // ASSET_DESCRIPTOR_UTILS_H
