/**
 * @file src/Editor/Core/EditorAssetUI.h
 * @brief Declarations for EditorAssetUI.
 */

#ifndef EDITOR_ASSET_UI_H
#define EDITOR_ASSET_UI_H

#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

class Asset;

namespace EditorAssetUI {

/// @brief Enumerates values for AssetKind.
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
    LensFlareAsset,
    MaterialAsset,
    Material,
    Font,
    Text,
    Unknown
};

/// @brief Holds data for AssetTransaction.
struct AssetTransaction {
    std::filesystem::path absolutePath;
    std::string assetRef;
    std::string extension;
    AssetKind kind = AssetKind::Unknown;
    bool isDirectory = false;
};

/// @brief Holds data for PickedAssetRef.
struct PickedAssetRef {
    AssetTransaction transaction;
    std::shared_ptr<Asset> asset;
};

/**
 * @brief Classifies an asset path.
 * @param path Filesystem path for path.
 * @param isDirectory Filesystem path for is directory.
 * @return Result of this operation.
 */
AssetKind ClassifyPath(const std::filesystem::path& path, bool isDirectory);
/**
 * @brief Checks whether kind compatible.
 * @param offered Value for offered.
 * @param requested Value for requested.
 * @return True when the condition is satisfied; otherwise false.
 */
bool IsKindCompatible(AssetKind offered, AssetKind requested);
/**
 * @brief Checks whether kind compatible any.
 * @param offered Value for offered.
 * @param requestedKinds Mode or type selector.
 * @param requestedKindCount Number of elements or bytes.
 * @return True when the condition is satisfied; otherwise false.
 */
bool IsKindCompatibleAny(AssetKind offered, const AssetKind* requestedKinds, size_t requestedKindCount);
/**
 * @brief Builds transaction.
 * @param absolutePath Filesystem path for absolute path.
 * @param assetRoot Value for asset root.
 * @param out Output value for out.
 * @return True when the operation succeeds; otherwise false.
 */
bool BuildTransaction(const std::filesystem::path& absolutePath, const std::filesystem::path& assetRoot, AssetTransaction& out);
/**
 * @brief Checks whether resolve picked asset ref.
 * @param tx Value for tx.
 * @param out Output value for out.
 * @return True when the operation succeeds; otherwise false.
 */
bool ResolvePickedAssetRef(const AssetTransaction& tx, PickedAssetRef& out);
/**
 * @brief Invalidates all cached thumbnails.
 */
void InvalidateAllThumbnails();
/**
 * @brief Invalidates the material thumbnail cache entry.
 * @param assetRef Reference to asset.
 */
void InvalidateMaterialThumbnail(const std::string& assetRef = "");

/// @brief Represents the AssetBrowserWidget type.
class AssetBrowserWidget {
    public:
        /// @brief Holds data for DrawResult.
        struct DrawResult {
            bool selectionChanged = false;
            bool selectionActivated = false;
            bool itemContextRequested = false;
            bool backgroundContextRequested = false;
        };

        /**
         * @brief Sets the asset root.
         * @param rootPath Filesystem path for root path.
         */
        void setAssetRoot(const std::filesystem::path& rootPath);
        /**
         * @brief Sets the current directory.
         * @param directoryPath Filesystem path for directory path.
         */
        void setCurrentDirectory(const std::filesystem::path& directoryPath);
        /**
         * @brief Sets the selected path.
         * @param selectedPath Filesystem path for selected path.
         */
        void setSelectedPath(const std::filesystem::path& selectedPath);
        /**
         * @brief Sets the requested kinds.
         * @param requestedKinds Mode or type selector.
         * @param requestedKindCount Number of elements or bytes.
         */
        void setRequestedKinds(const AssetKind* requestedKinds, size_t requestedKindCount);
        /**
         * @brief Resets requested kinds.
         */
        void resetRequestedKinds();
        /**
         * @brief Sets the tile size.
         * @param size Number of elements or bytes.
         */
        void setTileSize(float size);
        /**
         * @brief Synchronizes selection from the current value.
         * @param currentValue Value for current value.
         */
        void syncSelectionFromValue(const std::string& currentValue);
        /**
         * @brief Navigates to the asset root.
         */
        void goToRoot();
        /**
         * @brief Navigates to the parent directory.
         */
        void goUp();

        /**
         * @brief Draws this object.
         * @param childId Identifier or index value.
         * @param footerReserve Value for footer reserve.
         * @return Result of this operation.
         */
        DrawResult draw(const char* childId, float footerReserve = 0.0f);

        /**
         * @brief Returns the asset root.
         * @return Reference to the resulting value.
         */
        const std::filesystem::path& getAssetRoot() const;
        /**
         * @brief Returns the current directory.
         * @return Reference to the resulting value.
         */
        const std::filesystem::path& getCurrentDirectory() const;
        /**
         * @brief Returns the selected path.
         * @return Reference to the resulting value.
         */
        const std::filesystem::path& getSelectedPath() const;
        /**
         * @brief Checks whether browsing bundle.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool isBrowsingBundle() const;
        /**
         * @brief Checks whether selected asset.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool hasSelectedAsset() const;
        /**
         * @brief Checks whether try get selected transaction.
         * @param out Output value for out.
         * @return True when the operation succeeds; otherwise false.
         */
        bool tryGetSelectedTransaction(AssetTransaction& out) const;
        /**
         * @brief Checks whether try get selected reference.
         * @param out Output value for out.
         * @return True when the operation succeeds; otherwise false.
         */
        bool tryGetSelectedReference(PickedAssetRef& out) const;

