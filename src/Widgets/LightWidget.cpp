#include "Widgets/LightWidget.h"

#include <cmath>
#include <vector>

namespace {
    constexpr ImU32 kLightColor = IM_COL32(255, 214, 64, 255);
    constexpr ImU32 kLightOutline = IM_COL32(255, 214, 64, 220);
    constexpr float kHandleRadius = 6.0f;
    constexpr float kPickRadius = 10.0f;
    constexpr float kIconSizePx = 64.0f;
    constexpr float kClipEpsilon = 1e-6f;

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

    bool clipHomogeneousLine(glm::vec4& a, glm::vec4& b){
        auto clipPlane = [&](float fa, float fb) -> bool {
            if(fa < 0.0f && fb < 0.0f){
                return false;
            }
            if(fa < 0.0f || fb < 0.0f){
                float t = fa / (fa - fb);
                glm::vec4 p = a + (b - a) * t;
                if(fa < 0.0f){
                    a = p;
                }else{
                    b = p;
                }
            }
            return true;
        };

        if(!clipPlane(a.x + a.w, b.x + b.w)) return false;
        if(!clipPlane(-a.x + a.w, -b.x + b.w)) return false;
        if(!clipPlane(a.y + a.w, b.y + b.w)) return false;
        if(!clipPlane(-a.y + a.w, -b.y + b.w)) return false;
        if(!clipPlane(a.z + a.w, b.z + b.w)) return false;
        if(!clipPlane(-a.z + a.w, -b.z + b.w)) return false;
        return true;
    }

    bool projectWorldPoint(View* view,
                           PCamera camera,
                           const TransformWidget::Viewport& viewport,
                           const Math3D::Vec3& worldPos,
                           ImVec2& out){
        if(!view || !camera || !viewport.valid || viewport.w <= 0.0f || viewport.h <= 0.0f){
            return false;
        }

        glm::mat4 viewMat = (glm::mat4)camera->getViewMatrix();
        glm::mat4 projMat = (glm::mat4)camera->getProjectionMatrix();
        glm::vec4 clip = projMat * viewMat * glm::vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
        if(clip.x < -clip.w || clip.x > clip.w ||
           clip.y < -clip.w || clip.y > clip.w ||
           clip.z < -clip.w || clip.z > clip.w){
            return false;
        }
        if(std::fabs(clip.w) < kClipEpsilon){
            return false;
        }
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        out.x = viewport.x + (ndc.x + 1.0f) * 0.5f * viewport.w;
        out.y = viewport.y + (1.0f - (ndc.y + 1.0f) * 0.5f) * viewport.h;
        return true;
    }

