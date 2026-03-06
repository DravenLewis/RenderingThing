#ifndef PROPERTIES_PANEL_H
#define PROPERTIES_PANEL_H

#include <filesystem>
#include <functional>
#include <string>

#include "Scene/Scene.h"
#include "neoecs.hpp"
#include "Editor/Widgets/FilePreviewWidget.h"

class PropertiesPanel {
    public:
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

    private:
        FilePreviewWidget filePreviewWidget;
        bool showHiddenComponents = false;
};

#endif // PROPERTIES_PANEL_H
