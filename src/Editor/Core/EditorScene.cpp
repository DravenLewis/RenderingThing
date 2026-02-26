#include "Editor/Core/EditorScene.h"

// EditorScene is an editor host/wrapper scene: it renders and edits a contained target scene.
// The editor viewport navigation camera is owned by EditorScene; target-scene cameras are
// preview/edit targets and can be designated as the target scene's runtime current camera.

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

#include "ECS/Core/ECSComponents.h"
#include "Foundation/IO/File.h"
#include "Editor/Core/ImGuiLayer.h"
#include "Foundation/Logging/Logbot.h"
#include "Platform/Window/RenderWindow.h"
#include "Assets/Core/Asset.h"
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include "neoecs.hpp"
#include <cctype>

namespace {
    constexpr float kToolbarHeight = 32.0f;
    constexpr float kPanelGap = 4.0f;
    constexpr float kSplitterThickness = 6.0f;
    constexpr float kMinLeftPanelWidth = 180.0f;
    constexpr float kMinRightPanelWidth = 220.0f;
    constexpr float kMinCenterPanelWidth = 260.0f;
    constexpr float kMinBottomPanelHeight = 140.0f;
    constexpr float kMinTopPanelHeight = 180.0f;

    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;

    class EditorInputBlocker : public IEventHandler {
        public:
            explicit EditorInputBlocker(EditorScene* owner, std::function<bool()> shouldBlock)
                : owner(owner), shouldBlock(std::move(shouldBlock)) {}

            bool onKeyUp(int keyCode, InputManager&) override {
                if(owner && keyCode == SDL_SCANCODE_ESCAPE){
                    if(owner->handleQuitRequest()){
                        return true;
                    }
                }
                return shouldBlock();
            }

            bool onKeyDown(int keyCode, InputManager&) override {
                if(owner && keyCode == SDL_SCANCODE_ESCAPE){
                    if(owner->handleQuitRequest()){
                        return true;
                    }
                }
                return shouldBlock();
            }

            bool onMousePressed(int, InputManager&) override { return shouldBlock(); }
            bool onMouseReleased(int, InputManager&) override { return shouldBlock(); }
            bool onMouseMoved(int, int, InputManager&) override { return shouldBlock(); }
            bool onMouseScroll(float, InputManager&) override { return shouldBlock(); }

        private:
            EditorScene* owner = nullptr;
            std::function<bool()> shouldBlock;
    };

    bool screenPointInViewport(const Math3D::Vec3& screenPos, const TransformWidget::Viewport& viewport){
        if(screenPos.z < 0.0f || screenPos.z > 1.0f){
            return false;
        }
        if(screenPos.x < viewport.x || screenPos.x > (viewport.x + viewport.w)){
            return false;
        }
        if(screenPos.y < viewport.y || screenPos.y > (viewport.y + viewport.h)){
            return false;
        }
        return true;
    }

}

EditorScene::EditorScene(RenderWindow* window, PScene targetScene)
    : Scene(window),
      targetScene(std::move(targetScene)) {
}

EditorScene::EditorScene(RenderWindow* window, PScene targetScene, std::function<PScene(RenderWindow*)> factory)
    : Scene(window),
      targetScene(std::move(targetScene)),
      targetFactory(std::move(factory)) {
}

void EditorScene::init(){
    assetRoot = std::filesystem::path(File::GetCWD()) / "res";
    workspacePanel.setAssetRoot(assetRoot);
    ensureTargetInitialized();
}

void EditorScene::ensureTargetInitialized(){
    if(!targetScene || targetInitialized) return;

    if(getWindow()){
        targetScene->attachWindow(getWindow());
    }
    if(inputManager){
        targetScene->setInputManager(inputManager);
    }

    targetScene->init();
    targetInitialized = true;
    targetScene->setOutlineEnabled(true);

    auto mainScreen = targetScene->getMainScreen();
    if(mainScreen && !targetCamera){
        targetCamera = targetScene->getPreferredCamera();
        if(!targetCamera){
            targetCamera = mainScreen->getCamera();
        }
    }

    if(!editorCameraObject){
        editorCameraObject = createECSGameObject("EditorCamera");
        if(editorCameraObject){
            editorCameraObject->addComponent<TransformComponent>();
            editorCameraObject->addComponent<CameraComponent>();
            editorCameraObject->addComponent<BoundsComponent>();
        }
    }

    if(editorCameraObject && !editorCameraComponent){
        auto* manager = getECS()->getComponentManager();
        editorCameraTransform = manager->getECSComponent<TransformComponent>(editorCameraObject->gameobject());
        editorCameraComponent = manager->getECSComponent<CameraComponent>(editorCameraObject->gameobject());
        auto* boundsComp = manager->getECSComponent<BoundsComponent>(editorCameraObject->gameobject());
        if(editorCameraComponent && !editorCameraComponent->camera){
            RenderWindow* window = getWindow();
            float w = window ? (float)window->getWindowWidth() : 1280.0f;
            float h = window ? (float)window->getWindowHeight() : 720.0f;
            editorCameraComponent->camera = Camera::CreatePerspective(45.0f, Math3D::Vec2(w, h), 0.1f, 2000.0f);
        }

        if(editorCameraComponent){
            editorCamera = editorCameraComponent->camera;
            viewportCamera = editorCamera;
        }

        if(boundsComp){
            boundsComp->type = BoundsType::Sphere;
            boundsComp->radius = 0.5f;
        }

        if(targetCamera && editorCameraTransform){
            editorCameraTransform->local = targetCamera->transform();
            Math3D::Vec3 euler = targetCamera->transform().rotation.ToEuler();
            editorPitch = euler.x;
            editorYaw = euler.y;
        }
    }
}

void EditorScene::setInputManager(std::shared_ptr<InputManager> manager){
    inputManager = manager;

    if(inputManager && !inputBlockerRegistered){
        auto shouldBlock = [this](){
            ImGuiIO& io = ImGui::GetIO();
            if(playState != PlayState::Play) return true;
            if(inputManager && inputManager->getMouseCaptureMode() == MouseLockMode::LOCKED){
                return false;
            }
            if(isMouseInViewport()) return false;
            return io.WantCaptureMouse || io.WantCaptureKeyboard;
        };
        inputBlocker = std::make_shared<EditorInputBlocker>(this, shouldBlock);
        inputManager->addEventHandler(inputBlocker);
        inputBlockerRegistered = true;
    }

    if(targetScene){
        targetScene->setInputManager(manager);
    }
}