    bool clipWorldLineToScreen(View* view,
                               PCamera camera,
                               const TransformWidget::Viewport& viewport,
                               const Math3D::Vec3& aWorld,
                               const Math3D::Vec3& bWorld,
                               ImVec2& outA,
                               ImVec2& outB){
        if(!view || !camera || !viewport.valid || viewport.w <= 0.0f || viewport.h <= 0.0f){
            return false;
        }
        glm::mat4 viewMat = (glm::mat4)camera->getViewMatrix();
        glm::mat4 projMat = (glm::mat4)camera->getProjectionMatrix();
        glm::vec4 a = projMat * viewMat * glm::vec4(aWorld.x, aWorld.y, aWorld.z, 1.0f);
        glm::vec4 b = projMat * viewMat * glm::vec4(bWorld.x, bWorld.y, bWorld.z, 1.0f);
        if(!clipHomogeneousLine(a, b)){
            return false;
        }
        if(std::fabs(a.w) < kClipEpsilon || std::fabs(b.w) < kClipEpsilon){
            return false;
        }
        glm::vec3 ndcA = glm::vec3(a) / a.w;
        glm::vec3 ndcB = glm::vec3(b) / b.w;
        outA.x = viewport.x + (ndcA.x + 1.0f) * 0.5f * viewport.w;
        outA.y = viewport.y + (1.0f - (ndcA.y + 1.0f) * 0.5f) * viewport.h;
        outB.x = viewport.x + (ndcB.x + 1.0f) * 0.5f * viewport.w;
        outB.y = viewport.y + (1.0f - (ndcB.y + 1.0f) * 0.5f) * viewport.h;
        return true;
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
    ImVec2 screen;
    return projectWorldPoint(view, camera, viewport, worldPos, screen);
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
    ImVec2 screen;
    if(!projectWorldPoint(view, camera, viewport, worldPos, screen)){
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
        ImVec2 centerPt(0,0);
        if(!projectWorldPoint(view, camera, viewport, lightPos, centerPt)){
            centerPt = ImVec2(0,0);
        }
        Math3D::Vec3 handleWorld = lightPos + camRight * range;
        ImVec2 handlePt(0,0);
        const bool handleVisible = projectWorldPoint(view, camera, viewport, handleWorld, handlePt);
        handleScreenPos = handlePt;
        Math3D::Vec2 axis2(handlePt.x - centerPt.x, handlePt.y - centerPt.y);
        float axisLen = axis2.length();
        if(axisLen > Math3D::EPSILON){
            axisDir = Math3D::Vec2(axis2.x / axisLen, axis2.y / axisLen);
        }
        ImVec2 unitPt;
        if(projectWorldPoint(view, camera, viewport, lightPos + camRight, unitPt)){
            axisPixelsPerUnit = Math3D::Vec2(unitPt.x - centerPt.x, unitPt.y - centerPt.y).length();
        }
        if(allowInput && handleVisible && isOnScreen(view, camera, viewport, lightPos)){
            if(screenDistance(mousePt, handleScreenPos) <= kPickRadius){
                hoverHandle = Handle::PointRadius;
            }
        }
    }else if(light.type == LightType::SPOT){
        Math3D::Vec3 dir = syncDirection ? Math3D::Vec3(0,0,1) : light.direction;
        if(syncDirection){
            dir = worldForward;
        }
        if(dir.length() < Math3D::EPSILON){
            dir = Math3D::Vec3(0,-1,0);
        }else{
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
        ImVec2 axisPt(0,0);
        ImVec2 centerPt(0,0);
        const bool axisVisible = projectWorldPoint(view, camera, viewport, axisPoint, axisPt);
        const bool centerVisible = projectWorldPoint(view, camera, viewport, lightPos, centerPt);
        Math3D::Vec2 axisVec(axisPt.x - centerPt.x, axisPt.y - centerPt.y);
        float axisLen = axisVec.length();
        Math3D::Vec2 axisDirRange(1,0);
        if(axisLen > Math3D::EPSILON){
            axisDirRange = Math3D::Vec2(axisVec.x / axisLen, axisVec.y / axisLen);
        }
        float rangePixelsPerUnit = 1.0f;
        ImVec2 unitRangePt;
        if(projectWorldPoint(view, camera, viewport, lightPos + dir, unitRangePt)){
            rangePixelsPerUnit = Math3D::Vec2(unitRangePt.x - centerPt.x, unitRangePt.y - centerPt.y).length();
        }

        // Angle handle on cone edge
        Math3D::Vec3 handleWorld = axisPoint + axisRight * radius;
        ImVec2 anglePt(0,0);
        const bool angleVisible = projectWorldPoint(view, camera, viewport, handleWorld, anglePt);
        ImVec2 angleLeftPt(0,0);
        const bool angleLeftVisible = projectWorldPoint(view, camera, viewport, axisPoint - axisRight * radius, angleLeftPt);
        Math3D::Vec2 angleAxis(anglePt.x - axisPt.x, anglePt.y - axisPt.y);
        float angleLen = angleAxis.length();
        Math3D::Vec2 angleDir(1,0);
        if(angleLen > Math3D::EPSILON){
            angleDir = Math3D::Vec2(angleAxis.x / angleLen, angleAxis.y / angleLen);
        }
        Math3D::Vec2 angleAxisLeft(angleLeftPt.x - axisPt.x, angleLeftPt.y - axisPt.y);
        float angleLeftLen = angleAxisLeft.length();
        Math3D::Vec2 angleDirLeft(1,0);
        if(angleLeftLen > Math3D::EPSILON){
            angleDirLeft = Math3D::Vec2(angleAxisLeft.x / angleLeftLen, angleAxisLeft.y / angleLeftLen);
        }
        float anglePixelsPerUnit = 1.0f;
        ImVec2 unitAnglePt;
        if(projectWorldPoint(view, camera, viewport, axisPoint + axisRight, unitAnglePt)){
            anglePixelsPerUnit = Math3D::Vec2(unitAnglePt.x - axisPt.x, unitAnglePt.y - axisPt.y).length();
        }

        if(allowInput && axisVisible && centerVisible){
            float distRange = screenDistance(mousePt, axisPt);
            float distAngle = screenDistance(mousePt, anglePt);
            float distAngleLeft = screenDistance(mousePt, angleLeftPt);
            if(distRange <= kPickRadius){
                hoverHandle = Handle::SpotRange;
                handleScreenPos = axisPt;
                axisDir = axisDirRange;
                axisPixelsPerUnit = rangePixelsPerUnit;
            }else if(angleVisible && distAngle <= kPickRadius && distAngle <= distAngleLeft){
                hoverHandle = Handle::SpotAngle;
                handleScreenPos = anglePt;
                axisDir = angleDir;
                axisPixelsPerUnit = anglePixelsPerUnit;
            }else if(angleLeftVisible && distAngleLeft <= kPickRadius){
                hoverHandle = Handle::SpotAngle;
                handleScreenPos = angleLeftPt;
                axisDir = angleDirLeft;
                axisPixelsPerUnit = anglePixelsPerUnit;
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

    ImU32 color = kLightColor;
    ImU32 outline = kLightOutline;
    ImU32 iconTint = toImColor(light.color);

    if(light.type == LightType::POINT){
        float range = Math3D::Max(0.1f, light.range);
        std::vector<Math3D::Vec3> ringPoints;
        for(const Math3D::Vec3& axis : {Math3D::Vec3(1,0,0), Math3D::Vec3(0,1,0), Math3D::Vec3(0,0,1)}){
            buildRingPoints(ringPoints, lightPos, axis, range);
            bool hasPrev = false;
            Math3D::Vec3 prevWorld;
            for(const auto& wp : ringPoints){
                if(hasPrev){
                    ImVec2 a, b;
                    if(clipWorldLineToScreen(view, camera, viewport, prevWorld, wp, a, b)){
                        drawList->AddLine(a, b, outline, 2.0f);
                    }
                }
                prevWorld = wp;
                hasPrev = true;
            }
        }
        auto camTx = camera->transform();
        Math3D::Vec3 camRight = camTx.right().normalize();
        Math3D::Vec3 handleWorld = lightPos + camRight * range;
        ImVec2 handlePt;
        if(projectWorldPoint(view, camera, viewport, handleWorld, handlePt)){
            drawList->AddCircleFilled(handlePt, kHandleRadius, color);
        }
        drawBillboard(drawList, view, camera, viewport, lightPos, icons.point, kIconSizePx, iconTint);
    }else if(light.type == LightType::DIRECTIONAL){
        Math3D::Vec3 dir = syncDirection ? Math3D::Vec3(0,0,1) : light.direction;
        if(syncDirection){
            dir = worldForward;
        }
        if(dir.length() < Math3D::EPSILON){
            dir = Math3D::Vec3(0,-1,0);
        }else{
            dir = dir.normalize();
        }
        float length = 2.0f;
        Math3D::Vec3 endWorld = lightPos + dir * length;
        ImVec2 startPt, endPt;
        if(clipWorldLineToScreen(view, camera, viewport, lightPos, endWorld, startPt, endPt)){
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
        }
        drawBillboard(drawList, view, camera, viewport, lightPos, icons.directional, kIconSizePx, iconTint);
    }else if(light.type == LightType::SPOT){
        Math3D::Vec3 dir = syncDirection ? Math3D::Vec3(0,0,1) : light.direction;
        if(syncDirection){
            dir = worldForward;
        }
        if(dir.length() < Math3D::EPSILON){
            dir = Math3D::Vec3(0,-1,0);
        }else{
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
        ImVec2 origin;
        if(projectWorldPoint(view, camera, viewport, lightPos, origin)){
            ImVec2 a, b;
            if(clipWorldLineToScreen(view, camera, viewport, lightPos, axisPoint, a, b)){
                drawList->AddLine(a, b, outline, 2.0f);
            }
            if(clipWorldLineToScreen(view, camera, viewport, lightPos, edgeRight, a, b)){
                drawList->AddLine(a, b, outline, 2.0f);
            }
            if(clipWorldLineToScreen(view, camera, viewport, lightPos, edgeLeft, a, b)){
                drawList->AddLine(a, b, outline, 2.0f);
            }
        }

        // Ring at the cone end (projected, so it appears as an ellipse in screen space)
        if(radius > 0.0001f){
            std::vector<Math3D::Vec3> ringPoints;
            buildRingPoints(ringPoints, axisPoint, dir, radius);
            bool hasPrev = false;
            Math3D::Vec3 prevWorld;
            for(const auto& wp : ringPoints){
                if(hasPrev){
                    ImVec2 a, b;
                    if(clipWorldLineToScreen(view, camera, viewport, prevWorld, wp, a, b)){
                        drawList->AddLine(a, b, outline, 2.0f);
                    }
                }
                prevWorld = wp;
                hasPrev = true;
            }
        }

        ImVec2 axisPt, rightPt, leftPt;
        const bool axisVisible = projectWorldPoint(view, camera, viewport, axisPoint, axisPt);
        const bool rightVisible = projectWorldPoint(view, camera, viewport, edgeRight, rightPt);
        const bool leftVisible = projectWorldPoint(view, camera, viewport, edgeLeft, leftPt);
        if(rightVisible){
            drawList->AddCircleFilled(rightPt, kHandleRadius, color);
        }
        if(leftVisible){
            drawList->AddCircleFilled(leftPt, kHandleRadius, color);
        }
        if(axisVisible){
            drawList->AddCircleFilled(axisPt, kHandleRadius, color);
        }
        drawBillboard(drawList, view, camera, viewport, lightPos, icons.spot, kIconSizePx, iconTint);
    }
}
