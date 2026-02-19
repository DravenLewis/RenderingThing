#ifndef WIDGETS_CAMERA_WIDGET_H
#define WIDGETS_CAMERA_WIDGET_H

#include <imgui.h>
#include "Math.h"
#include "InputManager.h"
#include "View.h"
#include "Camera.h"
#include "Texture.h"
#include "Widgets/TransformWidget.h"

class CameraWidget {
    public:
        enum class Handle {
            None = 0,
            Fov,
            FarPlane
        };

        CameraWidget() = default;

        void setIcon(PTexture tex){ icon = tex; }
        void reset();

        bool update(View* view,
                    InputManager* input,
                    PCamera viewportCamera,
                    const TransformWidget::Viewport& viewport,
                    const Math3D::Vec3& worldPos,
                    const Math3D::Vec3& worldForward,
                    CameraSettings& settings,
                    bool allowInput,
                    bool lmbPressed,
                    bool lmbDown,
                    bool lmbReleased);

        void draw(ImDrawList* drawList,
                  View* view,
                  PCamera viewportCamera,
                  const TransformWidget::Viewport& viewport,
                  const Math3D::Vec3& worldPos,
                  const Math3D::Vec3& worldForward,
                  const CameraSettings& settings) const;

    private:
        PTexture icon;
        bool dragging = false;
        Handle activeHandle = Handle::None;
        Math3D::Vec2 startMouse = Math3D::Vec2(0,0);
        Math3D::Vec2 screenAxis = Math3D::Vec2(1,0);
        float pixelsPerUnit = 1.0f;
        float startFov = 45.0f;
        float startFar = 100.0f;
        float startRadius = 1.0f;
};

#endif // WIDGETS_CAMERA_WIDGET_H