void EditorScene::update(float deltaTime){
    ensureTargetInitialized();
    if(!targetScene) return;

    if(playState == PlayState::Play){
        playModePanelRefreshAccum += deltaTime;
        if(playModePanelRefreshAccum >= playModePanelRefreshInterval){
            playModePanelRefreshAccum = 0.0f;
            playModeHeavyPanelsRefreshDue = true;
        }
    }else{
        playModePanelRefreshAccum = 0.0f;
        playModeHeavyPanelsRefreshDue = true;
    }

    if(auto preferred = targetScene->getPreferredCamera()){
        if(preferred != targetCamera){
            targetCamera = preferred;
        }
    }

    if(resetCompleted.exchange(false)){
        restoreSelectionAfterReset();
    }

    if(playState == PlayState::Play){
        if(maximizeOnPlay){
            if(auto* window = getWindow()){
                viewportRect.x = 0.0f;
                viewportRect.y = 0.0f;
                viewportRect.w = (float)window->getWindowWidth();
                viewportRect.h = (float)window->getWindowHeight();
                viewportRect.valid = (viewportRect.w > 1.0f && viewportRect.h > 1.0f);
                viewportHovered = true;
            }
        }

        if(auto* window = getWindow()){
            bool shouldConstrainViewportMouse = false;
            MouseLockMode mouseMode = MouseLockMode::FREE;
            if(inputManager){
                mouseMode = inputManager->getMouseCaptureMode();
                shouldConstrainViewportMouse = (mouseMode != MouseLockMode::FREE) && viewportRect.valid;
            }

            if(shouldConstrainViewportMouse){
                const int rectX = (int)std::floor(viewportRect.x);
                const int rectY = (int)std::floor(viewportRect.y);
                const int rectW = std::max(1, (int)std::ceil(viewportRect.w));
                const int rectH = std::max(1, (int)std::ceil(viewportRect.h));
                SDL_Rect mouseRect{rectX, rectY, rectW, rectH};
                SDL_SetWindowMouseRect(window->getWindowPtr(), &mouseRect);
                playViewportMouseRectConstrained = true;

                // For visible captured cursor mode, clamp immediately in case capture began outside the viewport.
                if(mouseMode == MouseLockMode::CAPTURED){
                    float mx = 0.0f;
                    float my = 0.0f;
                    SDL_GetMouseState(&mx, &my);
                    const float minX = (float)rectX;
                    const float minY = (float)rectY;
                    const float maxX = (float)(rectX + rectW - 1);
                    const float maxY = (float)(rectY + rectH - 1);
                    const float clampedX = Math3D::Clamp(mx, minX, maxX);
                    const float clampedY = Math3D::Clamp(my, minY, maxY);
                    if(!Math3D::AreClose(mx, clampedX) || !Math3D::AreClose(my, clampedY)){
                        SDL_WarpMouseInWindow(window->getWindowPtr(), clampedX, clampedY);
                    }
                }
            }else if(playViewportMouseRectConstrained){
                SDL_SetWindowMouseRect(window->getWindowPtr(), nullptr);
                playViewportMouseRectConstrained = false;
            }
        }

        viewportCamera = targetCamera ? targetCamera : editorCamera;
        if(auto mainScreen = targetScene->getMainScreen()){
            if(viewportCamera){
                mainScreen->setCamera(viewportCamera);
            }
        }
        bool mouseInViewport = isMouseInViewport();
        if(inputManager && inputManager->getMouseCaptureMode() != MouseLockMode::FREE){
            mouseInViewport = true;
        }
        ImGuiLayer::SetInputEnabled(!mouseInViewport);
        if(targetScene->consumeCloseRequest()){
            handleQuitRequest();
            return;
        }
        targetScene->updateECS(deltaTime);
        targetScene->update(deltaTime);
    }else{
        if(playViewportMouseRectConstrained){
            if(auto* window = getWindow()){
                SDL_SetWindowMouseRect(window->getWindowPtr(), nullptr);
            }
            playViewportMouseRectConstrained = false;
        }
        if(inputManager){
            bool rmb = inputManager->isRMBDown();
            bool rmbPressed = rmb && !prevRmb;
            prevRmb = rmb;
            bool mouseInViewport = isMouseInViewport();
            bool mouseOverPreviewWindow = false;
            if(playState == PlayState::Edit && previewCamera && viewportRect.valid){
                Math3D::Vec2 mousePos = inputManager->getMousePosition();
                float previewAbsX = viewportRect.x + previewWindowLocalPos.x;
                float previewAbsY = viewportRect.y + previewWindowLocalPos.y;
                float previewW = previewWindowSize.x;
                float previewH = previewWindowSize.y;
                mouseOverPreviewWindow =
                    (mousePos.x >= previewAbsX && mousePos.x <= (previewAbsX + previewW) &&
                     mousePos.y >= previewAbsY && mousePos.y <= (previewAbsY + previewH));
            }
            ImGuiIO& io = ImGui::GetIO();
            const bool uiOverlayCapturingMouse = io.WantCaptureMouse && !viewportHovered;
            bool mouseInViewportInteractive = mouseInViewport && !mouseOverPreviewWindow && !uiOverlayCapturingMouse;
            bool lmb = inputManager->isLMBDown();
            bool lmbPressed = lmb && !prevLmb;
            bool lmbReleased = !lmb && prevLmb;
            prevLmb = lmb;

            if(!rmb){
                editorCameraActive = false;
            }else if(rmbPressed && mouseInViewportInteractive){
                editorCameraActive = true;
            }

            bool allowControl = editorCameraActive && rmb;
            inputManager->setMouseCaptureMode(allowControl ? MouseLockMode::LOCKED : MouseLockMode::FREE);
            ImGuiLayer::SetInputEnabled(!allowControl);

            viewportCamera = editorCamera;

            if(!allowControl){
                inputManager->consumeMouseAxisDelta();
            }

            if(editorCamera && allowControl){
                focusActive = false;
                if(viewportRect.valid){
                    int cx = (int)(viewportRect.x + (viewportRect.w * 0.5f));
                    int cy = (int)(viewportRect.y + (viewportRect.h * 0.5f));
                    RenderWindow* window = getWindow();
                    if(window){
                        SDL_WarpMouseInWindow(window->getWindowPtr(), (float)cx, (float)cy);
                    }
                }
                Math3D::Vec2 delta = inputManager->consumeMouseAxisDelta();
                float dx = delta.x * editorLookSensitivity;
                float dy = -delta.y * editorLookSensitivity;

                editorYaw -= dx;
                editorPitch += dy;
                editorPitch = Math3D::Clamp(editorPitch, -89.0f, 89.0f);

                auto transform = editorCamera->transform();
                transform.rotation = Math3D::Quat::FromEuler(Math3D::Vec3(editorPitch, editorYaw, 0.0f));

                float moveSpeed = editorMoveSpeed;
                if(inputManager->isKeyDown(SDL_SCANCODE_LSHIFT)){
                    moveSpeed *= editorFastScale;
                }

                Math3D::Vec3 moveDir = Math3D::Vec3::zero();
                if(inputManager->isKeyDown(SDL_SCANCODE_W)) moveDir -= transform.forward();
                if(inputManager->isKeyDown(SDL_SCANCODE_S)) moveDir += transform.forward();
                if(inputManager->isKeyDown(SDL_SCANCODE_A)) moveDir -= transform.right();
                if(inputManager->isKeyDown(SDL_SCANCODE_D)) moveDir += transform.right();
                if(inputManager->isKeyDown(SDL_SCANCODE_LCTRL)) moveDir -= transform.up();
                if(inputManager->isKeyDown(SDL_SCANCODE_SPACE)) moveDir += transform.up();

                if(moveDir.length() > Math3D::EPSILON){
                    moveDir = moveDir.normalize();
                }

                Math3D::Vec3 desiredMoveVelocity = moveDir * moveSpeed;
                float moveAlpha = 1.0f - std::exp(-editorMoveSmoothing * deltaTime);
                editorMoveVelocity = Math3D::Lerp(editorMoveVelocity, desiredMoveVelocity, moveAlpha);
                transform.position += editorMoveVelocity * deltaTime;

                float scrollDelta = inputManager->consumeScrollDelta();
                if(!Math3D::AreClose(scrollDelta, 0.0f)){
                    float zoomImpulse = editorZoomImpulse;
                    if(inputManager->isKeyDown(SDL_SCANCODE_LSHIFT)){
                        zoomImpulse *= editorFastScale;
                    }
                    editorZoomVelocity += scrollDelta * zoomImpulse;
                }

                if(!Math3D::AreClose(editorZoomVelocity, 0.0f)){
                    transform.position -= transform.forward() * (editorZoomVelocity * deltaTime);
                }

                float zoomAlpha = 1.0f - std::exp(-editorZoomDamping * deltaTime);
                editorZoomVelocity = Math3D::Lerp(editorZoomVelocity, 0.0f, zoomAlpha);

                editorCamera->setTransform(transform);
            }

            if(editorCameraTransform && editorCamera){
                editorCameraTransform->local = editorCamera->transform();
            }

            if(!allowControl && playState == PlayState::Edit){
                bool wDown = inputManager->isKeyDown(SDL_SCANCODE_W);
                bool eDown = inputManager->isKeyDown(SDL_SCANCODE_E);
                bool rDown = inputManager->isKeyDown(SDL_SCANCODE_R);

                if(wDown && !prevKeyW){
                    transformWidget.setMode(TransformWidget::Mode::Translate);
                }
                if(eDown && !prevKeyE){
                    transformWidget.setMode(TransformWidget::Mode::Rotate);
                }
                if(rDown && !prevKeyR){
                    transformWidget.setMode(TransformWidget::Mode::Scale);
                }

                prevKeyW = wDown;
                prevKeyE = eDown;
                prevKeyR = rDown;
            }

            bool widgetConsumed = false;
            if(playState == PlayState::Edit && viewportCamera && targetScene && targetScene->getECS()){
                auto* entity = findEntityById(selectedEntityId); // Maybe refactor this later to allow light widgets to always draw on entities with light components.
                if(entity){
                    auto* components = targetScene->getECS()->getComponentManager();
                    if(auto* transformComp = components->getECSComponent<TransformComponent>(entity)){
                        Math3D::Mat4 world = buildWorldMatrix(entity, components);
                        Math3D::Vec3 worldPos = world.getPosition();
                        Math3D::Transform worldTx = Math3D::Transform::fromMat4(world);
                        Math3D::Vec3 worldForward = worldTx.forward();
                        TransformWidget::Viewport viewport{viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, viewportRect.valid};
                        widgetConsumed = transformWidget.update(
                            this,
                            inputManager.get(),
                            viewportCamera,
                            viewport,
                            worldPos,
                            transformComp->local,
                            mouseInViewportInteractive && !allowControl,
                            lmbPressed,
                            lmb,
                            lmbReleased
                        );
                        if(auto* lightComp = components->getECSComponent<LightComponent>(entity)){
                            if(!widgetConsumed){
                                widgetConsumed = lightWidget.update(
                                    this,
                                    inputManager.get(),
                                    viewportCamera,
                                    viewport,
                                    worldPos,
                                    worldForward,
                                    lightComp->light,
                                    lightComp->syncTransform,
                                    lightComp->syncDirection,
                                    mouseInViewportInteractive && !allowControl,
                                    lmbPressed,
                                    lmb,
                                    lmbReleased
                                );
                                if(lightComp->light.type == LightType::POINT){
                                    ensurePointLightBounds(entity, lightComp->light.range);
                                }
                            }
                        }
                        if(auto* cameraComp = components->getECSComponent<CameraComponent>(entity)){
                            if(!widgetConsumed && cameraComp->camera){
                                widgetConsumed = cameraWidget.update(
                                    this,
                                    inputManager.get(),
                                    viewportCamera,
                                    viewport,
                                    worldPos,
                                    worldForward,
                                    cameraComp->camera->getSettings(),
                                    mouseInViewportInteractive && !allowControl,
                                    lmbPressed,
                                    lmb,
                                    lmbReleased
                                );
                            }
                        }
                    }
                }
            }

            if(playState != PlayState::Play && lmbPressed && mouseInViewportInteractive && !allowControl && !widgetConsumed){
                if(viewportCamera){
                    Math3D::Vec2 mouse = inputManager->getMousePosition();
                    std::string picked = pickEntityIdAtScreen(mouse.x, mouse.y, viewportCamera);
                    if(!picked.empty()){
                        selectEntity(picked);
                    }else{
                        selectEntity("");
                    }
                }
            }
        }else{
            viewportCamera = editorCamera;
        }

        if(focusActive){
            if(viewportCamera && editorCamera && viewportCamera == editorCamera){
                auto transform = editorCamera->transform();
                Math3D::Vec3 desired = focusTarget - (focusForward * focusDistance);
                Math3D::Vec3 pos = transform.position;
                float t = Math3D::Clamp(deltaTime * focusSpeed, 0.0f, 1.0f);
                pos = pos + (desired - pos) * t;
                transform.setPosition(pos);
                editorCamera->setTransform(transform);
                if(editorCameraTransform){
                    editorCameraTransform->local = transform;
                }
            }else{
                focusActive = false;
            }
        }

        if(viewportCamera && viewportRect.valid){
            viewportCamera->resize(viewportRect.w, viewportRect.h);
        }

        if(auto mainScreen = targetScene->getMainScreen()){
            if(viewportCamera){
                mainScreen->setCamera(viewportCamera);
            }
        }

        targetScene->updateECS(0.0f);
    }
}

