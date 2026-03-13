/**
 * @file src/Editor/Widgets/PropertiesPanel.h
 * @brief Declarations for PropertiesPanel.
 */

#ifndef PROPERTIES_PANEL_H
#define PROPERTIES_PANEL_H

#include <filesystem>
#include <functional>
#include <string>

#include "Scene/Scene.h"
#include "neoecs.hpp"
#include "Editor/Widgets/FilePreviewWidget.h"

/// @brief Represents the PropertiesPanel type.
class PropertiesPanel {
    public:
        /// @brief Holds transient properties-panel state.
        struct State {
            bool showHiddenComponents = false;
        };

        /**
         * @brief Draws this object.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param w Value for w.
         * @param h Value for h.
         * @param targetScene Value for target scene.
         * @param assetRoot Value for asset root.
         * @param selectedAssetPath Filesystem path for selected asset path.
         * @param selectedEntityId Identifier or index value.
         * @param findEntityById Identifier or index value.
         */
        void draw(
            float x,
            float y,
            float w,
            float h,
            PScene targetScene,
            const std::filesystem::path& assetRoot,
            std::filesystem::path& selectedAssetPath,
            const std::string& selectedEntityId,
            const std::function<NeoECS::ECSEntity*(const std::string&)>& findEntityById
        );

        /**
         * @brief Captures the current transient panel state.
         * @return Result of this operation.
         */
        State captureState() const;
        /**
         * @brief Applies transient panel state.
         * @param state Value for state.
         */
        void applyState(const State& state);
        /**
         * @brief Checks whether an edit interaction is active inside the panel.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool isInteractionActive() const { return interactionActive; }

    private:
        FilePreviewWidget filePreviewWidget;
        bool showHiddenComponents = false;
        bool interactionActive = false;
};

#endif // PROPERTIES_PANEL_H
