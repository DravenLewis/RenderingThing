#ifndef ECS_VIEW_PANEL_H
#define ECS_VIEW_PANEL_H

#include <functional>
#include <filesystem>
#include <string>
#include <vector>

#include "Scene/Scene.h"
#include "neoecs.hpp"

class ECSViewPanel {
    public:
        static constexpr const char* EntityDragPayloadType = "ECS_ENTITY_DND";
        using InstantiatePrefabAtEntityFn = std::function<bool(const std::filesystem::path& prefabPath,
                                                               const std::string& parentEntityId,
                                                               std::string* outError)>;

        void draw(
            float x,
            float y,
            float w,
            float h,
            PScene targetScene,
            const std::string& selectedEntityId,
            const std::function<void(const std::string&)>& onSelectEntity,
            const std::function<void(const std::string&)>& onCreatePrefabForEntity = std::function<void(const std::string&)>(),
            const InstantiatePrefabAtEntityFn& onInstantiatePrefabAtEntity = InstantiatePrefabAtEntityFn()
        );

    private:
        static constexpr size_t kEntityRenameBufferSize = 256;

        enum class PendingActionKind {
            CreateEmpty,
            CreateLight,
            CreateCamera,
            DeleteEntity,
            ReparentToEntity,
            ReparentToRoot
        };

        struct PendingAction {
            PendingActionKind kind = PendingActionKind::CreateEmpty;
            std::string entityId;
            std::string targetEntityId;
        };

        bool showHiddenModelPartsInTree = false;
        bool entityRenameActive = false;
        bool entityRenameFocus = false;
        bool entityRenamePopupPendingOpen = false;
        std::string entityRenameId;
        char entityRenameBuffer[kEntityRenameBufferSize] = {};
        std::vector<PendingAction> pendingActions;

        void beginEntityRename(PScene targetScene, const std::string& entityId);
        void commitEntityRename(PScene targetScene);
        void cancelEntityRename();
        void drawRenamePopup(PScene targetScene);
        void drawEntityTree(
            NeoECS::ECSEntity* entity,
            PScene targetScene,
            const std::string& selectedEntityId,
            const std::function<void(const std::string&)>& onSelectEntity,
            const std::function<void(const std::string&)>& onCreatePrefabForEntity,
            const InstantiatePrefabAtEntityFn& onInstantiatePrefabAtEntity
        );
        void applyPendingActions(
            PScene targetScene,
            const std::string& selectedEntityId,
            const std::function<void(const std::string&)>& onSelectEntity
        );
};

#endif // ECS_VIEW_PANEL_H