void EditorScene::render(){
    ensureTargetInitialized();
    if(resetRequested.exchange(false)){
        performStop();
        resetCompleted.store(true);
        return;
    }
    if(targetScene){
        if(playState == PlayState::Edit){
            previewCamera = resolveSelectedTargetCamera();
            auto mainScreen = targetScene->getMainScreen();
            NeoECS::ECSEntity* previewEntity = findEntityById(selectedEntityId);

            auto applyCameraEffects = [&](NeoECS::ECSEntity* cameraEntity){
                if(!mainScreen){
                    return;
                }
                mainScreen->clearEffects();
                if(!cameraEntity || !targetScene || !targetScene->getECS()){
                    return;
                }
                auto* components = targetScene->getECS()->getComponentManager();
                auto* camComponent = components->getECSComponent<CameraComponent>(cameraEntity);
                if(!camComponent || !camComponent->camera){
                    return;
                }
                const CameraSettings& settings = camComponent->camera->getSettings();
                if(auto* ssao = components->getECSComponent<SSAOComponent>(cameraEntity)){
                    if(auto effect = ssao->getEffectForCamera(settings)){
                        mainScreen->addEffect(effect);
                    }
                }
                if(auto* dof = components->getECSComponent<DepthOfFieldComponent>(cameraEntity)){
                    if(auto effect = dof->getEffectForCamera(settings)){
                        mainScreen->addEffect(effect);
                    }
                }
                if(auto* aa = components->getECSComponent<AntiAliasingComponent>(cameraEntity)){
                    if(auto effect = aa->getEffectForCamera(settings)){
                        mainScreen->addEffect(effect);
                    }
                }
            };

            // Render the selected target-scene camera into a small preview texture first.
            if(mainScreen && previewCamera && previewEntity){
                applyCameraEffects(previewEntity);
                mainScreen->setCamera(previewCamera);
                targetScene->render();
                auto sourceBuffer = mainScreen->getDisplayBuffer();
                if(sourceBuffer){
                    int srcW = sourceBuffer->getWidth();
                    int srcH = sourceBuffer->getHeight();
                    if(srcW > 0 && srcH > 0){
                        bool needsResize = !previewCaptureBuffer ||
                                           !previewTexture ||
                                           previewCaptureBuffer->getWidth() != srcW ||
                                           previewCaptureBuffer->getHeight() != srcH ||
                                           previewTexture->getWidth() != srcW ||
                                           previewTexture->getHeight() != srcH;
                        if(needsResize){
                            previewCaptureBuffer = FrameBuffer::Create(srcW, srcH);
                            previewTexture = Texture::CreateEmpty(srcW, srcH);
                            if(previewCaptureBuffer && previewTexture){
                                previewCaptureBuffer->attachTexture(previewTexture);
                            }
                        }

                        if(previewCaptureBuffer && previewTexture){
                            GLint prevReadFbo = 0;
                            GLint prevDrawFbo = 0;
                            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
                            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);

                            glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceBuffer->getID());
                            glReadBuffer(GL_COLOR_ATTACHMENT0);
                            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previewCaptureBuffer->getID());
                            glDrawBuffer(GL_COLOR_ATTACHMENT0);
                            glBlitFramebuffer(
                                0, 0, srcW, srcH,
                                0, 0, srcW, srcH,
                                GL_COLOR_BUFFER_BIT,
                                GL_NEAREST
                            );

                            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevReadFbo);
                            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDrawFbo);
                        }else{
                            previewTexture.reset();
                        }
                    }else{
                        previewTexture.reset();
                    }
                }else{
                    previewTexture.reset();
                }
            }else{
                previewTexture.reset();
            }

            // Keep the editor viewport render driven by the editor camera in edit mode.
            PCamera mainEditCamera = viewportCamera ? viewportCamera : editorCamera;
            if(mainScreen && mainEditCamera){
                mainScreen->clearEffects();
                mainScreen->setCamera(mainEditCamera);
            }
            targetScene->render();
        }else{
            previewTexture.reset();
            previewCamera.reset();
            targetScene->render();
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;
    const bool maximizeViewport = (maximizeOnPlay && playState == PlayState::Play);

    if(!maximizeViewport){
        drawToolbar(width, kToolbarHeight);
    }

    if(maximizeViewport){
        viewportRect.x = 0.0f;
        viewportRect.y = 0.0f;
        viewportRect.w = width;
        viewportRect.h = height;
        viewportRect.valid = (width > 1.0f && height > 1.0f);
        viewportHovered = true;
        return;
    }

    const float panelsTop = kToolbarHeight + kPanelGap;
    const float availableHeight = std::max(0.0f, height - panelsTop - kPanelGap);
    const float availableWidth = std::max(0.0f, width - (kSplitterThickness * 2.0f));

    float maxBottom = std::max(kMinBottomPanelHeight, availableHeight - kMinTopPanelHeight - kSplitterThickness);
    bottomPanelHeight = std::clamp(bottomPanelHeight, kMinBottomPanelHeight, maxBottom);
    float topPanelsHeight = availableHeight - bottomPanelHeight - kSplitterThickness;
    if(topPanelsHeight < kMinTopPanelHeight){
        topPanelsHeight = kMinTopPanelHeight;
        bottomPanelHeight = std::max(kMinBottomPanelHeight, availableHeight - topPanelsHeight - kSplitterThickness);
    }
    if(bottomPanelHeight < kMinBottomPanelHeight){
        bottomPanelHeight = kMinBottomPanelHeight;
        topPanelsHeight = std::max(0.0f, availableHeight - bottomPanelHeight - kSplitterThickness);
    }
    if(availableHeight < (kMinTopPanelHeight + kMinBottomPanelHeight + kSplitterThickness)){
        topPanelsHeight = std::max(80.0f, availableHeight * 0.6f);
        bottomPanelHeight = std::max(60.0f, availableHeight - topPanelsHeight - kSplitterThickness);
    }

    float maxLeft = std::max(kMinLeftPanelWidth, availableWidth - kMinRightPanelWidth - kMinCenterPanelWidth);
    leftPanelWidth = std::clamp(leftPanelWidth, kMinLeftPanelWidth, maxLeft);
    float maxRight = std::max(kMinRightPanelWidth, availableWidth - leftPanelWidth - kMinCenterPanelWidth);
    rightPanelWidth = std::clamp(rightPanelWidth, kMinRightPanelWidth, maxRight);

    float centerWidth = availableWidth - leftPanelWidth - rightPanelWidth;
    if(centerWidth < kMinCenterPanelWidth){
        float deficit = kMinCenterPanelWidth - centerWidth;
        float shrinkRight = std::min(deficit, std::max(0.0f, rightPanelWidth - kMinRightPanelWidth));
        rightPanelWidth -= shrinkRight;
        deficit -= shrinkRight;
        float shrinkLeft = std::min(deficit, std::max(0.0f, leftPanelWidth - kMinLeftPanelWidth));
        leftPanelWidth -= shrinkLeft;
        centerWidth = availableWidth - leftPanelWidth - rightPanelWidth;
    }
    if(availableWidth < (kMinLeftPanelWidth + kMinRightPanelWidth + kMinCenterPanelWidth)){
        leftPanelWidth = std::max(120.0f, availableWidth * 0.25f);
        rightPanelWidth = std::max(140.0f, availableWidth * 0.30f);
        centerWidth = std::max(80.0f, availableWidth - leftPanelWidth - rightPanelWidth);
        float total = leftPanelWidth + rightPanelWidth + centerWidth;
        if(total > availableWidth){
            float overflow = total - availableWidth;
            float trimRight = std::min(overflow, std::max(0.0f, rightPanelWidth - 80.0f));
            rightPanelWidth -= trimRight;
            overflow -= trimRight;
            float trimLeft = std::min(overflow, std::max(0.0f, leftPanelWidth - 80.0f));
            leftPanelWidth -= trimLeft;
            overflow -= trimLeft;
            centerWidth = std::max(80.0f, availableWidth - leftPanelWidth - rightPanelWidth);
        }
    }

    const float leftSplitterX = leftPanelWidth;
    const float viewportX = leftSplitterX + kSplitterThickness;
    const float rightSplitterX = viewportX + centerWidth;
    const float propertiesX = rightSplitterX + kSplitterThickness;
    const float bottomTop = panelsTop + topPanelsHeight + kSplitterThickness;

    // Note: immediate-mode placeholder swapping caused visible flicker (scrollbars/windows blinking)
    // in play mode. Keep panels rendering every frame and rely on internal panel/cache optimizations instead.
    bool renderHeavyPanels = true;
    playModeHeavyPanelsRefreshDue = false;

    drawEcsPanel(0.0f, panelsTop, leftPanelWidth, topPanelsHeight, !renderHeavyPanels);
    drawViewportPanel(viewportX, panelsTop, centerWidth, topPanelsHeight);
    drawPropertiesPanel(propertiesX, panelsTop, rightPanelWidth, topPanelsHeight, !renderHeavyPanels);
    drawAssetsPanel(0.0f, bottomTop, width, bottomPanelHeight, !renderHeavyPanels);

    auto drawSplitter = [](const char* windowId, const ImVec2& pos, const ImVec2& size, ImGuiMouseCursor cursor, bool vertical, float& value, float deltaSign, float minValue, float maxValue){
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin(windowId, nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoBringToFrontOnFocus
        );
        ImGui::InvisibleButton("splitter_btn", size);
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        if(hovered || active){
            ImGui::SetMouseCursor(cursor);
        }
        if(active){
            float delta = vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
            value = std::clamp(value + (delta * deltaSign), minValue, maxValue);
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 color = active ? IM_COL32(102, 160, 255, 255)
                             : (hovered ? IM_COL32(84, 124, 188, 210) : IM_COL32(56, 68, 92, 180));
        drawList->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), color, 1.0f);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    };

    drawSplitter("##LeftRightSplitter", ImVec2(leftSplitterX, panelsTop), ImVec2(kSplitterThickness, topPanelsHeight), ImGuiMouseCursor_ResizeEW, true, leftPanelWidth, +1.0f, kMinLeftPanelWidth, std::max(kMinLeftPanelWidth, availableWidth - kMinRightPanelWidth - kMinCenterPanelWidth));
    drawSplitter("##RightPropsSplitter", ImVec2(rightSplitterX, panelsTop), ImVec2(kSplitterThickness, topPanelsHeight), ImGuiMouseCursor_ResizeEW, true, rightPanelWidth, -1.0f, kMinRightPanelWidth, std::max(kMinRightPanelWidth, availableWidth - leftPanelWidth - kMinCenterPanelWidth));
    drawSplitter("##BottomSplitter", ImVec2(0.0f, panelsTop + topPanelsHeight), ImVec2(width, kSplitterThickness), ImGuiMouseCursor_ResizeNS, false, bottomPanelHeight, -1.0f, kMinBottomPanelHeight, std::max(kMinBottomPanelHeight, availableHeight - kMinTopPanelHeight - kSplitterThickness));
}

