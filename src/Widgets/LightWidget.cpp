#include "Widgets/LightWidget.h"

#include <cmath>
#include <vector>

namespace {
    constexpr ImU32 kLightColor = IM_COL32(255, 214, 64, 255);
    constexpr ImU32 kLightOutline = IM_COL32(255, 214, 64, 220);
    constexpr float kHandleRadius = 6.0f;
    constexpr float kPickRadius = 10.0f;
    constexpr float kIconSizePx = 64.0f;

    float screenDistance(const ImVec2& a, const ImVec2& b){
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    ImU32 toImColor(const Math3D::Vec4& c){
        ImVec4 col(c.x, c.y, c.z, c.w);
        return ImGui::ColorConvertFloat4ToU32(col);
    }

    void buildRingPoints(std::vector<Math3D::Vec3>& out,
                         const Math3D::Vec3& center,
                         const Math3D::Vec3& axis,
                         float radius){
        out.clear();
        Math3D::Vec3 normal = axis.normalize();
        Math3D::Vec3 basis = (std::fabs(normal.y) < 0.9f) ? Math3D::Vec3(0,1,0) : Math3D::Vec3(1,0,0);
        Math3D::Vec3 u = Math3D::Vec3::cross(normal, basis).normalize();
        Math3D::Vec3 v = Math3D::Vec3::cross(normal, u).normalize();

        constexpr int segments = 48;
        out.reserve(segments + 1);
        for(int i = 0; i <= segments; ++i){
            float t = (float)i / (float)segments;
            float angle = t * 2.0f * Math3D::PI;
            float c = Math3D::Cos(angle);
            float s = Math3D::Sin(angle);
            Math3D::Vec3 point = center + (u * c + v * s) * radius;
            out.push_back(point);
        }
    }
}

void LightWidget::reset(){
    dragging = false;
    activeHandle = Handle::None;
}

bool LightWidget::isOnScreen(View* view,
                             PCamera camera,
                             const TransformWidget::Viewport& viewport,
                             const Math3D::Vec3& worldPos) const{
    if(!view || !camera || !viewport.valid){
        return false;
    }
    Math3D::Vec3 screen = view->worldToScreen(camera, worldPos, viewport.x, viewport.y, viewport.w, viewport.h);
    if(screen.z < 0.0f || screen.z > 1.0f){
        return false;
    }
    if(screen.x < viewport.x || screen.x > (viewport.x + viewport.w)){
        return false;
    }
    if(screen.y < viewport.y || screen.y > (viewport.y + viewport.h)){
        return false;
    }
    return true;
}

void LightWidget::drawBillboard(ImDrawList* drawList,
                                View* view,
                                PCamera camera,
                                const TransformWidget::Viewport& viewport,
                                const Math3D::Vec3& worldPos,
                                PTexture texture,
                                float sizePx,
                                ImU32 tint) const{
    if(!drawList || !view || !camera || !texture){
        return;
    }
    Math3D::Vec3 screen = view->worldToScreen(camera, worldPos, viewport.x, viewport.y, viewport.w, viewport.h);
    if(!isOnScreen(view, camera, viewport, worldPos)){
        return;
    }
    float half = sizePx * 0.5f;
    ImVec2 pmin(screen.x - half, screen.y - half);
    ImVec2 pmax(screen.x + half, screen.y + half);
    ImTextureID texId = (ImTextureID)(intptr_t)texture->getID();
    drawList->AddImage(texId, pmin, pmax, ImVec2(0, 0), ImVec2(1, 1), tint);
}

bool LightWidget::update(View* view,
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
                         bool lmbReleased){
    if(!view || !input || !camera || !viewport.valid){
        reset();
        return false;
    }

    Math3D::Vec3 lightPos = syncTransform ? worldPos : light.position;
    Math3D::Vec2 mouse = input->getMousePosition();
    ImVec2 mousePt(mouse.x, mouse.y);

    auto camTx = camera->transform();
    Math3D::Vec3 camRight = camTx.right().normalize();

    Handle hoverHandle = Handle::None;
    ImVec2 handleScreenPos(0,0);
    Math3D::Vec2 axisDir(1,0);
    float axisPixelsPerUnit = 1.0f;

    if(light.type == LightType::POINT){
        float range = Math3D::Max(0.1f, light.range);
        Math3D::Vec3 centerScreen = view->worldToScreen(camera, lightPos, viewport.x, viewport.y, viewport.w, viewport.h);
        Math3D::Vec3 handleWorld = lightPos + camRight * range;
        Math3D::Vec3 handleScreen = view->worldToScreen(camera, handleWorld, viewport.x, viewport.y, viewport.w, viewport.h);
        ImVec2 centerPt(centerScreen.x, centerScreen.y);
        handleScreenPos = ImVec2(handleScreen.x, handleScreen.y);
        Math3D::Vec2 axis2(handleScreen.x - centerScreen.x, handleScreen.y - centerScreen.y);
        float axisLen = axis2.length();
        if(axisLen > Math3D::EPSILON){
            axisDir = Math3D::Vec2(axis2.x / axisLen, axis2.y / axisLen);
        }
        Math3D::Vec3 unitScreen = view->worldToScreen(camera, lightPos + camRight, viewport.x, viewport.y, viewport.w, viewport.h);
        axisPixelsPerUnit = Math3D::Vec2(unitScreen.x - centerScreen.x, unitScreen.y - centerScreen.y).length();
        if(allowInput && isOnScreen(view, camera, viewport, lightPos)){
            if(screenDistance(mousePt, handleScreenPos) <= kPickRadius){
                hoverHandle = Handle::PointRadius;
            }
        }
    }else if(light.type == LightType::SPOT){
        Math3D::Vec3 dir = syncDirection ? Math3D::Vec3(0,0,1) : light.direction;
        if(syncDirection){
            dir = worldForward;
        }
        if(dir.length() > Math3D::EPSILON){
            dir = dir.normalize();
        }

        Math3D::Vec3 axisUp(0,1,0);
        if(std::fabs(Math3D::Vec3::dot(dir, axisUp)) > 0.9f){
            axisUp = Math3D::Vec3(1,0,0);
        }
        Math3D::Vec3 axisRight = Math3D::Vec3::cross(dir, axisUp).normalize();
        float range = Math3D::Max(0.1f, light.range);
        float angle = Math3D::Clamp(light.spotAngle, 1.0f, 170.0f);
        float radius = range * std::tan(Math3D::PI * (angle * 0.5f) / 180.0f);
        Math3D::Vec3 axisPoint = lightPos + dir * range;

        // Range handle at cone tip
        Math3D::Vec3 axisScreen = view->worldToScreen(camera, axisPoint, viewport.x, viewport.y, viewport.w, viewport.h);
        Math3D::Vec3 centerScreen = view->worldToScreen(camera, lightPos, viewport.x, viewport.y, viewport.w, viewport.h);
        ImVec2 axisPt(axisScreen.x, axisScreen.y);
        ImVec2 centerPt(centerScreen.x, centerScreen.y);
        Math3D::Vec2 axisVec(axisScreen.x - centerScreen.x, axisScreen.y - centerScreen.y);
        float axisLen = axisVec.length();
        Math3D::Vec2 axisDirRange(1,0);
        if(axisLen > Math3D::EPSILON){
            axisDirRange = Math3D::Vec2(axisVec.x / axisLen, axisVec.y / axisLen);
        }
        Math3D::Vec3 unitScreen = view->worldToScreen(camera, lightPos + dir, viewport.x, viewport.y, viewport.w, viewport.h);
        float rangePixelsPerUnit = Math3D::Vec2(unitScreen.x - centerScreen.x, unitScreen.y - centerScreen.y).length();

        // Angle handle on cone edge
        Math3D::Vec3 handleWorld = axisPoint + axisRight * radius;
        Math3D::Vec3 handleScreen = view->worldToScreen(camera, handleWorld, viewport.x, viewport.y, viewport.w, viewport.h);
        ImVec2 anglePt(handleScreen.x, handleScreen.y);
        Math3D::Vec2 angleAxis(handleScreen.x - axisScreen.x, handleScreen.y - axisScreen.y);
        float angleLen = angleAxis.length();
        Math3D::Vec2 angleDir(1,0);
        if(angleLen > Math3D::EPSILON){
            angleDir = Math3D::Vec2(angleAxis.x / angleLen, angleAxis.y / angleLen);
        }
        Math3D::Vec3 unitAngleScreen = view->worldToScreen(camera, axisPoint + axisRight, viewport.x, viewport.y, viewport.w, viewport.h);
        float anglePixelsPerUnit = Math3D::Vec2(unitAngleScreen.x - axisScreen.x, unitAngleScreen.y - axisScreen.y).length();

        if(allowInput && isOnScreen(view, camera, viewport, axisPoint)){
            float distRange = screenDistance(mousePt, axisPt);
            float distAngle = screenDistance(mousePt, anglePt);
            if(distAngle <= kPickRadius && distAngle <= distRange){
                hoverHandle = Handle::SpotAngle;
                handleScreenPos = anglePt;
                axisDir = angleDir;
                axisPixelsPerUnit = anglePixelsPerUnit;
            }else if(distRange <= kPickRadius){
                hoverHandle = Handle::SpotRange;
                handleScreenPos = axisPt;
                axisDir = axisDirRange;
                axisPixelsPerUnit = rangePixelsPerUnit;
            }
        }
    }

    if(lmbPressed && allowInput && hoverHandle != Handle::None){
        dragging = true;
        activeHandle = hoverHandle;
        startMouse = mouse;
        screenAxis = axisDir;
        pixelsPerUnit = Math3D::Max(1.0f, axisPixelsPerUnit);
        if(activeHandle == Handle::PointRadius){
            startValue = Math3D::Max(0.1f, light.range);
        }else if(activeHandle == Handle::SpotAngle){
            startValue = Math3D::Clamp(light.spotAngle, 1.0f, 170.0f);
            startRange = Math3D::Max(0.1f, light.range);
        }else if(activeHandle == Handle::SpotRange){
            startRange = Math3D::Max(0.1f, light.range);
        }
        return true;
    }

    if(dragging){
        if(lmbReleased || !lmbDown){
            reset();
            return true;
        }

        Math3D::Vec2 delta(mouse.x - startMouse.x, mouse.y - startMouse.y);
        float axisDeltaPx = (delta.x * screenAxis.x) + (delta.y * screenAxis.y);
        float axisDeltaWorld = axisDeltaPx / pixelsPerUnit;

        if(activeHandle == Handle::PointRadius){
            light.range = Math3D::Max(0.1f, startValue + axisDeltaWorld);
        }else if(activeHandle == Handle::SpotAngle){
            float radius = startRange * std::tan(Math3D::PI * (startValue * 0.5f) / 180.0f);
            radius = Math3D::Max(0.01f, radius + axisDeltaWorld);
            float newAngle = 2.0f * std::atan(radius / startRange) * (180.0f / Math3D::PI);
            light.spotAngle = Math3D::Clamp(newAngle, 1.0f, 170.0f);
        }else if(activeHandle == Handle::SpotRange){
            light.range = Math3D::Max(0.1f, startRange + axisDeltaWorld);
        }
        return true;
    }

    return hoverHandle != Handle::None;
}

void LightWidget::draw(ImDrawList* drawList,
                       View* view,
                       PCamera camera,
                       const TransformWidget::Viewport& viewport,
                       const Math3D::Vec3& worldPos,
                       const Math3D::Vec3& worldForward,
                       const Light& light,
                       bool syncTransform,
                       bool syncDirection) const{
    if(!drawList || !view || !camera || !viewport.valid){
        return;
    }

    Math3D::Vec3 lightPos = syncTransform ? worldPos : light.position;
    Math3D::Vec3 centerScreen = view->worldToScreen(camera, lightPos, viewport.x, viewport.y, viewport.w, viewport.h);
    if(!isOnScreen(view, camera, viewport, lightPos)){
        return;
    }

    ImU32 color = kLightColor;
    ImU32 outline = kLightOutline;
    ImU32 iconTint = toImColor(light.color);

    if(light.type == LightType::POINT){
        float range = Math3D::Max(0.1f, light.range);
        std::vector<Math3D::Vec3> ringPoints;
        for(const Math3D::Vec3& axis : {Math3D::Vec3(1,0,0), Math3D::Vec3(0,1,0), Math3D::Vec3(0,0,1)}){
            buildRingPoints(ringPoints, lightPos, axis, range);
            ImVec2 prev;
            bool hasPrev = false;
            for(const auto& wp : ringPoints){
                Math3D::Vec3 sp = view->worldToScreen(camera, wp, viewport.x, viewport.y, viewport.w, viewport.h);
                ImVec2 pt(sp.x, sp.y);
                if(hasPrev){
                    drawList->AddLine(prev, pt, outline, 2.0f);
                }
                prev = pt;
                hasPrev = true;
            }
        }
        auto camTx = camera->transform();
        Math3D::Vec3 camRight = camTx.right().normalize();
        Math3D::Vec3 handleWorld = lightPos + camRight * range;
        Math3D::Vec3 handleScreen = view->worldToScreen(camera, handleWorld, viewport.x, viewport.y, viewport.w, viewport.h);
        ImVec2 handlePt(handleScreen.x, handleScreen.y);
        drawList->AddCircleFilled(handlePt, kHandleRadius, color);
        drawBillboard(drawList, view, camera, viewport, lightPos, icons.point, kIconSizePx, iconTint);
    }else if(light.type == LightType::DIRECTIONAL){
        Math3D::Vec3 dir = syncDirection ? Math3D::Vec3(0,0,1) : light.direction;
        if(syncDirection){
            dir = worldForward;
        }
        if(dir.length() > Math3D::EPSILON){
            dir = dir.normalize();
        }
        float length = 2.0f;
        Math3D::Vec3 endWorld = lightPos + dir * length;
        Math3D::Vec3 endScreen = view->worldToScreen(camera, endWorld, viewport.x, viewport.y, viewport.w, viewport.h);
        ImVec2 startPt(centerScreen.x, centerScreen.y);
        ImVec2 endPt(endScreen.x, endScreen.y);
        drawList->AddLine(startPt, endPt, color, 2.5f);
        ImVec2 dir2(endPt.x - startPt.x, endPt.y - startPt.y);
        float len = std::sqrt(dir2.x * dir2.x + dir2.y * dir2.y);
        if(len > 1.0f){
            ImVec2 ndir(dir2.x / len, dir2.y / len);
            ImVec2 left(-ndir.y, ndir.x);
            ImVec2 tip = endPt;
            ImVec2 p1(tip.x - ndir.x * 10.0f + left.x * 5.0f, tip.y - ndir.y * 10.0f + left.y * 5.0f);
            ImVec2 p2(tip.x - ndir.x * 10.0f - left.x * 5.0f, tip.y - ndir.y * 10.0f - left.y * 5.0f);
            drawList->AddTriangleFilled(tip, p1, p2, color);
        }
        drawBillboard(drawList, view, camera, viewport, lightPos, icons.directional, kIconSizePx, iconTint);
    }else if(light.type == LightType::SPOT){
        Math3D::Vec3 dir = syncDirection ? Math3D::Vec3(0,0,1) : light.direction;
        if(syncDirection){
            dir = worldForward;
        }
        if(dir.length() > Math3D::EPSILON){
            dir = dir.normalize();
        }
        Math3D::Vec3 axisUp(0,1,0);
        if(std::fabs(Math3D::Vec3::dot(dir, axisUp)) > 0.9f){
            axisUp = Math3D::Vec3(1,0,0);
        }
        Math3D::Vec3 axisRight = Math3D::Vec3::cross(dir, axisUp).normalize();
        float range = Math3D::Max(0.1f, light.range);
        float angle = Math3D::Clamp(light.spotAngle, 1.0f, 170.0f);
        float radius = range * std::tan(Math3D::PI * (angle * 0.5f) / 180.0f);
        Math3D::Vec3 axisPoint = lightPos + dir * range;
        Math3D::Vec3 edgeRight = axisPoint + axisRight * radius;
        Math3D::Vec3 edgeLeft = axisPoint - axisRight * radius;
        Math3D::Vec3 axisScreen = view->worldToScreen(camera, axisPoint, viewport.x, viewport.y, viewport.w, viewport.h);
        Math3D::Vec3 rightScreen = view->worldToScreen(camera, edgeRight, viewport.x, viewport.y, viewport.w, viewport.h);
        Math3D::Vec3 leftScreen = view->worldToScreen(camera, edgeLeft, viewport.x, viewport.y, viewport.w, viewport.h);
        ImVec2 origin(centerScreen.x, centerScreen.y);
        ImVec2 rightPt(rightScreen.x, rightScreen.y);
        ImVec2 leftPt(leftScreen.x, leftScreen.y);
        ImVec2 axisPt(axisScreen.x, axisScreen.y);
        // Cone edges + center line to show length
        drawList->AddLine(origin, rightPt, outline, 2.0f);
        drawList->AddLine(origin, leftPt, outline, 2.0f);
        drawList->AddLine(origin, axisPt, outline, 2.0f);
        float radiusPx = screenDistance(axisPt, rightPt);
        drawList->AddCircle(axisPt, radiusPx, outline, 32, 2.0f);
        drawList->AddCircleFilled(rightPt, kHandleRadius, color);
        drawList->AddCircleFilled(axisPt, kHandleRadius, color);
        drawBillboard(drawList, view, camera, viewport, lightPos, icons.spot, kIconSizePx, iconTint);
    }
}
