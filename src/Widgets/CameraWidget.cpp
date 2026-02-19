#include "Widgets/CameraWidget.h"

#include <cmath>

namespace {
    constexpr ImU32 kCameraColor = IM_COL32(104, 214, 255, 255);
    constexpr ImU32 kCameraOutline = IM_COL32(104, 214, 255, 210);
    constexpr float kHandleRadius = 6.0f;
    constexpr float kPickRadius = 10.0f;
    constexpr float kIconSizePx = 56.0f;
    constexpr float kClipEpsilon = 1e-6f;

    float screenDistance(const ImVec2& a, const ImVec2& b){
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    void buildCameraBasis(const Math3D::Vec3& rawForward,
                          Math3D::Vec3& outForward,
                          Math3D::Vec3& outRight,
                          Math3D::Vec3& outUp){
        outForward = rawForward;
        if(outForward.length() < Math3D::EPSILON){
            outForward = Math3D::Vec3(0,0,1);
        }else{
            outForward = outForward.normalize();
        }

        Math3D::Vec3 worldUp(0,1,0);
        if(std::fabs(Math3D::Vec3::dot(outForward, worldUp)) > 0.95f){
            worldUp = Math3D::Vec3(1,0,0);
        }
        outRight = Math3D::Vec3::cross(outForward, worldUp).normalize();
        outUp = Math3D::Vec3::cross(outRight, outForward).normalize();
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

    void drawWorldLine(ImDrawList* drawList,
                       View* view,
                       PCamera viewportCamera,
                       const TransformWidget::Viewport& viewport,
                       const Math3D::Vec3& a,
                       const Math3D::Vec3& b,
                       ImU32 color,
                       float thickness){
        ImVec2 sa, sb;
        if(clipWorldLineToScreen(view, viewportCamera, viewport, a, b, sa, sb)){
            drawList->AddLine(sa, sb, color, thickness);
        }
    }

    void drawBillboard(ImDrawList* drawList,
                       View* view,
                       PCamera viewportCamera,
                       const TransformWidget::Viewport& viewport,
                       const Math3D::Vec3& worldPos,
                       PTexture texture,
                       float sizePx,
                       ImU32 tint){
        if(!drawList || !texture){
            return;
        }
        ImVec2 screen;
        if(!projectWorldPoint(view, viewportCamera, viewport, worldPos, screen)){
            return;
        }
        float half = sizePx * 0.5f;
        ImVec2 pmin(screen.x - half, screen.y - half);
        ImVec2 pmax(screen.x + half, screen.y + half);
        ImTextureID texId = (ImTextureID)(intptr_t)texture->getID();
        drawList->AddImage(texId, pmin, pmax, ImVec2(0, 0), ImVec2(1, 1), tint);
    }
}

void CameraWidget::reset(){
    dragging = false;
    activeHandle = Handle::None;
}

bool CameraWidget::update(View* view,
                          InputManager* input,
                          PCamera viewportCamera,
                          const TransformWidget::Viewport& viewport,
                          const Math3D::Vec3& worldPos,
                          const Math3D::Vec3& worldForward,
                          CameraSettings& settings,
                          bool allowInput,
                          bool lmbPressed,
                          bool lmbDown,
                          bool lmbReleased){
    if(!view || !input || !viewportCamera || !viewport.valid){
        reset();
        return false;
    }

    Math3D::Vec3 forward, right, up;
    buildCameraBasis(worldForward, forward, right, up);

    float nearPlane = Math3D::Max(0.01f, settings.nearPlane);
    float farPlane = Math3D::Max(nearPlane + 0.1f, settings.farPlane);
    float aspect = Math3D::Max(0.01f, settings.aspect);
    float fov = Math3D::Clamp(settings.fov, 10.0f, 130.0f);

    Math3D::Vec3 farCenter = worldPos + (forward * farPlane);
    float farHalfH = farPlane * std::tan(Math3D::PI * (fov * 0.5f) / 180.0f);
    float farHalfW = farHalfH * aspect;
    Math3D::Vec3 fovHandleWorld = farCenter + right * farHalfW;
    Math3D::Vec3 farHandleWorld = farCenter;

    Math3D::Vec2 mouse = input->getMousePosition();
    ImVec2 mousePt(mouse.x, mouse.y);

    Handle hoverHandle = Handle::None;
    Math3D::Vec2 axisDir(1,0);
    float axisPixelsPerUnit = 1.0f;
    ImVec2 handlePt(0,0);

    ImVec2 fovHandlePt, farHandlePt;
    const bool fovVisible = projectWorldPoint(view, viewportCamera, viewport, fovHandleWorld, fovHandlePt);
    const bool farVisible = projectWorldPoint(view, viewportCamera, viewport, farHandleWorld, farHandlePt);

    if(allowInput){
        float bestDist = 1e9f;
        if(!settings.isOrtho && fovVisible){
            float dist = screenDistance(mousePt, fovHandlePt);
            if(dist <= kPickRadius && dist < bestDist){
                bestDist = dist;
                hoverHandle = Handle::Fov;
                handlePt = fovHandlePt;
                ImVec2 centerPt;
                if(projectWorldPoint(view, viewportCamera, viewport, farCenter, centerPt)){
                    Math3D::Vec2 axis((fovHandlePt.x - centerPt.x), (fovHandlePt.y - centerPt.y));
                    float len = axis.length();
                    if(len > Math3D::EPSILON){
                        axisDir = Math3D::Vec2(axis.x / len, axis.y / len);
                    }
                }
                ImVec2 unitPt, centerPt2;
                if(projectWorldPoint(view, viewportCamera, viewport, farCenter + right, unitPt) &&
                   projectWorldPoint(view, viewportCamera, viewport, farCenter, centerPt2)){
                    axisPixelsPerUnit = Math3D::Vec2(unitPt.x - centerPt2.x, unitPt.y - centerPt2.y).length();
                }
            }
        }
        if(farVisible){
            float dist = screenDistance(mousePt, farHandlePt);
            if(dist <= kPickRadius && dist < bestDist){
                hoverHandle = Handle::FarPlane;
                handlePt = farHandlePt;
                ImVec2 originPt;
                if(projectWorldPoint(view, viewportCamera, viewport, worldPos, originPt)){
                    Math3D::Vec2 axis((farHandlePt.x - originPt.x), (farHandlePt.y - originPt.y));
                    float len = axis.length();
                    if(len > Math3D::EPSILON){
                        axisDir = Math3D::Vec2(axis.x / len, axis.y / len);
                    }
                }
                ImVec2 unitPt, originPt2;
                if(projectWorldPoint(view, viewportCamera, viewport, worldPos + forward, unitPt) &&
                   projectWorldPoint(view, viewportCamera, viewport, worldPos, originPt2)){
                    axisPixelsPerUnit = Math3D::Vec2(unitPt.x - originPt2.x, unitPt.y - originPt2.y).length();
                }
            }
        }
    }

    if(lmbPressed && allowInput && hoverHandle != Handle::None){
        dragging = true;
        activeHandle = hoverHandle;
        startMouse = mouse;
        screenAxis = axisDir;
        pixelsPerUnit = Math3D::Max(1.0f, axisPixelsPerUnit);
        startFov = fov;
        startFar = farPlane;
        startRadius = farHalfW;
        (void)handlePt;
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

        if(activeHandle == Handle::Fov){
            float radius = Math3D::Max(0.02f, startRadius + axisDeltaWorld);
            float newFov = 2.0f * std::atan(radius / Math3D::Max(startFar, nearPlane + 0.1f)) * (180.0f / Math3D::PI);
            settings.fov = Math3D::Clamp(newFov, 10.0f, 130.0f);
        }else if(activeHandle == Handle::FarPlane){
            settings.farPlane = Math3D::Max(nearPlane + 0.1f, startFar + axisDeltaWorld);
        }
        return true;
    }

    return hoverHandle != Handle::None;
}

void CameraWidget::draw(ImDrawList* drawList,
                        View* view,
                        PCamera viewportCamera,
                        const TransformWidget::Viewport& viewport,
                        const Math3D::Vec3& worldPos,
                        const Math3D::Vec3& worldForward,
                        const CameraSettings& settings) const{
    if(!drawList || !view || !viewportCamera || !viewport.valid){
        return;
    }

    Math3D::Vec3 forward, right, up;
    buildCameraBasis(worldForward, forward, right, up);

    float nearPlane = Math3D::Max(0.01f, settings.nearPlane);
    float farPlane = Math3D::Max(nearPlane + 0.1f, settings.farPlane);
    float aspect = Math3D::Max(0.01f, settings.aspect);

    if(!settings.isOrtho){
        float fov = Math3D::Clamp(settings.fov, 10.0f, 130.0f);
        float nearHalfH = nearPlane * std::tan(Math3D::PI * (fov * 0.5f) / 180.0f);
        float nearHalfW = nearHalfH * aspect;
        float farHalfH = farPlane * std::tan(Math3D::PI * (fov * 0.5f) / 180.0f);
        float farHalfW = farHalfH * aspect;

        Math3D::Vec3 nearC = worldPos + (forward * nearPlane);
        Math3D::Vec3 farC = worldPos + (forward * farPlane);

        Math3D::Vec3 ntr = nearC + right * nearHalfW + up * nearHalfH;
        Math3D::Vec3 ntl = nearC - right * nearHalfW + up * nearHalfH;
        Math3D::Vec3 nbr = nearC + right * nearHalfW - up * nearHalfH;
        Math3D::Vec3 nbl = nearC - right * nearHalfW - up * nearHalfH;

        Math3D::Vec3 ftr = farC + right * farHalfW + up * farHalfH;
        Math3D::Vec3 ftl = farC - right * farHalfW + up * farHalfH;
        Math3D::Vec3 fbr = farC + right * farHalfW - up * farHalfH;
        Math3D::Vec3 fbl = farC - right * farHalfW - up * farHalfH;

        drawWorldLine(drawList, view, viewportCamera, viewport, worldPos, ntr, kCameraOutline, 1.6f);
        drawWorldLine(drawList, view, viewportCamera, viewport, worldPos, ntl, kCameraOutline, 1.6f);
        drawWorldLine(drawList, view, viewportCamera, viewport, worldPos, nbr, kCameraOutline, 1.6f);
        drawWorldLine(drawList, view, viewportCamera, viewport, worldPos, nbl, kCameraOutline, 1.6f);

        drawWorldLine(drawList, view, viewportCamera, viewport, ntl, ntr, kCameraOutline, 1.4f);
        drawWorldLine(drawList, view, viewportCamera, viewport, ntr, nbr, kCameraOutline, 1.4f);
        drawWorldLine(drawList, view, viewportCamera, viewport, nbr, nbl, kCameraOutline, 1.4f);
        drawWorldLine(drawList, view, viewportCamera, viewport, nbl, ntl, kCameraOutline, 1.4f);

        drawWorldLine(drawList, view, viewportCamera, viewport, ftl, ftr, kCameraColor, 1.7f);
        drawWorldLine(drawList, view, viewportCamera, viewport, ftr, fbr, kCameraColor, 1.7f);
        drawWorldLine(drawList, view, viewportCamera, viewport, fbr, fbl, kCameraColor, 1.7f);
        drawWorldLine(drawList, view, viewportCamera, viewport, fbl, ftl, kCameraColor, 1.7f);

        drawWorldLine(drawList, view, viewportCamera, viewport, ntl, ftl, kCameraOutline, 1.3f);
        drawWorldLine(drawList, view, viewportCamera, viewport, ntr, ftr, kCameraOutline, 1.3f);
        drawWorldLine(drawList, view, viewportCamera, viewport, nbl, fbl, kCameraOutline, 1.3f);
        drawWorldLine(drawList, view, viewportCamera, viewport, nbr, fbr, kCameraOutline, 1.3f);

        ImVec2 fovPt;
        Math3D::Vec3 fovHandleWorld = farC + right * farHalfW;
        if(projectWorldPoint(view, viewportCamera, viewport, fovHandleWorld, fovPt)){
            drawList->AddCircleFilled(fovPt, kHandleRadius, kCameraColor);
        }
        ImVec2 farPt;
        if(projectWorldPoint(view, viewportCamera, viewport, farC, farPt)){
            drawList->AddCircleFilled(farPt, kHandleRadius, kCameraOutline);
        }
    }else{
        Math3D::Vec3 farC = worldPos + (forward * farPlane);
        drawWorldLine(drawList, view, viewportCamera, viewport, worldPos, farC, kCameraOutline, 1.8f);
        ImVec2 farPt;
        if(projectWorldPoint(view, viewportCamera, viewport, farC, farPt)){
            drawList->AddCircleFilled(farPt, kHandleRadius, kCameraOutline);
        }
    }

    drawBillboard(drawList, view, viewportCamera, viewport, worldPos, icon, kIconSizePx, IM_COL32(255, 255, 255, 255));
}