void EditorScene::drawToWindow(bool clearWindow, float, float, float, float){
    RenderWindow* window = getWindow();
    if(!window) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window->getWindowWidth(), window->getWindowHeight());

    if(clearWindow){
        glClearColor(0.08f, 0.09f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if(targetScene && viewportRect.valid){
        targetScene->drawToWindow(false, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
    }
}

void EditorScene::drawToolbar(float width, float height){
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));

    ImGui::Begin("##Toolbar", nullptr, kPanelFlags | ImGuiWindowFlags_NoTitleBar);

    bool playClicked = ImGui::Button(playState == PlayState::Play ? "Playing" : "Play");
    ImGui::SameLine();
    bool pauseClicked = ImGui::Button(playState == PlayState::Pause ? "Paused" : "Pause");
    ImGui::SameLine();
    bool stopClicked = ImGui::Button("Stop");

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(playState == PlayState::Edit ? "Edit Mode" : (playState == PlayState::Play ? "Play Mode" : "Paused"));

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::Checkbox("Maximize on play", &maximizeOnPlay);

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    auto modeButton = [&](const char* label, TransformWidget::Mode mode){
        bool active = (transformWidget.getMode() == mode);
        if(active){
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.45f, 0.6f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.5f, 0.68f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.4f, 0.55f, 1.0f));
        }
        if(ImGui::Button(label)){
            transformWidget.setMode(mode);
        }
        if(active){
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();
    };
    modeButton("All", TransformWidget::Mode::Combined);

    if(playClicked){
        if(playState == PlayState::Edit){
            storeSelectionForPlay();
            playState = PlayState::Play;
        }else if(playState == PlayState::Pause){
            playState = PlayState::Play;
        }
    }

    if(pauseClicked){
        if(playState == PlayState::Play){
            playState = PlayState::Pause;
        }else if(playState == PlayState::Pause){
            playState = PlayState::Play;
        }
    }

    if(stopClicked){
        if(playState != PlayState::Edit){
            resetRequested.store(true);
        }
    }

    ImGui::End();
}

