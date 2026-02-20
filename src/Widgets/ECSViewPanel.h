#ifndef ECS_VIEW_PANEL_H
#define ECS_VIEW_PANEL_H

#include <functional>
#include <string>

#include "Scene.h"
#include "neoecs.hpp"

class ECSViewPanel {
    public:
        void draw(
            float x,
            float y,
            float w,
            float h,
            PScene targetScene,
            const std::string& selectedEntityId,
            const std::function<void(const std::string&)>& onSelectEntity
        );

    private:
        bool showHiddenModelPartsInTree = false;

        void drawEntityTree(
            NeoECS::ECSEntity* entity,
            PScene targetScene,
            const std::string& selectedEntityId,
            const std::function<void(const std::string&)>& onSelectEntity
        );
};

#endif // ECS_VIEW_PANEL_H
