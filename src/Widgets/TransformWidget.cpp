#include "Widgets/TransformWidget.h"

#include <cmath>
#include <algorithm>
#include <vector>

namespace {
    constexpr float kPickThresholdPx = 8.0f;
    constexpr float kRotatePickThresholdPx = 10.0f;
    constexpr int kRingSegments = 48;
    constexpr float kClipEpsilon = 1e-6f;

    ImU32 axisColor(TransformWidget::Axis axis, bool active){
        if(active){
            return IM_COL32(255, 214, 64, 255);
        }

        switch(axis){
            case TransformWidget::Axis::X: return IM_COL32(232, 92, 92, 255);
            case TransformWidget::Axis::Y: return IM_COL32(94, 214, 96, 255);
            case TransformWidget::Axis::Z: return IM_COL32(102, 164, 255, 255);
            default: break;
        }
        return IM_COL32(200, 200, 200, 255);
    }

    float distancePointToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b){
        const float vx = b.x - a.x;
        const float vy = b.y - a.y;
        const float wx = p.x - a.x;
        const float wy = p.y - a.y;
        const float denom = vx * vx + vy * vy;
        float t = 0.0f;
        if(denom > 1e-5f){
            t = (wx * vx + wy * vy) / denom;
            t = std::clamp(t, 0.0f, 1.0f);
        }
        const float dx = a.x + t * vx - p.x;
        const float dy = a.y + t * vy - p.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    bool intersectRayPlane(const Math3D::Vec3& rayOrigin,
                           const Math3D::Vec3& rayDir,
                           const Math3D::Vec3& planePoint,
                           const Math3D::Vec3& planeNormal,
                           Math3D::Vec3& outPoint){
        float denom = Math3D::Vec3::dot(rayDir, planeNormal);
        if(std::fabs(denom) < 1e-5f){
            return false;
        }
        float t = Math3D::Vec3::dot(planePoint - rayOrigin, planeNormal) / denom;
        if(t < 0.0f){
            return false;
        }
        outPoint = rayOrigin + (rayDir * t);
        return true;
    }

    bool closestPointOnAxis(const Math3D::Vec3& rayOrigin,
                            const Math3D::Vec3& rayDir,
                            const Math3D::Vec3& axisOrigin,
                            const Math3D::Vec3& axisDir,
                            float& outAxisT){
        const Math3D::Vec3 r = rayOrigin - axisOrigin;
        const float a = Math3D::Vec3::dot(rayDir, rayDir);
        const float b = Math3D::Vec3::dot(rayDir, axisDir);
        const float c = Math3D::Vec3::dot(axisDir, axisDir);
        const float d = Math3D::Vec3::dot(rayDir, r);
        const float e = Math3D::Vec3::dot(axisDir, r);
        const float denom = a * c - b * b;
        if(std::fabs(denom) < 1e-5f){
            return false;
        }
        const float s = (b * e - c * d) / denom;
        outAxisT = (a * e - b * d) / denom;
        return s >= 0.0f;
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

        out.reserve(kRingSegments + 1);
        for(int i = 0; i <= kRingSegments; ++i){
            float t = (float)i / (float)kRingSegments;
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

        if(!clipPlane(a.x + a.w, b.x + b.w)) return false; // left
        if(!clipPlane(-a.x + a.w, -b.x + b.w)) return false; // right
        if(!clipPlane(a.y + a.w, b.y + b.w)) return false; // bottom
        if(!clipPlane(-a.y + a.w, -b.y + b.w)) return false; // top
        if(!clipPlane(a.z + a.w, b.z + b.w)) return false; // near
        if(!clipPlane(-a.z + a.w, -b.z + b.w)) return false; // far
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

    bool isGizmoOnScreen(View* view,
                         PCamera camera,
                         const TransformWidget::Viewport& viewport,
                         const Math3D::Vec3& worldPos){
        ImVec2 screen;
        return projectWorldPoint(view, camera, viewport, worldPos, screen);
    }
}

TransformWidget::TransformWidget() = default;

void TransformWidget::setMode(Mode newMode){
    mode = newMode;
    reset();
}

void TransformWidget::reset(){
    dragging = false;
    activeAxis = Axis::None;
    hoverAxis = Axis::None;
    activeHandle = Handle::None;
    hoverHandle = Handle::None;
    startAxisT = 0.0f;
    startPlaneVec = Math3D::Vec3(1,0,0);
}

float TransformWidget::computeGizmoSize(PCamera camera, const Math3D::Vec3& worldPos) const{
    if(!camera) return 1.0f;
    Math3D::Vec3 camPos = camera->transform().position;
    float dist = (worldPos - camPos).length();
    float size = dist * 0.12f;
    size = Math3D::Clamp(size, 0.6f, 25.0f);
    return size;
}

Math3D::Vec3 TransformWidget::axisLocalFromEnum(Axis axis) const{
    switch(axis){
        case Axis::X: return Math3D::Vec3(1,0,0);
        case Axis::Y: return Math3D::Vec3(0,1,0);
        case Axis::Z: return Math3D::Vec3(0,0,1);
        default: break;
    }
    return Math3D::Vec3(1,0,0);
}

Math3D::Vec3 TransformWidget::axisWorldFromLocal(const Math3D::Transform& transform, Axis axis) const{
    Math3D::Vec3 local = axisLocalFromEnum(axis);
    return transform.rotation * local;
}

TransformWidget::HoverResult TransformWidget::pickHandleFromMouse(View* view,
                                                                  InputManager* input,
                                                                  PCamera camera,
                                                                  const Viewport& viewport,
                                                                  const Math3D::Vec3& worldPos,
                                                                  const Math3D::Transform& transform) const{
    if(!view || !input || !camera || !viewport.valid){
        return {};
    }

    Math3D::Vec2 mouse = input->getMousePosition();
    ImVec2 mousePt(mouse.x, mouse.y);

    HoverResult best{};
    float bestDist = 1e9f;

    ImVec2 centerPt(0,0);
    const bool centerVisible = projectWorldPoint(view, camera, viewport, worldPos, centerPt);
    float centerDx = mousePt.x - centerPt.x;
    float centerDy = mousePt.y - centerPt.y;
    float centerDist = centerVisible ? std::sqrt(centerDx * centerDx + centerDy * centerDy) : 1e9f;

    if(mode == Mode::Combined && centerDist <= centerPickRadius){
        best.handle = Handle::UniformScale;
        best.axis = Axis::None;
        return best;
    }

    auto consider = [&](Handle handle, Axis axis, float dist, float threshold){
        if(dist <= threshold && dist < bestDist){
            bestDist = dist;
            best.handle = handle;
            best.axis = axis;
        }
    };

    if(mode == Mode::Rotate || mode == Mode::Combined){
        std::vector<Math3D::Vec3> ringPoints;
        for(Axis axis : {Axis::X, Axis::Y, Axis::Z}){
            Math3D::Vec3 axisWorld = axisWorldFromLocal(transform, axis).normalize();
            buildRingPoints(ringPoints, worldPos, axisWorld, gizmoSize);
            bool hasPrev = false;
            Math3D::Vec3 prevWorld;
            for(const auto& wp : ringPoints){
                if(hasPrev){
                    ImVec2 a, b;
                    if(clipWorldLineToScreen(view, camera, viewport, prevWorld, wp, a, b)){
                        float dist = distancePointToSegment(mousePt, a, b);
                        consider(Handle::Rotate, axis, dist, kRotatePickThresholdPx);
                    }
                }
                prevWorld = wp;
                hasPrev = true;
            }
        }
        if(mode == Mode::Rotate && best.handle == Handle::Rotate){
            return best;
        }
    }

    if(mode == Mode::Translate || mode == Mode::Combined){
        for(Axis axis : {Axis::X, Axis::Y, Axis::Z}){
            if(centerDist <= centerIgnoreRadius){
                continue;
            }
            Math3D::Vec3 axisWorld = axisWorldFromLocal(transform, axis).normalize();
            Math3D::Vec3 start = worldPos;
            Math3D::Vec3 end = worldPos + axisWorld * (gizmoSize * translateDotOffset);
            ImVec2 a, b;
            if(clipWorldLineToScreen(view, camera, viewport, start, end, a, b)){
                float distLine = distancePointToSegment(mousePt, a, b);
                consider(Handle::Translate, axis, distLine, kPickThresholdPx);

                float dx = mousePt.x - b.x;
                float dy = mousePt.y - b.y;
                float distDot = std::sqrt(dx * dx + dy * dy);
                consider(Handle::Translate, axis, distDot, translateDotRadius + 3.0f);
            }
        }
        if(mode == Mode::Translate && best.handle == Handle::Translate){
            return best;
        }
    }

    if(mode == Mode::Scale || mode == Mode::Combined){
        for(Axis axis : {Axis::X, Axis::Y, Axis::Z}){
            if(centerDist <= centerIgnoreRadius){
                continue;
            }
            Math3D::Vec3 axisWorld = axisWorldFromLocal(transform, axis).normalize();
            Math3D::Vec3 end = worldPos + axisWorld * (gizmoSize * scaleSquareOffset);
            ImVec2 b;
            if(projectWorldPoint(view, camera, viewport, end, b)){
                float dx = std::fabs(mousePt.x - b.x);
                float dy = std::fabs(mousePt.y - b.y);
                float distSquare = std::max(dx, dy);
                consider(Handle::Scale, axis, distSquare, scaleSquareHalf + 3.0f);
            }
        }

        if(mode == Mode::Combined){
            if(centerVisible){
                float dx = std::fabs(mousePt.x - centerPt.x);
                float dy = std::fabs(mousePt.y - centerPt.y);
                float distSquare = std::max(dx, dy);
                consider(Handle::UniformScale, Axis::None, distSquare, uniformSquareHalf + 4.0f);
            }
        }
        if(mode == Mode::Scale && best.handle == Handle::Scale){
            return best;
        }
    }

    return best;
}

bool TransformWidget::update(View* view,
                             InputManager* input,
                             PCamera camera,
                             const Viewport& viewport,
                             const Math3D::Vec3& worldPos,
                             Math3D::Transform& transform,
                             bool allowInput,
                             bool lmbPressed,
                             bool lmbDown,
                             bool lmbReleased){
    if(!view || !camera || !input || !viewport.valid){
        hoverAxis = Axis::None;
        dragging = false;
        activeAxis = Axis::None;
        hoverHandle = Handle::None;
        activeHandle = Handle::None;
        return false;
    }

    gizmoSize = computeGizmoSize(camera, worldPos);

    if(!dragging && allowInput){
        if(!isGizmoOnScreen(view, camera, viewport, worldPos)){
            hoverAxis = Axis::None;
            hoverHandle = Handle::None;
            return false;
        }
        HoverResult hover = pickHandleFromMouse(view, input, camera, viewport, worldPos, transform);
        hoverHandle = hover.handle;
        hoverAxis = hover.axis;
    }else if(!allowInput && !dragging){
        hoverAxis = Axis::None;
        hoverHandle = Handle::None;
    }

    if(lmbPressed && allowInput && hoverHandle != Handle::None){
        dragging = true;
        activeAxis = hoverAxis;
        activeHandle = hoverHandle;
        startTransform = transform;
        startWorldPos = worldPos;
        startMouse = input->getMousePosition();
        dragAxisLocal = axisLocalFromEnum(activeAxis);
        dragAxisWorld = axisWorldFromLocal(startTransform, activeAxis).normalize();

        Math3D::Vec2 mouse = input->getMousePosition();
        Math3D::Vec3 rayStart = view->screenToWorld(camera, mouse.x, mouse.y, 0.0f, viewport.x, viewport.y, viewport.w, viewport.h);
        Math3D::Vec3 rayEnd = view->screenToWorld(camera, mouse.x, mouse.y, 1.0f, viewport.x, viewport.y, viewport.w, viewport.h);
        Math3D::Vec3 rayDir = (rayEnd - rayStart).normalize();

        if(activeHandle == Handle::Rotate){
            Math3D::Vec3 hitPoint;
            if(intersectRayPlane(rayStart, rayDir, startWorldPos, dragAxisWorld, hitPoint)){
                Math3D::Vec3 delta = (hitPoint - startWorldPos);
                if(delta.length() > Math3D::EPSILON){
                    startPlaneVec = delta.normalize();
                }
            }
        }else if(activeHandle == Handle::Translate || activeHandle == Handle::Scale){
            float axisT = 0.0f;
            if(closestPointOnAxis(rayStart, rayDir, startWorldPos, dragAxisWorld, axisT)){
                startAxisT = axisT;
            }else{
                startAxisT = 0.0f;
            }
        }

        return true;
    }

    if(dragging){
        if(lmbReleased || !lmbDown){
            dragging = false;
            activeAxis = Axis::None;
            activeHandle = Handle::None;
            return true;
        }

        Math3D::Vec2 mouse = input->getMousePosition();
        Math3D::Vec3 rayStart = view->screenToWorld(camera, mouse.x, mouse.y, 0.0f, viewport.x, viewport.y, viewport.w, viewport.h);
        Math3D::Vec3 rayEnd = view->screenToWorld(camera, mouse.x, mouse.y, 1.0f, viewport.x, viewport.y, viewport.w, viewport.h);
        Math3D::Vec3 rayDir = (rayEnd - rayStart).normalize();

        if(activeHandle == Handle::Translate){
            float axisT = 0.0f;
            if(closestPointOnAxis(rayStart, rayDir, startWorldPos, dragAxisWorld, axisT)){
                float delta = axisT - startAxisT;
                transform.position = startTransform.position + dragAxisWorld * delta;
            }
        }else if(activeHandle == Handle::Scale){
            float axisT = 0.0f;
            if(closestPointOnAxis(rayStart, rayDir, startWorldPos, dragAxisWorld, axisT)){
                float delta = axisT - startAxisT;
                Math3D::Vec3 scale = startTransform.scale;
                if(activeAxis == Axis::X){
                    scale.x = Math3D::Max(0.01f, scale.x + delta);
                }else if(activeAxis == Axis::Y){
                    scale.y = Math3D::Max(0.01f, scale.y + delta);
                }else if(activeAxis == Axis::Z){
                    scale.z = Math3D::Max(0.01f, scale.z + delta);
                }
                transform.scale = scale;
            }
        }else if(activeHandle == Handle::UniformScale){
            Math3D::Vec2 current = input->getMousePosition();
            float delta = (current.x - startMouse.x + current.y - startMouse.y) * 0.005f;
            float scaleFactor = Math3D::Max(0.01f, 1.0f + delta);
            transform.scale = startTransform.scale * scaleFactor;
        }else if(activeHandle == Handle::Rotate){
            Math3D::Vec3 hitPoint;
            if(intersectRayPlane(rayStart, rayDir, startWorldPos, dragAxisWorld, hitPoint)){
                Math3D::Vec3 delta = (hitPoint - startWorldPos);
                if(delta.length() > Math3D::EPSILON){
                    Math3D::Vec3 currentVec = delta.normalize();
                    float dot = Math3D::Vec3::dot(startPlaneVec, currentVec);
                    dot = Math3D::Clamp(dot, -1.0f, 1.0f);
                    Math3D::Vec3 cross = Math3D::Vec3::cross(startPlaneVec, currentVec);
                    float angleRad = std::atan2(Math3D::Vec3::dot(dragAxisWorld, cross), dot);
                    float angleDeg = angleRad * (180.0f / Math3D::PI);

                    transform = startTransform;
                    transform.rotateAxisAngle(dragAxisLocal, angleDeg, true);
                }
            }
        }

        return true;
    }

    return hoverHandle != Handle::None;
}

void TransformWidget::draw(ImDrawList* drawList,
                           View* view,
                           PCamera camera,
                           const Viewport& viewport,
                           const Math3D::Vec3& worldPos,
                           const Math3D::Transform& transform,
                           bool viewportHovered) const{
    if(!drawList || !view || !camera || !viewport.valid){
        return;
    }

    float size = computeGizmoSize(camera, worldPos);

    if(mode == Mode::Rotate || mode == Mode::Combined){
        std::vector<Math3D::Vec3> ringPoints;
        for(Axis axis : {Axis::X, Axis::Y, Axis::Z}){
            Math3D::Vec3 axisWorld = axisWorldFromLocal(transform, axis).normalize();
            buildRingPoints(ringPoints, worldPos, axisWorld, size);
            bool highlight = (axis == activeAxis && activeHandle == Handle::Rotate) ||
                             (axis == hoverAxis && hoverHandle == Handle::Rotate);
            ImU32 col = axisColor(axis, highlight);

            bool hasPrev = false;
            Math3D::Vec3 prevWorld;
            for(const auto& wp : ringPoints){
                if(hasPrev){
                    float thickness = highlight ? lineThicknessActive : lineThickness;
                    ImVec2 a, b;
                    if(clipWorldLineToScreen(view, camera, viewport, prevWorld, wp, a, b)){
                        drawList->AddLine(a, b, col, thickness);
                    }
                }
                prevWorld = wp;
                hasPrev = true;
            }
        }
        if(mode == Mode::Rotate){
            return;
        }
    }

    if(mode == Mode::Translate || mode == Mode::Scale || mode == Mode::Combined){
        for(Axis axis : {Axis::X, Axis::Y, Axis::Z}){
            Math3D::Vec3 axisWorld = axisWorldFromLocal(transform, axis).normalize();
            Math3D::Vec3 start = worldPos;
            Math3D::Vec3 endLine = worldPos + axisWorld * size;
            bool highlightLine = (axis == activeAxis && activeHandle == Handle::Translate) ||
                                 (axis == hoverAxis && hoverHandle == Handle::Translate);
            ImU32 colLine = axisColor(axis, highlightLine);
            float thickness = highlightLine ? lineThicknessActive : lineThickness;
            ImVec2 a, b;
            if(clipWorldLineToScreen(view, camera, viewport, start, endLine, a, b)){
                drawList->AddLine(a, b, colLine, thickness);
            }

            if(mode == Mode::Translate || mode == Mode::Combined){
                Math3D::Vec3 endDot = worldPos + axisWorld * (size * translateDotOffset);
                ImVec2 d;
                if(projectWorldPoint(view, camera, viewport, endDot, d)){
                    bool highlightDot = (axis == activeAxis && activeHandle == Handle::Translate) ||
                                        (axis == hoverAxis && hoverHandle == Handle::Translate);
                    ImU32 colDot = axisColor(axis, highlightDot);
                    drawList->AddCircleFilled(d, translateDotRadius, colDot);
                }
            }

            if(mode == Mode::Scale || mode == Mode::Combined){
                Math3D::Vec3 endSquare = worldPos + axisWorld * (size * scaleSquareOffset);
                ImVec2 s;
                if(projectWorldPoint(view, camera, viewport, endSquare, s)){
                    bool highlightSq = (axis == activeAxis && activeHandle == Handle::Scale) ||
                                       (axis == hoverAxis && hoverHandle == Handle::Scale);
                    ImU32 colSq = axisColor(axis, highlightSq);
                    drawList->AddRectFilled(ImVec2(s.x - scaleSquareHalf, s.y - scaleSquareHalf),
                                            ImVec2(s.x + scaleSquareHalf, s.y + scaleSquareHalf),
                                            colSq);
                }
            }
        }
    }

    if(mode == Mode::Combined){
        ImVec2 c;
        if(projectWorldPoint(view, camera, viewport, worldPos, c)){
            bool highlight = (activeHandle == Handle::UniformScale) || (hoverHandle == Handle::UniformScale);
            ImU32 col = highlight ? IM_COL32(255, 214, 64, 255) : IM_COL32(220, 220, 220, 255);
            drawList->AddRectFilled(ImVec2(c.x - uniformSquareHalf, c.y - uniformSquareHalf),
                                    ImVec2(c.x + uniformSquareHalf, c.y + uniformSquareHalf),
                                    col);
        }
    }

}