bool EditorScene::isMouseInViewport() const{
    if(!inputManager || !viewportRect.valid) return false;
    Math3D::Vec2 mouse = inputManager->getMousePosition();
    return (mouse.x >= viewportRect.x && mouse.x <= (viewportRect.x + viewportRect.w) &&
            mouse.y >= viewportRect.y && mouse.y <= (viewportRect.y + viewportRect.h));
}

void EditorScene::selectEntity(const std::string& id){
    if(!id.empty() && id == selectedEntityId){
        focusOnEntity(id);
        return;
    }
    selectedEntityId = id;
    if(!id.empty()){
        selectedAssetPath.clear();
    }
    transformWidget.reset();
    cameraWidget.reset();
    if(targetScene){
        targetScene->setSelectedEntityId(id);
    }
}

void EditorScene::focusOnEntity(const std::string& id){
    PCamera focusCamera = viewportCamera ? viewportCamera : editorCamera;
    if(id.empty() || !targetScene || !focusCamera) return;

    auto* ecs = targetScene->getECS();
    if(!ecs) return;
    auto* entity = findEntityById(id);
    if(!entity) return;

    auto* components = ecs->getComponentManager();
    auto* transformComp = components->getECSComponent<TransformComponent>(entity);
    auto* boundsComp = components->getECSComponent<BoundsComponent>(entity);
    if(!transformComp) return;

    Math3D::Vec3 target = targetScene->getWorldPosition(entity);
    float radius = 1.0f;
    if(boundsComp){
        switch(boundsComp->type){
            case BoundsType::Box:
                radius = Math3D::Vec3(boundsComp->size.x, boundsComp->size.y, boundsComp->size.z).length();
                break;
            case BoundsType::Sphere:
                radius = boundsComp->radius;
                break;
            case BoundsType::Capsule:
                radius = boundsComp->radius + (boundsComp->height * 0.5f);
                break;
        }
    }else{
        float scale = (transformComp->local.scale.x + transformComp->local.scale.y + transformComp->local.scale.z) / 3.0f;
        radius = std::max(0.5f, scale);
    }

    float fov = focusCamera->getSettings().fov;
    float distance = radius / std::tan(glm::radians(fov * 0.5f));
    distance *= 1.5f;

    auto transform = focusCamera->transform();
    focusTarget = target;
    focusForward = transform.forward() * -1.0f;
    focusDistance = distance;
    focusActive = true;
}

