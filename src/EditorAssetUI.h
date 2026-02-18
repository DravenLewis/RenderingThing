#ifndef EDITOR_ASSET_UI_H
#define EDITOR_ASSET_UI_H

#include <filesystem>
#include <memory>
#include <string>

namespace EditorAssetUI {

enum class AssetKind : int {
    Any = 0,
    Directory,
    Image,
    Model,
    ShaderVertex,
    ShaderFragment,
    ShaderGeneric,
    ShaderAsset,
    MaterialAsset,
    Font,
    Text,
    Unknown
};

struct AssetTransaction {
    std::filesystem::path absolutePath;
    std::string assetRef;
    std::string extension;
    AssetKind kind = AssetKind::Unknown;
    bool isDirectory = false;
};

AssetKind ClassifyPath(const std::filesystem::path& path, bool isDirectory);
bool IsKindCompatible(AssetKind offered, AssetKind requested);
bool BuildTransaction(const std::filesystem::path& absolutePath, const std::filesystem::path& assetRoot, AssetTransaction& out);

bool DrawAssetTile(const char* id, const AssetTransaction& tx, float iconSize, bool selected = false, bool* outDoubleClicked = nullptr);
void BeginAssetDragSource(const AssetTransaction& tx);
bool AcceptAssetDrop(AssetKind requestedKind, AssetTransaction& out);
bool DrawAssetDropInput(const char* label, char* buffer, size_t bufferSize, AssetKind requestedKind, bool readOnly = false, bool* outDropped = nullptr);

template<typename T>
bool TryReplacePointer(std::shared_ptr<T>& current, const std::shared_ptr<T>& candidate){
    if(!candidate){
        return false;
    }
    current = candidate;
    return true;
}

} // namespace EditorAssetUI

#endif // EDITOR_ASSET_UI_H