    private:
        std::filesystem::path assetRoot;
        std::filesystem::path currentDirectory;
        std::filesystem::path selectedPath;
        float tileSize = 76.0f;
        std::vector<AssetKind> requestedKinds;
    };

/**
 * @brief Checks whether draw asset tile.
 * @param id Identifier or index value.
 * @param tx Value for tx.
 * @param iconSize Number of elements or bytes.
 * @param selected Value for selected.
 * @param outDoubleClicked Output value for double clicked.
 * @return True when the operation succeeds; otherwise false.
 */
bool DrawAssetTile(const char* id, const AssetTransaction& tx, float iconSize, bool selected = false, bool* outDoubleClicked = nullptr);
/**
 * @brief Begins asset drag source.
 * @param tx Value for tx.
 */
void BeginAssetDragSource(const AssetTransaction& tx);
/**
 * @brief Checks whether accept asset drop.
 * @param requestedKind Mode or type selector.
 * @param out Output value for out.
 * @param acceptBeforeDelivery Value for accept before delivery.
 * @param outIsDelivery Output value for is delivery.
 * @return True when the operation succeeds; otherwise false.
 */
bool AcceptAssetDrop(AssetKind requestedKind, AssetTransaction& out, bool acceptBeforeDelivery = false, bool* outIsDelivery = nullptr);
/**
 * @brief Checks whether accept asset drop.
 * @param requestedKinds Mode or type selector.
 * @param requestedKindCount Number of elements or bytes.
 * @param out Output value for out.
 * @param acceptBeforeDelivery Value for accept before delivery.
 * @param outIsDelivery Output value for is delivery.
 * @return True when the operation succeeds; otherwise false.
 */
bool AcceptAssetDrop(const AssetKind* requestedKinds, size_t requestedKindCount, AssetTransaction& out, bool acceptBeforeDelivery = false, bool* outIsDelivery = nullptr);
/**
 * @brief Checks whether accept asset drop in current target.
 * @param requestedKind Mode or type selector.
 * @param out Output value for out.
 * @param acceptBeforeDelivery Value for accept before delivery.
 * @param outIsDelivery Output value for is delivery.
 * @return True when the operation succeeds; otherwise false.
 */
bool AcceptAssetDropInCurrentTarget(AssetKind requestedKind, AssetTransaction& out, bool acceptBeforeDelivery = false, bool* outIsDelivery = nullptr);
/**
 * @brief Checks whether accept asset drop in current target.
 * @param requestedKinds Mode or type selector.
 * @param requestedKindCount Number of elements or bytes.
 * @param out Output value for out.
 * @param acceptBeforeDelivery Value for accept before delivery.
 * @param outIsDelivery Output value for is delivery.
 * @return True when the operation succeeds; otherwise false.
 */
bool AcceptAssetDropInCurrentTarget(const AssetKind* requestedKinds, size_t requestedKindCount, AssetTransaction& out, bool acceptBeforeDelivery = false, bool* outIsDelivery = nullptr);
/**
 * @brief Checks whether draw asset drop input.
 * @param label Value for label.
 * @param buffer Value for buffer.
 * @param bufferSize Number of elements or bytes.
 * @param requestedKind Mode or type selector.
 * @param readOnly Value for read only.
 * @param outDropped Output value for dropped.
 * @return True when the operation succeeds; otherwise false.
 */
bool DrawAssetDropInput(const char* label, char* buffer, size_t bufferSize, AssetKind requestedKind, bool readOnly = false, bool* outDropped = nullptr);
/**
 * @brief Checks whether draw asset drop input.
 * @param label Value for label.
 * @param value Value for value.
 * @param requestedKinds Mode or type selector.
 * @param requestedKindCount Number of elements or bytes.
 * @param readOnly Value for read only.
 * @param outDropped Output value for dropped.
 * @return True when the operation succeeds; otherwise false.
 */
bool DrawAssetDropInput(const char* label, std::string& value, const AssetKind* requestedKinds, size_t requestedKindCount, bool readOnly = false, bool* outDropped = nullptr);
/**
 * @brief Checks whether draw asset drop input.
 * @param label Value for label.
 * @param value Value for value.
 * @param requestedKinds Mode or type selector.
 * @param readOnly Value for read only.
 * @param outDropped Output value for dropped.
 * @return True when the operation succeeds; otherwise false.
 */
bool DrawAssetDropInput(const char* label, std::string& value, std::initializer_list<AssetKind> requestedKinds, bool readOnly = false, bool* outDropped = nullptr);

/**
 * @brief Checks whether try replace pointer.
 * @param current Value for current.
 * @param candidate Flag controlling candidate.
 * @return True when the operation succeeds; otherwise false.
 */
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
