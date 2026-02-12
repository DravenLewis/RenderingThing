#ifndef WIDGETS_TRANSFORM_WIDGET_H
#define WIDGETS_TRANSFORM_WIDGET_H

#include <imgui.h>
#include "Math.h"
#include "InputManager.h"
#include "View.h"
#include "Camera.h"

class TransformWidget {
    public:
        enum class Mode {
            Translate = 0,
            Rotate,
            Scale,
            Combined
        };

        enum class Axis {
            None = 0,
            X,
            Y,
            Z
        };

        enum class Handle {
            None = 0,
            Translate,
            Rotate,
            Scale,
            UniformScale
        };

        struct Viewport {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            bool valid = false;
        };

        TransformWidget();

        void setMode(Mode newMode);
        Mode getMode() const { return mode; }
        bool isDragging() const { return dragging; }

        void reset();

        bool update(View* view,
                    InputManager* input,
                    PCamera camera,
                    const Viewport& viewport,
                    const Math3D::Vec3& worldPos,
                    Math3D::Transform& transform,
                    bool allowInput,
                    bool lmbPressed,
                    bool lmbDown,
                    bool lmbReleased);

        void draw(ImDrawList* drawList,
                  View* view,
                  PCamera camera,
                  const Viewport& viewport,
                  const Math3D::Vec3& worldPos,
                  const Math3D::Transform& transform,
                  bool viewportHovered) const;

    private:
        Mode mode = Mode::Combined;
        Axis activeAxis = Axis::None;
        Axis hoverAxis = Axis::None;
        Handle activeHandle = Handle::None;
        Handle hoverHandle = Handle::None;
        bool dragging = false;

        float gizmoSize = 1.0f;
        float translateDotOffset = 1.1f;
        float scaleSquareOffset = 0.85f;
        float translateDotRadius = 5.5f;
        float scaleSquareHalf = 4.5f;
        float uniformSquareHalf = 10.0f;
        float centerPickRadius = 14.0f;
        float centerIgnoreRadius = 12.0f;
        float lineThickness = 3.0f;
        float lineThicknessActive = 4.5f;

        Math3D::Transform startTransform;
        Math3D::Vec3 startWorldPos = Math3D::Vec3(0,0,0);
        Math3D::Vec2 startMouse = Math3D::Vec2(0,0);
        float startAxisT = 0.0f;
        Math3D::Vec3 dragAxisWorld = Math3D::Vec3(1,0,0);
        Math3D::Vec3 dragAxisLocal = Math3D::Vec3(1,0,0);
        Math3D::Vec3 startPlaneVec = Math3D::Vec3(1,0,0);

        struct HoverResult {
            Handle handle = Handle::None;
            Axis axis = Axis::None;
        };

        HoverResult pickHandleFromMouse(View* view,
                                        InputManager* input,
                                        PCamera camera,
                                        const Viewport& viewport,
                                        const Math3D::Vec3& worldPos,
                                        const Math3D::Transform& transform) const;

        float computeGizmoSize(PCamera camera, const Math3D::Vec3& worldPos) const;
        Math3D::Vec3 axisWorldFromLocal(const Math3D::Transform& transform, Axis axis) const;
        Math3D::Vec3 axisLocalFromEnum(Axis axis) const;
};

#endif // WIDGETS_TRANSFORM_WIDGET_H
