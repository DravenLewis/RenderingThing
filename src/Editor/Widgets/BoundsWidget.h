#ifndef WIDGETS_BOUNDS_WIDGET_H
#define WIDGETS_BOUNDS_WIDGET_H

#include <imgui.h>
#include <vector>

#include "Foundation/Math/Math3D.h"
#include "Platform/Input/InputManager.h"
#include "Rendering/Core/View.h"
#include "Scene/Camera.h"
#include "Editor/Widgets/TransformWidget.h"

struct BoundsComponent;

class BoundsWidget {
    public:
        BoundsWidget() = default;

        void reset();
        bool isDragging() const { return dragging; }

        bool update(View* view,
                    InputManager* input,
                    PCamera camera,
                    const TransformWidget::Viewport& viewport,
                    const Math3D::Mat4& worldMatrix,
                    BoundsComponent& bounds,
                    bool allowInput,
                    bool lmbPressed,
                    bool lmbDown,
                    bool lmbReleased);

        void draw(ImDrawList* drawList,
                  View* view,
                  PCamera camera,
                  const TransformWidget::Viewport& viewport,
                  const Math3D::Mat4& worldMatrix,
                  const BoundsComponent& bounds) const;

    private:
        enum class Handle {
            None = 0,
            OffsetX,
            OffsetY,
            OffsetZ,
            BoxSizeX,
            BoxSizeY,
            BoxSizeZ,
            SphereRadiusX,
            SphereRadiusY,
            SphereRadiusZ,
            CapsuleRadiusX,
            CapsuleRadiusZ,
            CapsuleHeight
        };

        struct HandleCandidate {
            Handle handle = Handle::None;
            Math3D::Vec3 localPos = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 axisLocal = Math3D::Vec3(1.0f, 0.0f, 0.0f);
        };

        bool dragging = false;
        Handle activeHandle = Handle::None;
        Handle hoverHandle = Handle::None;

        Math3D::Vec2 startMouse = Math3D::Vec2(0.0f, 0.0f);
        Math3D::Vec2 dragScreenAxis = Math3D::Vec2(1.0f, 0.0f);
        float dragPixelsPerUnit = 1.0f;
        Math3D::Vec3 startOffset = Math3D::Vec3(0.0f, 0.0f, 0.0f);
        Math3D::Vec3 startSize = Math3D::Vec3(1.0f, 1.0f, 1.0f);
        float startRadius = 0.5f;
        float startHeight = 1.0f;

        float pickRadiusPx = 10.0f;
        float handleRadiusPx = 6.0f;

        void buildHandles(const BoundsComponent& bounds, std::vector<HandleCandidate>& out) const;
        bool isOffsetHandle(Handle handle) const;
};

#endif // WIDGETS_BOUNDS_WIDGET_H