std::string EditorScene::pickEntityIdAtScreen(float x, float y, PCamera cam){
    if(!targetScene || !cam || !viewportRect.valid) return "";

    Math3D::Vec3 nearPoint = screenToWorld(cam, x, y, 0.0f, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
    Math3D::Vec3 farPoint = screenToWorld(cam, x, y, 1.0f, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
    Math3D::Vec3 rayDir = (farPoint - nearPoint).normalize();

    auto* ecs = targetScene->getECS();
    if(!ecs) return "";
    auto* componentManager = ecs->getComponentManager();
    const auto& entities = ecs->getEntityManager()->getEntities();

    float bestT = std::numeric_limits<float>::max();
    std::string bestId;

    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(!entity) continue;
        auto* transform = componentManager->getECSComponent<TransformComponent>(entity);
        auto* renderer = componentManager->getECSComponent<MeshRendererComponent>(entity);
        auto* cameraComp = componentManager->getECSComponent<CameraComponent>(entity);
        auto* lightComp = componentManager->getECSComponent<LightComponent>(entity);
        auto* bounds = componentManager->getECSComponent<BoundsComponent>(entity);
        if(!transform) continue;
        if(!renderer && !cameraComp && !lightComp && !bounds) continue;

        Math3D::Vec3 pos = targetScene->getWorldPosition(entity);
        Math3D::Vec3 toPoint = pos - nearPoint;
        float t = Math3D::Vec3::dot(toPoint, rayDir);
        if(t < 0.0f) continue;

        Math3D::Vec3 closest = nearPoint + (rayDir * t);
        float dist = (pos - closest).length();
        float radius = 1.0f;
        if(bounds){
            switch(bounds->type){
                case BoundsType::Box:
                    radius = Math3D::Vec3(bounds->size.x, bounds->size.y, bounds->size.z).length();
                    break;
                case BoundsType::Sphere:
                    radius = bounds->radius;
                    break;
                case BoundsType::Capsule:
                    radius = bounds->radius + (bounds->height * 0.5f);
                    break;
            }
        }else{
            float scale = (transform->local.scale.x + transform->local.scale.y + transform->local.scale.z) / 3.0f;
            radius = std::max(0.5f, scale);
        }

        if(dist <= radius && t < bestT){
            bestT = t;
            bestId = entity->getNodeUniqueID();
        }
    }

    return bestId;
}

bool EditorScene::handleQuitRequest(){
    if(playState == PlayState::Play || playState == PlayState::Pause){
        LogBot.Log(LOG_WARN, "EditorScene::handleQuitRequest() -> Stop simulation");
        resetRequested.store(true);
        return true;
    }
    return false;
}

void EditorScene::performStop(){
    playState = PlayState::Edit;
    viewportRect = ViewportRect{};
    viewportHovered = false;
    previewWindowInitialized = false;
    if(targetFactory){
        if(targetScene){
            targetScene->dispose();
        }
        targetScene = targetFactory(getWindow());
        targetInitialized = false;
        targetCamera.reset();
        ensureTargetInitialized();
    }
    LogBot.Log(LOG_WARN, "EditorScene::performStop() -> Done");
}

void EditorScene::storeSelectionForPlay(){
    resetContext.hadSelection = !selectedEntityId.empty();
    resetContext.selectedId = selectedEntityId;
    if(resetContext.hadSelection){
        selectEntity("");
    }
    if(editorCamera){
        resetContext.hadCamera = true;
        resetContext.editorCameraTransform = editorCamera->transform();
    }
}

void EditorScene::restoreSelectionAfterReset(){
    if(resetContext.hadSelection && !resetContext.selectedId.empty()){
        selectEntity(resetContext.selectedId);
    }else{
        selectEntity("");
    }
    if(resetContext.hadCamera && editorCamera){
        editorCamera->setTransform(resetContext.editorCameraTransform);
        if(editorCameraTransform){
            editorCameraTransform->local = resetContext.editorCameraTransform;
        }
    }
    resetContext = ResetContext{};
}

void EditorScene::requestClose(){
    if(handleQuitRequest()){
        return;
    }
    Scene::requestClose();
}

bool EditorScene::consumeCloseRequest(){
    if(targetScene && targetScene->consumeCloseRequest()){
        LogBot.Log(LOG_WARN, "EditorScene::consumeCloseRequest() intercepted target close.");
        handleQuitRequest();
        // Swallow target close requests so the editor never quits.
        closeRequested.store(false, std::memory_order_relaxed);
        return false;
    }
    // Editor scene should only close itself if explicitly requested elsewhere.
    return Scene::consumeCloseRequest();
}

void EditorScene::drawEcsPanel(float x, float y, float w, float h, bool lightweight){
    if(lightweight){
        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::Begin("ECS Graph", nullptr, kPanelFlags);
        ImGui::TextDisabled("Panel throttled during play mode (10 Hz).");
        ImGui::End();
        return;
    }
    ecsViewPanel.draw(
        x,
        y,
        w,
        h,
        targetScene,
        selectedEntityId,
        [this](const std::string& id){
            selectEntity(id);
        }
    );
}

void EditorScene::drawViewportPanel(float x, float y, float w, float h){
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport", nullptr, kPanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    viewportHovered = ImGui::IsWindowHovered();
    viewportRect.x = pos.x;
    viewportRect.y = pos.y;
    viewportRect.w = size.x;
    viewportRect.h = size.y;
    viewportRect.valid = (size.x > 1.0f && size.y > 1.0f);

    if(playState == PlayState::Edit && targetScene && viewportCamera && viewportRect.valid && targetScene->getECS()){
        auto* ecs = targetScene->getECS();
        auto* components = ecs->getComponentManager();
        TransformWidget::Viewport viewport{viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, viewportRect.valid};
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ensureEditorIconsLoaded();
        auto lightColorTint = [](const Math3D::Vec4& c) -> ImU32 {
            int r = (int)Math3D::Clamp(c.x * 255.0f, 0.0f, 255.0f);
            int g = (int)Math3D::Clamp(c.y * 255.0f, 0.0f, 255.0f);
            int b = (int)Math3D::Clamp(c.z * 255.0f, 0.0f, 255.0f);
            int a = (int)Math3D::Clamp(c.w * 255.0f, 0.0f, 255.0f);
            return IM_COL32(r, g, b, a);
        };

        // Always render helper icons for invisible scene objects while editing.
        const auto& entities = ecs->getEntityManager()->getEntities();
        for(const auto& entityPtr : entities){
            auto* entity = entityPtr.get();
            if(!entity){
                continue;
            }
            auto* transformComp = components->getECSComponent<TransformComponent>(entity);
            if(!transformComp){
                continue;
            }
            auto* rendererComp = components->getECSComponent<MeshRendererComponent>(entity);
            auto* cameraComp = components->getECSComponent<CameraComponent>(entity);
            auto* lightComp = components->getECSComponent<LightComponent>(entity);
            bool isSelected = (entity->getNodeUniqueID() == selectedEntityId);
            Math3D::Vec3 worldPos = buildWorldMatrix(entity, components).getPosition();

            if(cameraComp && cameraComp->camera && !isSelected){
                drawBillboardIcon(drawList, worldPos, iconCamera, 56.0f, IM_COL32(255, 255, 255, 255));
            }

            if(lightComp && !isSelected){
                PTexture icon = iconLightPoint;
                if(lightComp->light.type == LightType::SPOT){
                    icon = iconLightSpot;
                }else if(lightComp->light.type == LightType::DIRECTIONAL){
                    icon = iconLightDirectional;
                }
                if(icon){
                    drawBillboardIcon(drawList, worldPos, icon, 56.0f, lightColorTint(lightComp->light.color));
                }
            }

            // Best-effort audio marker: if an entity is an editor-only helper with audio-style naming,
            // show the audio billboard in the editor viewport.
            if(iconAudio && !isSelected && !rendererComp && !cameraComp && !lightComp){
                std::string lowerName = entity->getName();
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c){
                    return (char)std::tolower(c);
                });
                if(lowerName.find("audio") != std::string::npos ||
                   lowerName.find("sound") != std::string::npos ||
                   lowerName.find("speaker") != std::string::npos){
                    drawBillboardIcon(drawList, worldPos, iconAudio, 56.0f, IM_COL32(255, 255, 255, 255));
                }
            }
        }

        auto* entity = findEntityById(selectedEntityId);
        if(entity){
            if(auto* transformComp = components->getECSComponent<TransformComponent>(entity)){
                Math3D::Mat4 world = buildWorldMatrix(entity, components);
                Math3D::Vec3 worldPos = world.getPosition();
                Math3D::Transform worldTx = Math3D::Transform::fromMat4(world);
                Math3D::Vec3 worldForward = worldTx.forward();

                transformWidget.draw(drawList, this, viewportCamera, viewport, worldPos, transformComp->local, viewportHovered);
                if(auto* cameraComp = components->getECSComponent<CameraComponent>(entity)){
                    if(cameraComp->camera){
                        cameraWidget.draw(
                            drawList,
                            this,
                            viewportCamera,
                            viewport,
                            worldPos,
                            worldForward,
                            cameraComp->camera->getSettings()
                        );
                    }
                }
                if(auto* lightComp = components->getECSComponent<LightComponent>(entity)){
                    lightWidget.draw(drawList, this, viewportCamera, viewport, worldPos, worldForward, lightComp->light, lightComp->syncTransform, lightComp->syncDirection);
                }
            }
        }

        if(previewTexture && previewTexture->getID() != 0){
            const float minPreviewW = 220.0f;
            const float minPreviewH = 160.0f;
            const float margin = 10.0f;
            float maxPreviewW = std::max(minPreviewW, viewportRect.w - (margin * 2.0f));
            float maxPreviewH = std::max(minPreviewH, viewportRect.h - (margin * 2.0f));

            if(!previewWindowInitialized){
                float defaultW = std::clamp(viewportRect.w * 0.32f, minPreviewW, std::min(380.0f, maxPreviewW));
                float defaultH = std::clamp((defaultW * (9.0f / 16.0f)) + 44.0f, minPreviewH, maxPreviewH);
                previewWindowSize = Math3D::Vec2(defaultW, defaultH);
                previewWindowLocalPos = Math3D::Vec2(viewportRect.w - defaultW - margin, 28.0f);
                previewWindowInitialized = true;
            }

            previewWindowSize.x = std::clamp(previewWindowSize.x, minPreviewW, maxPreviewW);
            previewWindowSize.y = std::clamp(previewWindowSize.y, minPreviewH, maxPreviewH);

            if(previewWindowPinned){
                previewWindowLocalPos.x = viewportRect.w - previewWindowSize.x - margin;
                previewWindowLocalPos.y = 28.0f;
            }

            float maxLocalX = std::max(0.0f, viewportRect.w - previewWindowSize.x);
            float maxLocalY = std::max(0.0f, viewportRect.h - previewWindowSize.y);
            previewWindowLocalPos.x = std::clamp(previewWindowLocalPos.x, 0.0f, maxLocalX);
            previewWindowLocalPos.y = std::clamp(previewWindowLocalPos.y, 0.0f, maxLocalY);

            ImVec2 windowPos(viewportRect.x + previewWindowLocalPos.x, viewportRect.y + previewWindowLocalPos.y);
            ImVec2 windowSize(previewWindowSize.x, previewWindowSize.y);

            ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(minPreviewW, minPreviewH),
                ImVec2(maxPreviewW, maxPreviewH)
            );

            ImGuiWindowFlags previewFlags =
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
            if(previewWindowPinned){
                previewFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
            }

            ImVec2 appliedPos = windowPos;
            ImVec2 appliedSize = windowSize;
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.05f, 0.08f, 0.92f));
            if(ImGui::Begin("Preview##ViewportPreviewWindow", nullptr, previewFlags)){
                bool pinned = previewWindowPinned;
                if(ImGui::Checkbox("Pin", &pinned)){
                    previewWindowPinned = pinned;
                }
                ImGui::Separator();

                ImVec2 avail = ImGui::GetContentRegionAvail();
                if(avail.x > 1.0f && avail.y > 1.0f){
                    float texW = (float)previewTexture->getWidth();
                    float texH = (float)previewTexture->getHeight();
                    if(texW <= 0.0f || texH <= 0.0f){
                        texW = 16.0f;
                        texH = 9.0f;
                    }
                    float texAspect = texW / texH;
                    float availAspect = avail.x / std::max(avail.y, 1.0f);
                    ImVec2 imageSize = avail;
                    if(availAspect > texAspect){
                        imageSize.x = avail.y * texAspect;
                    }else{
                        imageSize.y = avail.x / texAspect;
                    }

                    ImVec2 centered = ImGui::GetCursorPos();
                    if(avail.x > imageSize.x){
                        centered.x += (avail.x - imageSize.x) * 0.5f;
                    }
                    if(avail.y > imageSize.y){
                        centered.y += (avail.y - imageSize.y) * 0.5f;
                    }
                    ImGui::SetCursorPos(centered);

                    ImTextureID previewTexId = (ImTextureID)(intptr_t)previewTexture->getID();
                    ImGui::Image(previewTexId, imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                }
            }
            appliedPos = ImGui::GetWindowPos();
            appliedSize = ImGui::GetWindowSize();
            ImGui::End();
            ImGui::PopStyleColor();

            previewWindowLocalPos = Math3D::Vec2(appliedPos.x - viewportRect.x, appliedPos.y - viewportRect.y);
            previewWindowSize = Math3D::Vec2(appliedSize.x, appliedSize.y);
        }
    }

    ImGui::TextUnformatted("Viewport");
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorScene::drawPropertiesPanel(float x, float y, float w, float h, bool lightweight){
    if(lightweight){
        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::Begin("Properties", nullptr, kPanelFlags);
        ImGui::TextDisabled("Panel throttled during play mode (10 Hz).");
        ImGui::End();
        return;
    }
    workspacePanel.setAssetRoot(assetRoot);
    propertiesPanel.draw(
        x,
        y,
        w,
        h,
        targetScene,
        assetRoot,
        selectedAssetPath,
        selectedEntityId,
        [this](const std::string& id) -> NeoECS::ECSEntity* {
            return findEntityById(id);
        }
    );
}

