/**
 * @file src/Editor/Widgets/ECSViewPanel.h
 * @brief Declarations for ECSViewPanel.
 */

#ifndef ECS_VIEW_PANEL_H
#define ECS_VIEW_PANEL_H

#include <functional>
#include <filesystem>
#include <string>
#include <vector>

#include "Scene/Scene.h"
#include "neoecs.hpp"

/// @brief Represents the ECSViewPanel type.
class ECSViewPanel {
    public:
        static constexpr const char* EntityDragPayloadType = "ECS_ENTITY_DND";
        using InstantiatePrefabAtEntityFn = std::function<bool(const std::filesystem::path& prefabPath,
                                                               const std::string& parentEntityId,
                                                               std::string* outError)>;
        /// @brief Holds editor change callbacks for scene mutations.
        struct ChangeCallbacks {
            std::function<void(const std::string&)> onBeforeDeleteEntity;
            std::function<void(const std::string&)> onAfterDeleteEntity;
            std::function<void(const std::string&)> onEntityCreated;
            std::function<void(const std::string&, const std::string&, const std::string&)> onEntityRenamed;
            std::function<void(const std::string&, const std::string&, const std::string&)> onEntityReparented;
        };

        /**
         * @brief Draws this object.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param w Value for w.
         * @param h Value for h.
         * @param targetScene Value for target scene.
         * @param selectedEntityId Identifier or index value.
         * @param onSelectEntity Callback for on select entity.
         * @param onCreatePrefabForEntity Callback for on create prefab for entity.
         * @param onInstantiatePrefabAtEntity Callback for on instantiate prefab at entity.
         * @param changeCallbacks Callbacks for editor-scene undo/redo tracking.
         */
        void draw(
            float x,
            float y,
            float w,
            float h,
            PScene targetScene,
            const std::string& selectedEntityId,
            const std::function<void(const std::string&)>& onSelectEntity,
            const std::function<void(const std::string&)>& onCreatePrefabForEntity = std::function<void(const std::string&)>(),
            const InstantiatePrefabAtEntityFn& onInstantiatePrefabAtEntity = InstantiatePrefabAtEntityFn(),
            const ChangeCallbacks& changeCallbacks = ChangeCallbacks()
        );

    private:
        static constexpr size_t kEntityRenameBufferSize = 256;

        /// @brief Enumerates values for PendingActionKind.
        enum class PendingActionKind {
            CreateEmpty,
            CreateLight,
            CreateCamera,
            CreateEnvironment,
            DeleteEntity,
            ReparentToEntity,
            ReparentToRoot
        };

        /// @brief Holds data for PendingAction.
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

        /**
         * @brief Begins entity rename.
         * @param targetScene Value for target scene.
         * @param entityId Identifier or index value.
         */
        void beginEntityRename(PScene targetScene, const std::string& entityId);
        /**
         * @brief Commits entity rename.
         * @param targetScene Value for target scene.
         * @param changeCallbacks Callbacks used to report successful renames.
         */
        void commitEntityRename(PScene targetScene, const ChangeCallbacks& changeCallbacks);
        /**
         * @brief Checks whether cel entity rename.
         */
        void cancelEntityRename();
        /**
         * @brief Draws rename popup.
         * @param targetScene Value for target scene.
         * @param changeCallbacks Callbacks used to report successful renames.
         */
        void drawRenamePopup(PScene targetScene, const ChangeCallbacks& changeCallbacks);
        /**
         * @brief Draws entity tree.
         * @param entity Value for entity.
         * @param targetScene Value for target scene.
         * @param selectedEntityId Identifier or index value.
         * @param onSelectEntity Callback for on select entity.
         * @param onCreatePrefabForEntity Callback for on create prefab for entity.
         * @param onInstantiatePrefabAtEntity Callback for on instantiate prefab at entity.
         * @param changeCallbacks Callbacks for editor-scene undo/redo tracking.
         */
        void drawEntityTree(
            NeoECS::ECSEntity* entity,
            PScene targetScene,
            const std::string& selectedEntityId,
            const std::function<void(const std::string&)>& onSelectEntity,
            const std::function<void(const std::string&)>& onCreatePrefabForEntity,
            const InstantiatePrefabAtEntityFn& onInstantiatePrefabAtEntity,
            const ChangeCallbacks& changeCallbacks
        );
        /**
         * @brief Applies pending actions.
         * @param targetScene Value for target scene.
         * @param selectedEntityId Identifier or index value.
         * @param onSelectEntity Callback for on select entity.
         * @param changeCallbacks Callbacks for editor-scene undo/redo tracking.
         */
        void applyPendingActions(
            PScene targetScene,
            const std::string& selectedEntityId,
            const std::function<void(const std::string&)>& onSelectEntity,
            const ChangeCallbacks& changeCallbacks
        );
};

#endif // ECS_VIEW_PANEL_H
