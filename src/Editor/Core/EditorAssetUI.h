#ifndef EDITOR_ASSET_UI_H
#define EDITOR_ASSET_UI_H

#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

class Asset;

namespace EditorAssetUI {

enum class AssetKind : int {
    Any = 0,
    Directory,
    Image,
    Model,
    ModelAsset,
    ShaderVertex,
    ShaderFragment,
    ShaderGrometry,
    ShaderGeometry = ShaderGrometry,
    ShaderCompute,
    ShaderTesselation,
    ShaderTask,
    ShaderRaytrace,
    ShaderGeneric,
    ShaderAsset,
    SkyboxAsset,
    MaterialAsset,
    Material,
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

struct PickedAssetRef {
    AssetTransaction transaction;
    std::shared_ptr<Asset> asset;
};

AssetKind ClassifyPath(const std::filesystem::path& path, bool isDirectory);
bool IsKindCompatible(AssetKind offered, AssetKind requested);
bool IsKindCompatibleAny(AssetKind offered, const AssetKind* requestedKinds, size_t requestedKindCount);
bool BuildTransaction(const std::filesystem::path& absolutePath, const std::filesystem::path& assetRoot, AssetTransaction& out);
bool ResolvePickedAssetRef(const AssetTransaction& tx, PickedAssetRef& out);
void InvalidateAllThumbnails();
void InvalidateMaterialThumbnail(const std::string& assetRef = "");

class AssetBrowserWidget {
    public:
        struct DrawResult {
            bool selectionChanged = false;
            bool selectionActivated = false;
            bool itemContextRequested = false;
            bool backgroundContextRequested = false;
        };

        void setAssetRoot(const std::filesystem::path& rootPath);
        void setCurrentDirectory(const std::filesystem::path& directoryPath);
        void setSelectedPath(const std::filesystem::path& selectedPath);
        void setRequestedKinds(const AssetKind* requestedKinds, size_t requestedKindCount);
        void resetRequestedKinds();
        void setTileSize(float size);
        void syncSelectionFromValue(const std::string& currentValue);
        void goToRoot();
        void goUp();

        DrawResult draw(const char* childId, float footerReserve = 0.0f);

        const std::filesystem::path& getAssetRoot() const;
        const std::filesystem::path& getCurrentDirectory() const;
        const std::filesystem::path& getSelectedPath() const;
        bool isBrowsingBundle() const;
        bool hasSelectedAsset() const;
        bool tryGetSelectedTransaction(AssetTransaction& out) const;
        bool tryGetSelectedReference(PickedAssetRef& out) const;

    private:
        std::filesystem::path assetRoot;
        std::filesystem::path currentDirectory;
        std::filesystem::path selectedPath;
        float tileSize = 76.0f;
        std::vector<AssetKind> requestedKinds;
    };

bool DrawAssetTile(const char* id, const AssetTransaction& tx, float iconSize, bool selected = false, bool* outDoubleClicked = nullptr);
void BeginAssetDragSource(const AssetTransaction& tx);
bool AcceptAssetDrop(AssetKind requestedKind, AssetTransaction& out, bool acceptBeforeDelivery = false, bool* outIsDelivery = nullptr);
bool AcceptAssetDrop(const AssetKind* requestedKinds, size_t requestedKindCount, AssetTransaction& out, bool acceptBeforeDelivery = false, bool* outIsDelivery = nullptr);
bool AcceptAssetDropInCurrentTarget(AssetKind requestedKind, AssetTransaction& out, bool acceptBeforeDelivery = false, bool* outIsDelivery = nullptr);
bool AcceptAssetDropInCurrentTarget(const AssetKind* requestedKinds, size_t requestedKindCount, AssetTransaction& out, bool acceptBeforeDelivery = false, bool* outIsDelivery = nullptr);
bool DrawAssetDropInput(const char* label, char* buffer, size_t bufferSize, AssetKind requestedKind, bool readOnly = false, bool* outDropped = nullptr);
bool DrawAssetDropInput(const char* label, std::string& value, const AssetKind* requestedKinds, size_t requestedKindCount, bool readOnly = false, bool* outDropped = nullptr);
bool DrawAssetDropInput(const char* label, std::string& value, std::initializer_list<AssetKind> requestedKinds, bool readOnly = false, bool* outDropped = nullptr);

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
