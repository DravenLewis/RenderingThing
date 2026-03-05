#include "Editor/Widgets/BoundsWidget.h"

#include <cmath>
#include <limits>

#include "ECS/Core/ECSComponents.h"

namespace {
    constexpr ImU32 kBoundsColor = IM_COL32(255, 182, 54, 255);
    constexpr ImU32 kBoundsColorSoft = IM_COL32(255, 212, 122, 215);
    constexpr ImU32 kHandleColor = IM_COL32(255, 176, 42, 255);
    constexpr ImU32 kHandleHotColor = IM_COL32(255, 235, 148, 255);
    constexpr float kClipEpsilon = 1e-6f;

    float screenDistance(const ImVec2& a, const ImVec2& b){
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
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

    Math3D::Vec3 localToWorld(const Math3D::Mat4& worldMatrix, const Math3D::Vec3& localPoint){
        return Math3D::Transform::transformPoint(worldMatrix, localPoint);
    }

    void drawLocalLine(ImDrawList* drawList,
                       View* view,
                       PCamera camera,
                       const TransformWidget::Viewport& viewport,
                       const Math3D::Mat4& worldMatrix,
                       const Math3D::Vec3& aLocal,
                       const Math3D::Vec3& bLocal,
                       ImU32 color,
                       float thickness){
        if(!drawList){
            return;
        }
        Math3D::Vec3 aWorld = localToWorld(worldMatrix, aLocal);
        Math3D::Vec3 bWorld = localToWorld(worldMatrix, bLocal);
        ImVec2 a, b;
        if(clipWorldLineToScreen(view, camera, viewport, aWorld, bWorld, a, b)){
            drawList->AddLine(a, b, color, thickness);
        }
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

        constexpr int kSegments = 48;
        out.reserve(kSegments + 1);
        for(int i = 0; i <= kSegments; ++i){
            float t = (float)i / (float)kSegments;
            float angle = t * 2.0f * Math3D::PI;
            float c = Math3D::Cos(angle);
            float s = Math3D::Sin(angle);
            out.push_back(center + (u * c + v * s) * radius);
        }
    }

    float maxBoundsExtent(const BoundsComponent& bounds){
        switch(bounds.type){
            case BoundsType::Box:
                return Math3D::Max(Math3D::Max(std::fabs(bounds.size.x), std::fabs(bounds.size.y)), std::fabs(bounds.size.z));
            case BoundsType::Sphere:
                return std::fabs(bounds.radius);
            case BoundsType::Capsule: {
                float half = std::fabs(bounds.height) * 0.5f;
                return Math3D::Max(std::fabs(bounds.radius), half + std::fabs(bounds.radius));
            }
            default:
                break;
        }
        return 1.0f;
    }
}

void BoundsWidget::reset(){
    dragging = false;
    activeHandle = Handle::None;
    hoverHandle = Handle::None;
    dragScreenAxis = Math3D::Vec2(1.0f, 0.0f);
    dragPixelsPerUnit = 1.0f;
}

bool BoundsWidget::isOffsetHandle(Handle handle) const{
    return handle == Handle::OffsetX ||
           handle == Handle::OffsetY ||
           handle == Handle::OffsetZ;
}

void BoundsWidget::buildHandles(const BoundsComponent& bounds, std::vector<HandleCandidate>& out) const{
    out.clear();
    Math3D::Vec3 center = bounds.offset;
    float extent = Math3D::Max(0.25f, maxBoundsExtent(bounds));
    float offsetDistance = Math3D::Max(0.55f, extent * 1.25f + 0.2f);

    out.push_back({Handle::OffsetX, center + Math3D::Vec3(offsetDistance, 0.0f, 0.0f), Math3D::Vec3(1.0f, 0.0f, 0.0f)});
    out.push_back({Handle::OffsetY, center + Math3D::Vec3(0.0f, offsetDistance, 0.0f), Math3D::Vec3(0.0f, 1.0f, 0.0f)});
    out.push_back({Handle::OffsetZ, center + Math3D::Vec3(0.0f, 0.0f, offsetDistance), Math3D::Vec3(0.0f, 0.0f, 1.0f)});

    switch(bounds.type){
        case BoundsType::Box: {
            float sx = Math3D::Max(0.01f, std::fabs(bounds.size.x));
            float sy = Math3D::Max(0.01f, std::fabs(bounds.size.y));
            float sz = Math3D::Max(0.01f, std::fabs(bounds.size.z));
            out.push_back({Handle::BoxSizeX, center + Math3D::Vec3(sx, 0.0f, 0.0f), Math3D::Vec3(1.0f, 0.0f, 0.0f)});
            out.push_back({Handle::BoxSizeY, center + Math3D::Vec3(0.0f, sy, 0.0f), Math3D::Vec3(0.0f, 1.0f, 0.0f)});
            out.push_back({Handle::BoxSizeZ, center + Math3D::Vec3(0.0f, 0.0f, sz), Math3D::Vec3(0.0f, 0.0f, 1.0f)});
            break;
        }
        case BoundsType::Sphere: {
            float r = Math3D::Max(0.01f, std::fabs(bounds.radius));
            out.push_back({Handle::SphereRadiusX, center + Math3D::Vec3(r, 0.0f, 0.0f), Math3D::Vec3(1.0f, 0.0f, 0.0f)});
            out.push_back({Handle::SphereRadiusY, center + Math3D::Vec3(0.0f, r, 0.0f), Math3D::Vec3(0.0f, 1.0f, 0.0f)});
            out.push_back({Handle::SphereRadiusZ, center + Math3D::Vec3(0.0f, 0.0f, r), Math3D::Vec3(0.0f, 0.0f, 1.0f)});
            break;
        }
        case BoundsType::Capsule: {
            float r = Math3D::Max(0.01f, std::fabs(bounds.radius));
            float half = Math3D::Max(0.005f, std::fabs(bounds.height) * 0.5f);
            out.push_back({Handle::CapsuleRadiusX, center + Math3D::Vec3(r, 0.0f, 0.0f), Math3D::Vec3(1.0f, 0.0f, 0.0f)});
            out.push_back({Handle::CapsuleRadiusZ, center + Math3D::Vec3(0.0f, 0.0f, r), Math3D::Vec3(0.0f, 0.0f, 1.0f)});
            out.push_back({Handle::CapsuleHeight, center + Math3D::Vec3(0.0f, half + r, 0.0f), Math3D::Vec3(0.0f, 1.0f, 0.0f)});
            break;
        }
        default:
            break;
    }
}

bool BoundsWidget::update(View* view,
                          InputManager* input,
                          PCamera camera,
                          const TransformWidget::Viewport& viewport,
                          const Math3D::Mat4& worldMatrix,
                          BoundsComponent& bounds,
                          bool allowInput,
                          bool lmbPressed,
                          bool lmbDown,
                          bool lmbReleased){
    if(!view || !input || !camera || !viewport.valid){
        reset();
        return false;
    }

    Math3D::Vec2 mouse = input->getMousePosition();
    ImVec2 mousePt(mouse.x, mouse.y);

    std::vector<HandleCandidate> handles;
    buildHandles(bounds, handles);

    auto computeHandleScreenAxis = [&](const HandleCandidate& candidate,
                                       Math3D::Vec2& outAxisDir,
                                       float& outPixelsPerUnit) -> bool {
        Math3D::Vec3 anchorWorld = localToWorld(worldMatrix, candidate.localPos);
        Math3D::Vec3 unitWorld = localToWorld(worldMatrix, candidate.localPos + candidate.axisLocal);
        ImVec2 anchorPt(0.0f, 0.0f);
        ImVec2 unitPt(0.0f, 0.0f);
        if(!projectWorldPoint(view, camera, viewport, anchorWorld, anchorPt) ||
           !projectWorldPoint(view, camera, viewport, unitWorld, unitPt)){
            return false;
        }
        Math3D::Vec2 axis(unitPt.x - anchorPt.x, unitPt.y - anchorPt.y);
        float len = axis.length();
        if(len > Math3D::EPSILON){
            outAxisDir = Math3D::Vec2(axis.x / len, axis.y / len);
            outPixelsPerUnit = Math3D::Max(1.0f, len);
        }else{
            outAxisDir = Math3D::Vec2(1.0f, 0.0f);
            outPixelsPerUnit = 1.0f;
        }
        return true;
    };

    if(!dragging){
        hoverHandle = Handle::None;
        if(allowInput){
            float bestDist = std::numeric_limits<float>::max();
            for(const HandleCandidate& candidate : handles){
                ImVec2 handlePt(0.0f, 0.0f);
                if(!projectWorldPoint(view, camera, viewport, localToWorld(worldMatrix, candidate.localPos), handlePt)){
                    continue;
                }
                float dist = screenDistance(mousePt, handlePt);
                if(dist <= pickRadiusPx && dist < bestDist){
                    bestDist = dist;
                    hoverHandle = candidate.handle;
                }
            }
        }
    }

    if(lmbPressed && allowInput && hoverHandle != Handle::None){
        const HandleCandidate* selectedCandidate = nullptr;
        for(const HandleCandidate& candidate : handles){
            if(candidate.handle == hoverHandle){
                selectedCandidate = &candidate;
                break;
            }
        }
        if(selectedCandidate){
            Math3D::Vec2 axisDir(1.0f, 0.0f);
            float pxPerUnit = 1.0f;
            computeHandleScreenAxis(*selectedCandidate, axisDir, pxPerUnit);

            dragging = true;
            activeHandle = hoverHandle;
            startMouse = mouse;
            dragScreenAxis = axisDir;
            dragPixelsPerUnit = Math3D::Max(1.0f, pxPerUnit);
            startOffset = bounds.offset;
            startSize = bounds.size;
            startRadius = bounds.radius;
            startHeight = bounds.height;
            return true;
        }
    }

    if(dragging){
        hoverHandle = activeHandle;
        if(lmbReleased || !lmbDown){
            reset();
            return true;
        }

        Math3D::Vec2 delta(mouse.x - startMouse.x, mouse.y - startMouse.y);
        float axisDeltaPx = (delta.x * dragScreenAxis.x) + (delta.y * dragScreenAxis.y);
        float axisDeltaLocal = axisDeltaPx / Math3D::Max(1.0f, dragPixelsPerUnit);

        switch(activeHandle){
            case Handle::OffsetX:
                bounds.offset.x = startOffset.x + axisDeltaLocal;
                break;
            case Handle::OffsetY:
                bounds.offset.y = startOffset.y + axisDeltaLocal;
                break;
            case Handle::OffsetZ:
                bounds.offset.z = startOffset.z + axisDeltaLocal;
                break;
            case Handle::BoxSizeX:
                bounds.size.x = Math3D::Max(0.01f, startSize.x + axisDeltaLocal);
                break;
            case Handle::BoxSizeY:
                bounds.size.y = Math3D::Max(0.01f, startSize.y + axisDeltaLocal);
                break;
            case Handle::BoxSizeZ:
                bounds.size.z = Math3D::Max(0.01f, startSize.z + axisDeltaLocal);
                break;
            case Handle::SphereRadiusX:
            case Handle::SphereRadiusY:
            case Handle::SphereRadiusZ:
            case Handle::CapsuleRadiusX:
            case Handle::CapsuleRadiusZ:
                bounds.radius = Math3D::Max(0.01f, startRadius + axisDeltaLocal);
                break;
            case Handle::CapsuleHeight:
                bounds.height = Math3D::Max(0.01f, startHeight + axisDeltaLocal * 2.0f);
                break;
            default:
                break;
        }
        return true;
    }

    return hoverHandle != Handle::None;
}

void BoundsWidget::draw(ImDrawList* drawList,
                        View* view,
                        PCamera camera,
                        const TransformWidget::Viewport& viewport,
                        const Math3D::Mat4& worldMatrix,
                        const BoundsComponent& bounds) const{
    if(!drawList || !view || !camera || !viewport.valid){
        return;
    }

    const Math3D::Vec3 center = bounds.offset;

    switch(bounds.type){
        case BoundsType::Box: {
            Math3D::Vec3 ext(
                Math3D::Max(0.01f, std::fabs(bounds.size.x)),
                Math3D::Max(0.01f, std::fabs(bounds.size.y)),
                Math3D::Max(0.01f, std::fabs(bounds.size.z))
            );
            Math3D::Vec3 corners[8] = {
                center + Math3D::Vec3(-ext.x, -ext.y, -ext.z),
                center + Math3D::Vec3( ext.x, -ext.y, -ext.z),
                center + Math3D::Vec3(-ext.x,  ext.y, -ext.z),
                center + Math3D::Vec3( ext.x,  ext.y, -ext.z),
                center + Math3D::Vec3(-ext.x, -ext.y,  ext.z),
                center + Math3D::Vec3( ext.x, -ext.y,  ext.z),
                center + Math3D::Vec3(-ext.x,  ext.y,  ext.z),
                center + Math3D::Vec3( ext.x,  ext.y,  ext.z),
            };
            const int edgePairs[12][2] = {
                {0,1}, {1,3}, {3,2}, {2,0},
                {4,5}, {5,7}, {7,6}, {6,4},
                {0,4}, {1,5}, {2,6}, {3,7}
            };
            for(const auto& edge : edgePairs){
                drawLocalLine(drawList, view, camera, viewport, worldMatrix, corners[edge[0]], corners[edge[1]], kBoundsColor, 1.9f);
            }
            break;
        }
        case BoundsType::Sphere: {
            float radius = Math3D::Max(0.01f, std::fabs(bounds.radius));
            std::vector<Math3D::Vec3> ringPoints;
            for(const Math3D::Vec3& axis : {Math3D::Vec3(1,0,0), Math3D::Vec3(0,1,0), Math3D::Vec3(0,0,1)}){
                buildRingPoints(ringPoints, center, axis, radius);
                for(size_t i = 1; i < ringPoints.size(); ++i){
                    drawLocalLine(drawList, view, camera, viewport, worldMatrix, ringPoints[i - 1], ringPoints[i], kBoundsColor, 1.8f);
                }
            }
            break;
        }
        case BoundsType::Capsule: {
            float radius = Math3D::Max(0.01f, std::fabs(bounds.radius));
            float half = Math3D::Max(0.005f, std::fabs(bounds.height) * 0.5f);
            Math3D::Vec3 topCenter = center + Math3D::Vec3(0.0f, half, 0.0f);
            Math3D::Vec3 bottomCenter = center - Math3D::Vec3(0.0f, half, 0.0f);

            std::vector<Math3D::Vec3> ringPoints;
            buildRingPoints(ringPoints, topCenter, Math3D::Vec3(0,1,0), radius);
            for(size_t i = 1; i < ringPoints.size(); ++i){
                drawLocalLine(drawList, view, camera, viewport, worldMatrix, ringPoints[i - 1], ringPoints[i], kBoundsColor, 1.8f);
            }
            buildRingPoints(ringPoints, bottomCenter, Math3D::Vec3(0,1,0), radius);
            for(size_t i = 1; i < ringPoints.size(); ++i){
                drawLocalLine(drawList, view, camera, viewport, worldMatrix, ringPoints[i - 1], ringPoints[i], kBoundsColor, 1.8f);
            }

            drawLocalLine(drawList, view, camera, viewport, worldMatrix, topCenter + Math3D::Vec3( radius, 0.0f, 0.0f), bottomCenter + Math3D::Vec3( radius, 0.0f, 0.0f), kBoundsColorSoft, 1.7f);
            drawLocalLine(drawList, view, camera, viewport, worldMatrix, topCenter + Math3D::Vec3(-radius, 0.0f, 0.0f), bottomCenter + Math3D::Vec3(-radius, 0.0f, 0.0f), kBoundsColorSoft, 1.7f);
            drawLocalLine(drawList, view, camera, viewport, worldMatrix, topCenter + Math3D::Vec3(0.0f, 0.0f,  radius), bottomCenter + Math3D::Vec3(0.0f, 0.0f,  radius), kBoundsColorSoft, 1.7f);
            drawLocalLine(drawList, view, camera, viewport, worldMatrix, topCenter + Math3D::Vec3(0.0f, 0.0f, -radius), bottomCenter + Math3D::Vec3(0.0f, 0.0f, -radius), kBoundsColorSoft, 1.7f);

            buildRingPoints(ringPoints, topCenter, Math3D::Vec3(1,0,0), radius);
            for(size_t i = 1; i < ringPoints.size(); ++i){
                drawLocalLine(drawList, view, camera, viewport, worldMatrix, ringPoints[i - 1], ringPoints[i], kBoundsColorSoft, 1.4f);
            }
            buildRingPoints(ringPoints, bottomCenter, Math3D::Vec3(1,0,0), radius);
            for(size_t i = 1; i < ringPoints.size(); ++i){
                drawLocalLine(drawList, view, camera, viewport, worldMatrix, ringPoints[i - 1], ringPoints[i], kBoundsColorSoft, 1.4f);
            }
            break;
        }
        default:
            break;
    }

    std::vector<HandleCandidate> handles;
    buildHandles(bounds, handles);
    for(const HandleCandidate& candidate : handles){
        Math3D::Vec3 handleWorld = localToWorld(worldMatrix, candidate.localPos);
        ImVec2 handlePt(0.0f, 0.0f);
        if(!projectWorldPoint(view, camera, viewport, handleWorld, handlePt)){
            continue;
        }

        ImU32 handleColor = kHandleColor;
        const bool isHot = (candidate.handle == hoverHandle) || (dragging && candidate.handle == activeHandle);
        if(isHot){
            handleColor = kHandleHotColor;
        }

        if(isOffsetHandle(candidate.handle)){
            drawLocalLine(drawList, view, camera, viewport, worldMatrix, center, candidate.localPos, kBoundsColorSoft, 1.5f);
        }else{
            drawLocalLine(drawList, view, camera, viewport, worldMatrix, center, candidate.localPos, kBoundsColor, 1.4f);
        }
        drawList->AddCircleFilled(handlePt, handleRadiusPx, handleColor);
    }

    ImVec2 centerPt(0.0f, 0.0f);
    if(projectWorldPoint(view, camera, viewport, localToWorld(worldMatrix, center), centerPt)){
        drawList->AddCircleFilled(centerPt, 4.0f, kBoundsColorSoft);
    }
}
