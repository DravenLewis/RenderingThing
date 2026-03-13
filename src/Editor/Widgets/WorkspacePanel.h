/**
 * @file src/Editor/Widgets/WorkspacePanel.h
 * @brief Declarations for WorkspacePanel.
 */

#ifndef WORKSPACE_PANEL_H
#define WORKSPACE_PANEL_H

#include <filesystem>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "imgui.h"

/// @brief Represents the WorkspacePanel type.
class WorkspacePanel {
    public:
        using EntityDropToPrefabFn = std::function<bool(const std::string& entityId,
                                                        const std::filesystem::path& exportDirectory,
                                                        std::string* outError)>;
        ~WorkspacePanel();

        /**
         * @brief Sets the asset root.
         * @param rootPath Filesystem path for root path.
         */
        void setAssetRoot(const std::filesystem::path& rootPath);
        /**
         * @brief Draws this object.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param w Value for w.
         * @param h Value for h.
         * @param selectedAssetPath Filesystem path for selected asset path.
         * @param onEntityDropToPrefab Callback for on entity drop to prefab.
         */
        void draw(float x,
                  float y,
                  float w,
                  float h,
                  std::filesystem::path& selectedAssetPath,
                  const EntityDropToPrefabFn& onEntityDropToPrefab = EntityDropToPrefabFn());
        /**
         * @brief Returns the current directory.
         * @return Reference to the resulting value.
         */
        const std::filesystem::path& getCurrentDirectory() const { return assetDir; }

    private:
        std::filesystem::path assetRoot;
        std::filesystem::path assetDir;
        bool assetRenameActive = false;
        std::filesystem::path assetRenamePath;
        char assetRenameBuffer[256] = {};
        bool assetRenameFocus = false;
        std::filesystem::path pendingDeleteAssetPath;
        float assetTileSize = 76.0f;
        std::filesystem::path clipboardAssetPath;
        bool clipboardAssetCutMode = false;
        std::filesystem::path contextMenuTargetPath;
        bool contextMenuHasTarget = false;
        std::filesystem::path pickerContextMenuTargetPath;
        bool pickerContextMenuHasTarget = false;
        bool deletePopupOpenRequested = false;
        std::string deletePopupIdToOpen;
        bool requestOpenLogTab = false;

        uint64_t lastLogVersion = 0;
        uint64_t lastLogSummaryVersion = 0;
        std::string logBuffer;
        std::string latestLogLine;
        std::vector<std::string> logLines;
        std::vector<ImVec4> logColors;
        bool showAssetPickerWindow = false;
        std::unordered_set<std::string> expandedRelatedAssets;
        bool browserCacheDirty = true;
        int assetChangeListenerHandle = -1;
        int lastExternalDirectoryValidationFrame = -100000;
        std::filesystem::path observedDirectoryPath;
        uint64_t observedDirectorySnapshotHash = 0;
        bool observedDirectorySnapshotValid = false;

        void beginAssetRename(const std::filesystem::path& path, std::filesystem::path& selectedAssetPath);
        void commitAssetRename(std::filesystem::path& selectedAssetPath);
        void cancelAssetRename();
        void ensureAssetChangeListenerRegistered();
        void handleAssetChanged(const std::string& cacheKey);
        void pollExternalDirectoryChanges(std::filesystem::path& selectedAssetPath);
        void resetExternalDirectoryTracking();
        std::filesystem::path makeUniquePathWithSuffix(const std::filesystem::path& desiredPath, const std::string& suffix) const;
        bool createMaterialWithLinkedAsset(const std::filesystem::path& materialPath, std::filesystem::path& outMaterialAssetPath, std::string* outError = nullptr) const;
};

#endif // WORKSPACE_PANEL_H