void EditorScene::ensureEditorIconsLoaded(){
    if(editorIconsLoaded){
        return;
    }
    iconCamera = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/editor_icons/camera.png"));
    iconLightPoint = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/editor_icons/light_point.png"));
    iconLightSpot = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/editor_icons/light_spot.png"));
    iconLightDirectional = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/editor_icons/light_directional.png"));
    iconAudio = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/editor_icons/audio.png"));
    cameraWidget.setIcon(iconCamera);
    LightWidget::Icons icons;
    icons.point = iconLightPoint;
    icons.spot = iconLightSpot;
    icons.directional = iconLightDirectional;
    lightWidget.setIcons(icons);
    editorIconsLoaded = true;
}

void EditorScene::drawBillboardIcon(ImDrawList* drawList,
                                    const Math3D::Vec3& worldPos,
                                    PTexture texture,
                                    float sizePx,
                                    ImU32 tint) const{
    if(!drawList || !texture || !viewportCamera || !viewportRect.valid){
        return;
    }
    TransformWidget::Viewport viewport{viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, viewportRect.valid};
    Math3D::Vec3 screen = worldToScreen(viewportCamera, worldPos, viewport.x, viewport.y, viewport.w, viewport.h);
    if(!screenPointInViewport(screen, viewport)){
        return;
    }
    float half = sizePx * 0.5f;
    ImVec2 pmin(screen.x - half, screen.y - half);
    ImVec2 pmax(screen.x + half, screen.y + half);
    ImTextureID texId = (ImTextureID)(intptr_t)texture->getID();
    drawList->AddImage(texId, pmin, pmax, ImVec2(0, 0), ImVec2(1, 1), tint);
}

