/**
 * @file src/Editor/Widgets/LightWidget.h
 * @brief Declarations for LightWidget.
 */

#ifndef WIDGETS_LIGHT_WIDGET_H
#define WIDGETS_LIGHT_WIDGET_H

#include <imgui.h>
#include "Foundation/Math/Math3D.h"
#include "Platform/Input/InputManager.h"
#include "Rendering/Core/View.h"
#include "Scene/Camera.h"
#include "Rendering/Lighting/Light.h"
#include "Rendering/Textures/Texture.h"
#include "Editor/Widgets/TransformWidget.h"

/// @brief Represents the LightWidget type.
class LightWidget {
    public:
        /// @brief Enumerates values for Handle.
        enum class Handle {
            None = 0,
            PointRadius,
            SpotAngle,
            SpotRange
        };

        /// @brief Holds data for Icons.
        struct Icons {
            PTexture point;
            PTexture spot;
            PTexture directional;
        };

        /**
         * @brief Constructs a new LightWidget instance.
         */
        LightWidget() = default;

        /**
         * @brief Sets the icons.
         * @param newIcons Value for new icons.
         */
        void setIcons(const Icons& newIcons){ icons = newIcons; }
        void reset();

        bool update(View* view,
                    InputManager* input,
                    PCamera camera,
                    const TransformWidget::Viewport& viewport,
                    const Math3D::Vec3& worldPos,
                    const Math3D::Vec3& worldForward,
                    Light& light,
                    bool syncTransform,
                    bool syncDirection,
                    bool allowInput,
                    bool lmbPressed,
                    bool lmbDown,
                    bool lmbReleased);

        void draw(ImDrawList* drawList,
                  View* view,
                  PCamera camera,
                  const TransformWidget::Viewport& viewport,
                  const Math3D::Vec3& worldPos,
                  const Math3D::Vec3& worldForward,
                  const Light& light,
                  bool syncTransform,
                  bool syncDirection) const;

    private:
        Icons icons{};
        bool dragging = false;
        Handle activeHandle = Handle::None;
        Math3D::Vec2 startMouse = Math3D::Vec2(0,0);
        Math3D::Vec2 screenAxis = Math3D::Vec2(1,0);
        float startValue = 0.0f;
        float startRange = 1.0f;
        float pixelsPerUnit = 1.0f;

        bool isOnScreen(View* view,
                        PCamera camera,
                        const TransformWidget::Viewport& viewport,
                        const Math3D::Vec3& worldPos) const;

        void drawBillboard(ImDrawList* drawList,
                           View* view,
                           PCamera camera,
                           const TransformWidget::Viewport& viewport,
                           const Math3D::Vec3& worldPos,
                           PTexture texture,
                           float sizePx,
                           ImU32 tint) const;
};

#endif // WIDGETS_LIGHT_WIDGET_H
