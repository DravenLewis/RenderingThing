#ifndef WORKSPACE_PANEL_H
#define WORKSPACE_PANEL_H

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "imgui.h"

class WorkspacePanel {
    public:
        void setAssetRoot(const std::filesystem::path& rootPath);
        void draw(float x, float y, float w, float h, std::filesystem::path& selectedAssetPath);

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
        std::string logBuffer;
        std::vector<std::string> logLines;
        std::vector<ImVec4> logColors;
        bool showAssetPickerWindow = false;
        std::unordered_set<std::string> expandedRelatedAssets;

        void beginAssetRename(const std::filesystem::path& path, std::filesystem::path& selectedAssetPath);
        void commitAssetRename(std::filesystem::path& selectedAssetPath);
        void cancelAssetRename();
        std::filesystem::path makeUniquePathWithSuffix(const std::filesystem::path& desiredPath, const std::string& suffix) const;
        bool createMaterialWithLinkedAsset(const std::filesystem::path& materialPath, std::filesystem::path& outMaterialAssetPath, std::string* outError = nullptr) const;
};

#endif // WORKSPACE_PANEL_H