void EditorScene::ensurePointLightBounds(NeoECS::ECSEntity* entity, float radius){
    if(!entity || !targetScene || !targetScene->getECS()){
        return;
    }
    auto* manager = targetScene->getECS()->getComponentManager();
    if(auto* bounds = manager->getECSComponent<BoundsComponent>(entity)){
        bounds->type = BoundsType::Sphere;
        bounds->radius = radius;
    }else{
        auto* ctx = targetScene->getECS()->getContext();
        std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(ctx, entity));
        if(wrapper && wrapper->addComponent<BoundsComponent>()){
            if(auto* newBounds = manager->getECSComponent<BoundsComponent>(entity)){
                newBounds->type = BoundsType::Sphere;
                newBounds->radius = radius;
            }
        }
    }
}

void EditorScene::drawAssetsPanel(float x, float y, float w, float h, bool lightweight){
    if(lightweight){
        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::Begin("Workspace", nullptr, kPanelFlags);
        ImGui::TextDisabled("Panel throttled during play mode (10 Hz).");
        ImGui::TextDisabled("Hover panel to force full refresh.");
        ImGui::End();
        return;
    }
    workspacePanel.setAssetRoot(assetRoot);
    workspacePanel.draw(x, y, w, h, selectedAssetPath);
}

NeoECS::ECSEntity* EditorScene::findEntityById(const std::string& id) const{
    if(id.empty() || !targetScene || !targetScene->getECS()) return nullptr;
    auto* entityManager = targetScene->getECS()->getEntityManager();
    const auto& entities = entityManager->getEntities();
    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(entity && entity->getNodeUniqueID() == id){
            return entity;
        }
    }
    return nullptr;
}

PCamera EditorScene::resolveSelectedTargetCamera() const{
    if(!targetScene || !targetScene->getECS()){
        return nullptr;
    }
    auto* entity = findEntityById(selectedEntityId);
    if(!entity){
        return nullptr;
    }
    auto* components = targetScene->getECS()->getComponentManager();
    auto* cameraComp = components->getECSComponent<CameraComponent>(entity);
    if(!cameraComp || !cameraComp->camera){
        return nullptr;
    }
    return cameraComp->camera;
}

void EditorScene::dispose(){
    if(playViewportMouseRectConstrained){
        if(auto* window = getWindow()){
            SDL_SetWindowMouseRect(window->getWindowPtr(), nullptr);
        }
        playViewportMouseRectConstrained = false;
    }
    if(targetScene){
        targetScene->dispose();
    }
    Scene::dispose();
}
