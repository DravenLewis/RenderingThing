/**
 * @file src/Assets/Core/AssetDescriptorUtils.h
 * @brief Declarations for AssetDescriptorUtils.
 */

#ifndef ASSET_DESCRIPTOR_UTILS_H
#define ASSET_DESCRIPTOR_UTILS_H

#include <filesystem>
#include <string>

namespace AssetDescriptorUtils {

// Shared helpers for text-based descriptor/wrapper files (not runtime Asset subclasses).
std::filesystem::path GetAssetRootPath();
/**
 * @brief Checks whether asset ref.
 * @param value Value for value.
 * @return True when the condition is satisfied; otherwise false.
 */
bool IsAssetRef(const std::string& value);
/**
 * @brief Creates asset ref from relative.
 * @param relative Value for relative.
 * @return Resulting string value.
 */
std::string MakeAssetRefFromRelative(const std::string& relative);
/**
 * @brief Checks whether asset ref to absolute path.
 * @param assetRef Reference to asset.
 * @param outPath Filesystem path for path.
 * @return True when the operation succeeds; otherwise false.
 */
bool AssetRefToAbsolutePath(const std::string& assetRef, std::filesystem::path& outPath);
/**
 * @brief Converts an absolute path to an asset reference.
 * @param absolutePath Filesystem path for absolute path.
 * @return Resulting string value.
 */
std::string AbsolutePathToAssetRef(const std::filesystem::path& absolutePath);
/**
 * @brief Checks whether path exists.
 * @param path Filesystem path for path.
 * @param outIsDirectory Filesystem path for is directory.
 * @return True when the operation succeeds; otherwise false.
 */
bool PathExists(const std::filesystem::path& path, bool* outIsDirectory = nullptr);

/**
 * @brief Reads text asset.
 * @param assetRef Reference to asset.
 * @param outText Buffer that receives text data.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool ReadTextAsset(const std::string& assetRef, std::string& outText, std::string* outError = nullptr);
/**
 * @brief Writes text asset.
 * @param assetRef Reference to asset.
 * @param text Value for text.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool WriteTextAsset(const std::string& assetRef, const std::string& text, std::string* outError = nullptr);
/**
 * @brief Reads text path.
 * @param path Filesystem path for path.
 * @param outText Buffer that receives text data.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool ReadTextPath(const std::filesystem::path& path, std::string& outText, std::string* outError = nullptr);
/**
 * @brief Writes text path.
 * @param path Filesystem path for path.
 * @param text Value for text.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool WriteTextPath(const std::filesystem::path& path, const std::string& text, std::string* outError = nullptr);
/**
 * @brief Reads text ref or path.
 * @param refOrPath Filesystem path for reference or path.
 * @param outText Buffer that receives text data.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool ReadTextRefOrPath(const std::string& refOrPath, std::string& outText, std::string* outError = nullptr);
/**
 * @brief Writes text ref or path.
 * @param refOrPath Filesystem path for reference or path.
 * @param text Value for text.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool WriteTextRefOrPath(const std::string& refOrPath, const std::string& text, std::string* outError = nullptr);

} // namespace AssetDescriptorUtils

#endif // ASSET_DESCRIPTOR_UTILS_H
