#include "Editor/Core/EditorScene.h"

// EditorScene is an editor host/wrapper scene: it renders and edits a contained target scene.
// The editor viewport navigation camera is owned by EditorScene; target-scene cameras are
// preview/edit targets and can be designated as the target scene's runtime current camera.

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

#include "ECS/Core/ECSComponents.h"
#include "Foundation/IO/File.h"
#include "Editor/Core/EditorAssetUI.h"
#include "Editor/Core/ImGuiLayer.h"
#include "Foundation/Logging/Logbot.h"
#include "Foundation/Util/StringUtils.h"
#include "Platform/Window/RenderWindow.h"
#include "Assets/Core/Asset.h"
#include "Assets/Descriptors/SkyboxAsset.h"
#include "Scene/LoadedScene.h"
#include "Serialization/IO/PrefabIO.h"
#include "Serialization/IO/SceneIO.h"
#include "Serialization/Schema/ComponentSerializationRegistry.h"
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include "neoecs.hpp"
#include <cctype>
#include <system_error>

namespace {
    constexpr float kToolbarHeight = 64.0f;
    constexpr float kPanelGap = 4.0f;
    constexpr float kSplitterThickness = 6.0f;
    constexpr float kMinLeftPanelWidth = 180.0f;
    constexpr float kMinRightPanelWidth = 220.0f;
    constexpr float kMinCenterPanelWidth = 260.0f;
    constexpr float kMinBottomPanelHeight = 140.0f;
    constexpr float kMinTopPanelHeight = 180.0f;
    constexpr float kIoStatusDurationSeconds = 6.0f;

    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;

    bool endsWithIgnoreCase(const std::string& value, const std::string& suffix){
        return StringUtils::EndsWith(StringUtils::ToLowerCase(value), StringUtils::ToLowerCase(suffix));
    }

    std::string sanitizeFileStem(const std::string& value, const std::string& fallback){
        std::string out;
        out.reserve(value.size());
        for(char c : value){
            const unsigned char uc = static_cast<unsigned char>(c);
            if(std::isalnum(uc) || c == '_' || c == '-'){
                out.push_back(c);
            }else if(std::isspace(uc)){
                out.push_back('_');
            }
        }
        if(out.empty()){
            out = fallback;
        }
        return out;
    }

    bool isPathWithExtension(const std::filesystem::path& path, const std::string& extension){
        if(path.empty() || extension.empty()){
            return false;
        }
        return endsWithIgnoreCase(path.generic_string(), extension);
    }

    std::filesystem::path makeUniquePath(const std::filesystem::path& desiredPath){
        std::error_code ec;
        if(!std::filesystem::exists(desiredPath, ec)){
            return desiredPath;
        }

        const std::filesystem::path parent = desiredPath.parent_path();
        const std::string stem = desiredPath.stem().string();
        const std::string ext = desiredPath.extension().string();
        int suffix = 1;
        std::filesystem::path candidate;
        do{
            candidate = parent / (stem + "_" + std::to_string(suffix) + ext);
            suffix++;
        }while(std::filesystem::exists(candidate, ec));
        if(ec){
            return desiredPath;
        }
        return candidate;
    }

    void copyFixedString(char* dst, size_t dstSize, const std::string& value){
        if(!dst || dstSize == 0){
            return;
        }
        std::memset(dst, 0, dstSize);
        if(value.empty()){
            return;
        }
        std::strncpy(dst, value.c_str(), dstSize - 1);
        dst[dstSize - 1] = '\0';
    }

    bool pathWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root){
        if(path.empty() || root.empty()){
            return false;
        }

        std::error_code ec;
        std::filesystem::path normalizedRoot = std::filesystem::weakly_canonical(root, ec);
        if(ec){
            normalizedRoot = root.lexically_normal();
            ec.clear();
        }

        std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, ec);
        if(ec){
            normalizedPath = path.lexically_normal();
        }

        const std::filesystem::path rel = normalizedPath.lexically_relative(normalizedRoot);
        if(rel.empty()){
            return normalizedPath == normalizedRoot;
        }
        return !StringUtils::BeginsWith(rel.generic_string(), "..");
    }

    bool tryResolveValidSceneAssetPath(const PAsset& asset, const std::filesystem::path& assetRoot, std::filesystem::path& outPath){
        outPath.clear();
        if(!asset){
            return false;
        }

        std::unique_ptr<File>& fileHandle = asset->getFileHandle();
        if(!fileHandle || fileHandle->isInMemoryFile()){
            return false;
        }

        std::filesystem::path candidate = std::filesystem::path(fileHandle->getPath()).lexically_normal();
        if(candidate.empty() || !isPathWithExtension(candidate, ".scene") || !pathWithinRoot(candidate, assetRoot)){
            return false;
        }

        std::error_code ec;
        if(!std::filesystem::exists(candidate, ec) || ec){
            return false;
        }
        if(std::filesystem::is_directory(candidate, ec) || ec){
            return false;
        }

        outPath = candidate;
        return true;
    }

    std::string trimCopy(const std::string& value){
        return StringUtils::Trim(value);
    }

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

void EditorScene::setActiveScene(PScene scene){
    if(targetScene == scene){
        return;
    }

    cancelViewportPrefabDragPreview();

    if(targetScene){
        targetScene->dispose();
    }

    targetScene = std::move(scene);
    targetInitialized = false;
    targetFactory = nullptr;
    activeScenePath.clear();
    targetCamera.reset();
    viewportCamera = editorCamera;
    selectedEntityId.clear();
    transformWidget.reset();
    cameraWidget.reset();
    previewTexture.reset();
    previewCamera.reset();
    focusActive = false;
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
    applyActiveSceneState();
}

void EditorScene::applyActiveSceneState(){
    if(!targetScene){
        return;
    }

    targetInitialized = true;
    if(std::shared_ptr<LoadedScene> loadedScene = std::dynamic_pointer_cast<LoadedScene>(targetScene)){
        activeScenePath = loadedScene->getSourceScenePath();
    }
    targetScene->setOutlineEnabled(true);
    targetCamera = targetScene->getPreferredCamera();
    if(!targetCamera){
        if(auto mainScreen = targetScene->getMainScreen()){
            targetCamera = mainScreen->getCamera();
        }
    }
    selectedEntityId = targetScene->getSelectedEntityId();

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

    if(ioStatusTimeRemaining > 0.0f){
        ioStatusTimeRemaining = std::max(0.0f, ioStatusTimeRemaining - deltaTime);
        if(ioStatusTimeRemaining <= 0.0f){
            ioStatusMessage.clear();
            ioStatusIsError = false;
        }
    }

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

        targetScene->refreshRenderState();
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

                if(auto* skybox = components->getECSComponent<SkyboxComponent>(cameraEntity)){
                    if(skybox->skyboxAssetRef.empty()){
                        skybox->loadedSkyboxAssetRef.clear();
                        skybox->runtimeSkyBox.reset();
                        if(auto env = mainScreen->getEnvironment()){
                            env->setSkyBox(nullptr);
                        }
                    }else{
                        if(!skybox->runtimeSkyBox || skybox->loadedSkyboxAssetRef != skybox->skyboxAssetRef){
                            std::string error;
                            auto runtimeSkybox = SkyboxAssetIO::InstantiateSkyBoxFromRef(skybox->skyboxAssetRef, &error);
                            if(!runtimeSkybox){
                                if(!error.empty()){
                                    LogBot.Log(LOG_WARN, "Failed to load skybox '%s': %s", skybox->skyboxAssetRef.c_str(), error.c_str());
                                }
                                skybox->loadedSkyboxAssetRef.clear();
                                skybox->runtimeSkyBox.reset();
                            }else{
                                skybox->runtimeSkyBox = runtimeSkybox;
                                skybox->loadedSkyboxAssetRef = skybox->skyboxAssetRef;
                            }
                        }

                        if(auto env = mainScreen->getEnvironment()){
                            if(skybox->runtimeSkyBox){
                                env->setSkyBox(skybox->runtimeSkyBox);
                            }
                        }
                    }
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
                targetScene->renderViewportContents();
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
            targetScene->renderViewportContents();
        }else{
            previewTexture.reset();
            previewCamera.reset();
            targetScene->renderViewportContents();
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
    processDeferredToolbarCommands();
    drawSceneFileDialog();
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

void EditorScene::setIoStatus(const std::string& message, bool isError){
    ioStatusMessage = message;
    ioStatusIsError = isError;
    ioStatusTimeRemaining = kIoStatusDurationSeconds;

    if(isError){
        LogBot.Log(LOG_ERRO, "%s", message.c_str());
    }else{
        LogBot.Log(LOG_INFO, "%s", message.c_str());
    }
}

bool EditorScene::openSceneFileDialog(SceneFileDialogMode mode){
    if(mode == SceneFileDialogMode::None){
        return false;
    }
    if(playState != PlayState::Edit){
        const char* action = (mode == SceneFileDialogMode::SaveAs) ? "Save Scene As" : "Load Scene";
        setIoStatus(std::string(action) + " is only available in Edit mode.", true);
        return false;
    }
    if(mode == SceneFileDialogMode::SaveAs && (!targetScene || !targetScene->getECS())){
        setIoStatus("Save Scene As failed: target scene is unavailable.", true);
        return false;
    }

    sceneFileDialogState = SceneFileDialogState{};
    sceneFileDialogState.mode = mode;
    sceneFileDialogState.openRequested = true;
    sceneFileDialogState.focusNameInput = true;

    std::filesystem::path initialDir = workspacePanel.getCurrentDirectory();
    if(initialDir.empty()){
        initialDir = assetRoot;
    }

    std::filesystem::path preferredScenePath;
    if(isPathWithExtension(selectedAssetPath, ".scene")){
        preferredScenePath = selectedAssetPath.lexically_normal();
    }else if(isPathWithExtension(activeScenePath, ".scene")){
        preferredScenePath = activeScenePath.lexically_normal();
    }

    if(!selectedAssetPath.empty() && pathWithinRoot(selectedAssetPath, assetRoot)){
        std::error_code ec;
        if(std::filesystem::is_directory(selectedAssetPath, ec) && !ec){
            initialDir = selectedAssetPath.lexically_normal();
        }
    }

    if(!preferredScenePath.empty()){
        const std::filesystem::path preferredDir = preferredScenePath.parent_path();
        if(!preferredDir.empty() && pathWithinRoot(preferredDir, assetRoot)){
            initialDir = preferredDir;
        }
    }

    if(!pathWithinRoot(initialDir, assetRoot)){
        initialDir = assetRoot;
    }
    sceneFileDialogState.currentDirectory = initialDir.lexically_normal();

    if(!preferredScenePath.empty() && pathWithinRoot(preferredScenePath, assetRoot)){
        if(mode == SceneFileDialogMode::SaveAs){
            sceneFileDialogState.selectedPath = preferredScenePath;
        }else{
            std::error_code ec;
            if(std::filesystem::exists(preferredScenePath, ec) && !std::filesystem::is_directory(preferredScenePath, ec)){
                sceneFileDialogState.selectedPath = preferredScenePath;
            }
        }
    }

    std::string initialFileName;
    if(!sceneFileDialogState.selectedPath.empty()){
        initialFileName = sceneFileDialogState.selectedPath.filename().string();
    }
    if(initialFileName.empty() && !preferredScenePath.empty()){
        initialFileName = preferredScenePath.filename().string();
    }
    if(mode == SceneFileDialogMode::SaveAs && initialFileName.empty()){
        initialFileName = "Main.scene";
    }
    copyFixedString(sceneFileDialogState.fileNameBuffer, sizeof(sceneFileDialogState.fileNameBuffer), initialFileName);
    return true;
}

bool EditorScene::saveSceneFromEditorCommand(){
    pendingPlayAfterSceneSave = false;
    std::filesystem::path sourcePath = resolveCurrentSceneSourcePath();
    if(sourcePath.empty()){
        return openSceneFileDialog(SceneFileDialogMode::SaveAs);
    }
    return saveSceneToAbsolutePath(sourcePath);
}

bool EditorScene::saveSceneAsFromEditorCommand(){
    pendingPlayAfterSceneSave = false;
    return openSceneFileDialog(SceneFileDialogMode::SaveAs);
}

std::filesystem::path EditorScene::resolveCurrentSceneSourcePath() const{
    if(isPathWithExtension(activeScenePath, ".scene")){
        return activeScenePath.lexically_normal();
    }

    if(std::shared_ptr<LoadedScene> loadedScene = std::dynamic_pointer_cast<LoadedScene>(targetScene)){
        const std::filesystem::path sourcePath = loadedScene->getSourceScenePath();
        if(isPathWithExtension(sourcePath, ".scene")){
            return sourcePath.lexically_normal();
        }
    }

    return {};
}

void EditorScene::updateSceneSourceAfterSave(const std::filesystem::path& savePath){
    const std::filesystem::path normalizedPath = savePath.lexically_normal();
    activeScenePath = normalizedPath;
    AssetManager::Instance.unmanageAsset(normalizedPath.generic_string());
    PAsset sourceSceneAsset = AssetManager::Instance.getOrLoad(normalizedPath.generic_string());
    if(std::shared_ptr<LoadedScene> loadedScene = std::dynamic_pointer_cast<LoadedScene>(targetScene)){
        loadedScene->setSceneRefOrPath(normalizedPath.generic_string());
        loadedScene->setBaseDirectory(normalizedPath.parent_path());
        loadedScene->setSourceScenePath(normalizedPath);
        loadedScene->setSourceSceneAsset(sourceSceneAsset);
    }

    const std::filesystem::path sceneDirectory = normalizedPath.parent_path();
    targetFactory = [normalizedPath, sceneDirectory](RenderWindow* window) -> PScene {
        return std::make_shared<LoadedScene>(window, normalizedPath.generic_string(), sceneDirectory);
    };
}

bool EditorScene::beginPlayModeFromEditor(){
    if(playState == PlayState::Pause){
        playState = PlayState::Play;
        return true;
    }
    if(playState != PlayState::Edit){
        return false;
    }

    std::shared_ptr<LoadedScene> loadedScene = std::dynamic_pointer_cast<LoadedScene>(targetScene);
    std::filesystem::path sourcePath;
    if(loadedScene && tryResolveValidSceneAssetPath(loadedScene->getSourceSceneAsset(), assetRoot, sourcePath)){
        if(!saveSceneToAbsolutePath(sourcePath)){
            return false;
        }

        return enterPlayModeFromScenePath(sourcePath);
    }

    pendingPlayAfterSceneSave = false;
    if(!openSceneFileDialog(SceneFileDialogMode::SaveAs)){
        return false;
    }

    pendingPlayAfterSceneSave = true;
    return false;
}

bool EditorScene::enterPlayModeFromScenePath(const std::filesystem::path& scenePath){
    if(scenePath.empty()){
        return false;
    }

    storeSelectionForPlay();
    if(!loadSceneFromAbsolutePath(scenePath)){
        restoreSelectionAfterReset();
        return false;
    }

    playState = PlayState::Play;
    return true;
}

void EditorScene::processDeferredToolbarCommands(){
    if(ImGui::IsAnyItemActive()){
        return;
    }

    if(pendingToolbarSaveSceneCommand){
        pendingToolbarSaveSceneCommand = false;
        saveSceneFromEditorCommand();
    }

    if(pendingToolbarSaveSceneAsCommand){
        pendingToolbarSaveSceneAsCommand = false;
        saveSceneAsFromEditorCommand();
    }

    if(pendingToolbarLoadSceneCommand){
        pendingToolbarLoadSceneCommand = false;
        loadSceneFromEditorCommand();
    }

    if(pendingToolbarPlayCommand){
        pendingToolbarPlayCommand = false;
        beginPlayModeFromEditor();
    }
}

bool EditorScene::saveSceneToAbsolutePath(const std::filesystem::path& savePath){
    if(playState != PlayState::Edit){
        setIoStatus("Save Scene is only available in Edit mode.", true);
        return false;
    }
    if(!targetScene || !targetScene->getECS()){
        setIoStatus("Save Scene failed: target scene is unavailable.", true);
        return false;
    }

    if(savePath.empty()){
        setIoStatus("Save Scene failed: choose a valid scene file name.", true);
        return false;
    }

    std::filesystem::path normalizedPath = savePath.lexically_normal();
    if(!isPathWithExtension(normalizedPath, ".scene")){
        normalizedPath += ".scene";
    }
    if(normalizedPath.filename().empty()){
        setIoStatus("Save Scene failed: choose a valid scene file name.", true);
        return false;
    }

    std::filesystem::path saveDirectory = normalizedPath.parent_path();
    if(saveDirectory.empty()){
        saveDirectory = assetRoot;
        normalizedPath = saveDirectory / normalizedPath.filename();
    }
    if(!pathWithinRoot(saveDirectory, assetRoot)){
        setIoStatus("Save Scene failed: scene files must stay inside the asset workspace.", true);
        return false;
    }

    std::error_code ec;
    if(!std::filesystem::exists(saveDirectory, ec) || !std::filesystem::is_directory(saveDirectory, ec)){
        setIoStatus("Save Scene failed: target directory does not exist.", true);
        return false;
    }

    SceneIO::SceneSaveOptions options;
    options.registry = &Serialization::DefaultComponentSerializationRegistry();
    options.metadata.name = normalizedPath.stem().string();
    if(options.metadata.name.empty()){
        options.metadata.name = "Scene";
    }

    std::string error;
    if(!SceneIO::SaveSceneToAbsolutePath(targetScene, normalizedPath, options, &error)){
        setIoStatus("Save Scene failed: " + error, true);
        return false;
    }

    updateSceneSourceAfterSave(normalizedPath);
    selectedAssetPath = normalizedPath;
    setIoStatus("Scene saved: " + normalizedPath.generic_string(), false);
    return true;
}

bool EditorScene::loadSceneFromEditorCommand(){
    pendingPlayAfterSceneSave = false;
    return openSceneFileDialog(SceneFileDialogMode::Load);
}

bool EditorScene::loadSceneFromAbsolutePath(const std::filesystem::path& loadPath){
    if(playState != PlayState::Edit){
        setIoStatus("Load Scene is only available in Edit mode.", true);
        return false;
    }

    cancelViewportPrefabDragPreview();

    if(loadPath.empty()){
        setIoStatus("Load Scene failed: choose a .scene file.", true);
        return false;
    }

    std::filesystem::path normalizedPath = loadPath.lexically_normal();
    if(!isPathWithExtension(normalizedPath, ".scene")){
        normalizedPath += ".scene";
    }
    if(!pathWithinRoot(normalizedPath, assetRoot)){
        setIoStatus("Load Scene failed: scene files must stay inside the asset workspace.", true);
        return false;
    }

    std::error_code ec;
    if(!std::filesystem::exists(normalizedPath, ec) || std::filesystem::is_directory(normalizedPath, ec)){
        setIoStatus("Load Scene failed: scene file does not exist.", true);
        return false;
    }

    const std::filesystem::path sceneDirectory = normalizedPath.parent_path();
    auto loadedSceneFactory = [normalizedPath, sceneDirectory](RenderWindow* window) -> PScene {
        return std::make_shared<LoadedScene>(window, normalizedPath.generic_string(), sceneDirectory);
    };

    std::shared_ptr<LoadedScene> loadedScene = std::dynamic_pointer_cast<LoadedScene>(loadedSceneFactory(getWindow()));
    if(!loadedScene){
        setIoStatus("Load Scene failed: could not create loaded scene instance.", true);
        return false;
    }

    if(getWindow()){
        loadedScene->attachWindow(getWindow());
    }
    if(inputManager){
        loadedScene->setInputManager(inputManager);
    }
    loadedScene->init();
    if(!loadedScene->didLoadSuccessfully()){
        const std::string error = loadedScene->getLastLoadError().empty()
            ? std::string("Unknown load error.")
            : loadedScene->getLastLoadError();
        loadedScene->dispose();
        setIoStatus("Load Scene failed: " + error, true);
        return false;
    }

    setActiveScene(loadedScene);
    targetInitialized = true;
    targetFactory = loadedSceneFactory;
    applyActiveSceneState();
    activeScenePath = normalizedPath;
    selectedAssetPath = normalizedPath;
    setIoStatus("Scene loaded: " + normalizedPath.generic_string(), false);
    return true;
}

void EditorScene::drawSceneFileDialog(){
    if(sceneFileDialogState.mode == SceneFileDialogMode::None){
        return;
    }

    constexpr const char* kPopupId = "Scene File Dialog##EditorScene";
    if(sceneFileDialogState.openRequested){
        ImGui::OpenPopup(kPopupId);
        sceneFileDialogState.openRequested = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if(viewport){
        const float modalWidth = std::clamp(viewport->Size.x * 0.46f, 540.0f, 900.0f);
        const float modalHeight = std::clamp(viewport->Size.y * 0.58f, 360.0f, 680.0f);
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(modalWidth, modalHeight), ImGuiCond_Appearing);
    }

    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.82f));
    if(!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)){
        if(!ImGui::IsPopupOpen(kPopupId)){
            sceneFileDialogState = SceneFileDialogState{};
            pendingPlayAfterSceneSave = false;
        }
        ImGui::PopStyleColor();
        return;
    }

    if(sceneFileDialogState.currentDirectory.empty() || !pathWithinRoot(sceneFileDialogState.currentDirectory, assetRoot)){
        sceneFileDialogState.currentDirectory = assetRoot;
    }

    const bool isSaveMode = (sceneFileDialogState.mode == SceneFileDialogMode::SaveAs);
    ImGui::TextUnformatted(isSaveMode ? "Save Scene As" : "Load Scene");
    ImGui::TextDisabled("Directory: %s", sceneFileDialogState.currentDirectory.generic_string().c_str());

    if(!sceneFileDialogState.errorMessage.empty()){
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", sceneFileDialogState.errorMessage.c_str());
        ImGui::PopStyleColor();
    }

    if(ImGui::Button("Root")){
        sceneFileDialogState.currentDirectory = assetRoot;
        sceneFileDialogState.selectedPath.clear();
        sceneFileDialogState.errorMessage.clear();
    }
    ImGui::SameLine();
    const bool canGoUp =
        !sceneFileDialogState.currentDirectory.empty() &&
        sceneFileDialogState.currentDirectory != assetRoot &&
        pathWithinRoot(sceneFileDialogState.currentDirectory.parent_path(), assetRoot);
    ImGui::BeginDisabled(!canGoUp);
    if(ImGui::Button("Up") && canGoUp){
        sceneFileDialogState.currentDirectory = sceneFileDialogState.currentDirectory.parent_path();
        sceneFileDialogState.selectedPath.clear();
        sceneFileDialogState.errorMessage.clear();
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    const float footerReserve = ImGui::GetFrameHeight() * (isSaveMode ? 4.8f : 4.0f);
    ImGui::BeginChild("SceneFileDialogEntries", ImVec2(0.0f, -footerReserve), true);
    if(std::filesystem::exists(sceneFileDialogState.currentDirectory)){
        struct Entry {
            std::filesystem::path path;
            bool isDirectory = false;
        };

        std::vector<Entry> entries;
        std::error_code ec;
        for(const auto& dirEntry : std::filesystem::directory_iterator(
                sceneFileDialogState.currentDirectory,
                std::filesystem::directory_options::skip_permission_denied,
                ec)){
            const std::filesystem::path entryPath = dirEntry.path().lexically_normal();
            const bool isDirectory = dirEntry.is_directory();
            if(isDirectory){
                entries.push_back({entryPath, true});
                continue;
            }
            if(isPathWithExtension(entryPath, ".scene")){
                entries.push_back({entryPath, false});
            }
        }

        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b){
            if(a.isDirectory != b.isDirectory){
                return a.isDirectory > b.isDirectory;
            }
            return a.path.filename().string() < b.path.filename().string();
        });

        for(const Entry& entry : entries){
            std::string label = entry.path.filename().string();
            if(entry.isDirectory){
                label = "[DIR] " + label;
            }

            const bool isSelected = (!entry.isDirectory && sceneFileDialogState.selectedPath == entry.path);
            if(ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)){
                sceneFileDialogState.errorMessage.clear();
                if(entry.isDirectory){
                    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){
                        sceneFileDialogState.currentDirectory = entry.path;
                    }
                    sceneFileDialogState.selectedPath.clear();
                }else{
                    sceneFileDialogState.selectedPath = entry.path;
                    copyFixedString(
                        sceneFileDialogState.fileNameBuffer,
                        sizeof(sceneFileDialogState.fileNameBuffer),
                        entry.path.filename().string()
                    );
                }
            }
        }
    }else{
        ImGui::TextDisabled("Directory missing.");
    }
    ImGui::EndChild();

    bool submitRequested = false;
    if(sceneFileDialogState.focusNameInput){
        ImGui::SetKeyboardFocusHere();
        sceneFileDialogState.focusNameInput = false;
    }
    if(ImGui::InputText("Scene File", sceneFileDialogState.fileNameBuffer, sizeof(sceneFileDialogState.fileNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)){
        submitRequested = true;
    }

    auto buildCandidatePath = [&]() -> std::filesystem::path {
        std::string fileName = trimCopy(sceneFileDialogState.fileNameBuffer);
        if(fileName.empty() && !sceneFileDialogState.selectedPath.empty()){
            fileName = sceneFileDialogState.selectedPath.filename().string();
        }
        if(fileName.empty()){
            return {};
        }

        std::filesystem::path candidate = sceneFileDialogState.currentDirectory / fileName;
        if(!isPathWithExtension(candidate, ".scene")){
            candidate += ".scene";
        }
        return candidate.lexically_normal();
    };

    const std::filesystem::path candidatePath = buildCandidatePath();
    if(!candidatePath.empty()){
        ImGui::TextDisabled("%s", candidatePath.generic_string().c_str());
    }else{
        ImGui::TextDisabled("%s", isSaveMode ? "Enter a scene file name." : "Select or enter a .scene file.");
    }

    const bool canConfirm = !candidatePath.empty();
    if(!canConfirm){
        ImGui::BeginDisabled();
    }
    if(ImGui::Button(isSaveMode ? "Save As" : "Load") || (submitRequested && canConfirm)){
        sceneFileDialogState.errorMessage.clear();
        bool success = false;
        if(isSaveMode){
            success = saveSceneToAbsolutePath(candidatePath);
        }else{
            success = loadSceneFromAbsolutePath(candidatePath);
        }

        if(success){
            if(isSaveMode && pendingPlayAfterSceneSave){
                success = enterPlayModeFromScenePath(candidatePath);
            }
        }

        if(success){
            pendingPlayAfterSceneSave = false;
            ImGui::CloseCurrentPopup();
            sceneFileDialogState = SceneFileDialogState{};
            ImGui::EndPopup();
            ImGui::PopStyleColor();
            return;
        }

        if(sceneFileDialogState.errorMessage.empty()){
            sceneFileDialogState.errorMessage = ioStatusMessage.empty()
                ? (isSaveMode ? "Save Scene As failed." : "Load Scene failed.")
                : ioStatusMessage;
        }
    }
    if(!canConfirm){
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if(ImGui::Button("Cancel")){
        pendingPlayAfterSceneSave = false;
        ImGui::CloseCurrentPopup();
        sceneFileDialogState = SceneFileDialogState{};
        ImGui::EndPopup();
        ImGui::PopStyleColor();
        return;
    }

    ImGui::EndPopup();
    ImGui::PopStyleColor();
}

bool EditorScene::exportEntityAsPrefabToDirectory(const std::string& entityId, const std::filesystem::path& directoryPath){
    if(playState != PlayState::Edit){
        setIoStatus("Create Prefab is only available in Edit mode.", true);
        return false;
    }
    if(!targetScene || !targetScene->getECS()){
        setIoStatus("Create Prefab failed: target scene is unavailable.", true);
        return false;
    }

    NeoECS::ECSEntity* entity = findEntityById(entityId);
    if(!entity){
        setIoStatus("Create Prefab failed: selected entity was not found.", true);
        return false;
    }
    if(targetScene->isSceneRootEntity(entity)){
        setIoStatus("Create Prefab failed: scene root cannot be exported.", true);
        return false;
    }

    std::filesystem::path exportDirectory = directoryPath;
    if(exportDirectory.empty()){
        exportDirectory = assetRoot;
    }
    std::error_code ec;
    if(!std::filesystem::exists(exportDirectory, ec)){
        if(!std::filesystem::create_directories(exportDirectory, ec)){
            setIoStatus("Create Prefab failed: could not create export directory.", true);
            return false;
        }
    }else if(!std::filesystem::is_directory(exportDirectory, ec)){
        setIoStatus("Create Prefab failed: export path is not a directory.", true);
        return false;
    }

    exportDirectory = exportDirectory.lexically_normal();
    const std::string safeStem = sanitizeFileStem(entity->getName(), "Prefab");
    std::filesystem::path prefabPath = makeUniquePath(exportDirectory / (safeStem + ".prefab"));

    PrefabIO::PrefabSaveOptions options;
    options.registry = &Serialization::DefaultComponentSerializationRegistry();
    options.metadata.name = entity->getName().empty() ? safeStem : entity->getName();

    std::string error;
    if(!PrefabIO::SaveEntitySubtreeToAbsolutePath(targetScene, entity, prefabPath, options, &error)){
        setIoStatus("Create Prefab failed: " + error, true);
        return false;
    }

    selectedAssetPath = prefabPath;
    setIoStatus("Prefab created: " + prefabPath.generic_string(), false);
    return true;
}

bool EditorScene::exportEntityAsPrefabToWorkspaceDirectory(const std::string& entityId){
    return exportEntityAsPrefabToDirectory(entityId, workspacePanel.getCurrentDirectory());
}

bool EditorScene::instantiatePrefabFromAbsolutePath(const std::filesystem::path& prefabPath,
                                                    NeoECS::GameObject* parentObject,
                                                    PrefabIO::PrefabInstantiateResult* outResult,
                                                    std::string* outError){
    if(!targetScene || !targetScene->getECS()){
        if(outError){
            *outError = "Target scene is unavailable.";
        }
        return false;
    }
    if(!isPathWithExtension(prefabPath, ".prefab")){
        if(outError){
            *outError = "Dropped asset is not a .prefab file.";
        }
        return false;
    }

    std::error_code ec;
    if(!std::filesystem::exists(prefabPath, ec) || std::filesystem::is_directory(prefabPath, ec)){
        if(outError){
            *outError = "Prefab file does not exist.";
        }
        return false;
    }

    PrefabIO::PrefabInstantiateOptions options;
    options.parent = parentObject;
    options.registry = &Serialization::DefaultComponentSerializationRegistry();
    if(!PrefabIO::InstantiateFromAbsolutePath(targetScene, prefabPath, options, outResult, outError)){
        return false;
    }
    if(outResult && outResult->rootObjects.empty()){
        if(outError){
            *outError = "Prefab instantiated with no root objects.";
        }
        return false;
    }
    return true;
}

bool EditorScene::instantiatePrefabUnderParentEntity(const std::filesystem::path& prefabPath, const std::string& parentEntityId){
    if(playState != PlayState::Edit){
        setIoStatus("Instantiate Prefab is only available in Edit mode.", true);
        return false;
    }
    if(!targetScene || !targetScene->getECS()){
        setIoStatus("Instantiate Prefab failed: target scene is unavailable.", true);
        return false;
    }

    NeoECS::GameObject* parentObject = nullptr;
    std::unique_ptr<NeoECS::GameObject> parentWrapper;
    if(!parentEntityId.empty()){
        NeoECS::ECSEntity* parentEntity = findEntityById(parentEntityId);
        if(!parentEntity){
            setIoStatus("Instantiate Prefab failed: target parent entity was not found.", true);
            return false;
        }
        parentWrapper.reset(NeoECS::GameObject::CreateFromECSEntity(targetScene->getECS()->getContext(), parentEntity));
        if(!parentWrapper){
            setIoStatus("Instantiate Prefab failed: could not resolve target parent object.", true);
            return false;
        }
        parentObject = parentWrapper.get();
    }

    PrefabIO::PrefabInstantiateResult result;
    std::string error;
    const std::filesystem::path normalizedPath = prefabPath.lexically_normal();
    if(!instantiatePrefabFromAbsolutePath(normalizedPath, parentObject, &result, &error)){
        setIoStatus("Instantiate Prefab failed: " + error, true);
        return false;
    }

    if(!result.rootObjects.empty() && result.rootObjects.front() && result.rootObjects.front()->gameobject()){
        selectEntity(result.rootObjects.front()->gameobject()->getNodeUniqueID());
    }

    setIoStatus("Prefab instantiated: " + normalizedPath.generic_string(), false);
    return true;
}

bool EditorScene::beginViewportPrefabDragPreview(const std::filesystem::path& prefabPath){
    cancelViewportPrefabDragPreview();
    if(playState != PlayState::Edit){
        return false;
    }

    PrefabIO::PrefabInstantiateResult result;
    std::string error;
    const std::filesystem::path normalizedPath = prefabPath.lexically_normal();
    if(!instantiatePrefabFromAbsolutePath(normalizedPath, nullptr, &result, &error)){
        setIoStatus("Prefab drag failed: " + error, true);
        return false;
    }
    if(result.rootObjects.empty()){
        setIoStatus("Prefab drag failed: instantiated no root objects.", true);
        return false;
    }

    viewportPrefabDragState.active = true;
    viewportPrefabDragState.prefabPath = normalizedPath;
    viewportPrefabDragState.rootEntityIds.clear();
    viewportPrefabDragState.rootOffsets.clear();

    Math3D::Vec3 anchor = Math3D::Vec3::zero();
    bool anchorInitialized = false;
    for(auto* rootObject : result.rootObjects){
        if(!rootObject || !rootObject->gameobject()){
            continue;
        }
        NeoECS::ECSEntity* entity = rootObject->gameobject();
        const std::string entityId = entity->getNodeUniqueID();
        viewportPrefabDragState.rootEntityIds.push_back(entityId);
        Math3D::Vec3 worldPos = targetScene->getWorldPosition(entity);
        if(!anchorInitialized){
            anchor = worldPos;
            anchorInitialized = true;
        }
        viewportPrefabDragState.rootOffsets.push_back(worldPos - anchor);
    }

    if(!anchorInitialized || viewportPrefabDragState.rootEntityIds.empty()){
        cancelViewportPrefabDragPreview();
        setIoStatus("Prefab drag failed: could not resolve instantiated root objects.", true);
        return false;
    }

    viewportPrefabDragState.placementPlaneY = anchor.y;
    selectEntity(viewportPrefabDragState.rootEntityIds.front());
    return true;
}

bool EditorScene::computeViewportMousePlacement(float planeY, Math3D::Vec3& outWorldPosition) const{
    if(!inputManager || !viewportCamera || !viewportRect.valid){
        return false;
    }

    Math3D::Vec2 mouse = inputManager->getMousePosition();
    Math3D::Vec3 nearPoint = screenToWorld(viewportCamera, mouse.x, mouse.y, 0.0f, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
    Math3D::Vec3 farPoint = screenToWorld(viewportCamera, mouse.x, mouse.y, 1.0f, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
    Math3D::Vec3 rayDir = farPoint - nearPoint;
    if(rayDir.length() <= Math3D::EPSILON){
        return false;
    }
    rayDir = rayDir.normalize();

    float t = 4.0f;
    if(std::fabs(rayDir.y) > 0.0001f){
        t = (planeY - nearPoint.y) / rayDir.y;
        if(t < 0.0f){
            t = 4.0f;
        }
    }

    outWorldPosition = nearPoint + (rayDir * t);
    outWorldPosition.y = planeY;
    return true;
}

void EditorScene::updateViewportPrefabDragPreviewFromMouse(){
    if(!viewportPrefabDragState.active || !targetScene || !targetScene->getECS()){
        return;
    }

    Math3D::Vec3 anchorPos = Math3D::Vec3::zero();
    if(!computeViewportMousePlacement(viewportPrefabDragState.placementPlaneY, anchorPos)){
        return;
    }

    auto* manager = targetScene->getECS()->getComponentManager();
    const size_t count = std::min(viewportPrefabDragState.rootEntityIds.size(), viewportPrefabDragState.rootOffsets.size());
    for(size_t i = 0; i < count; ++i){
        NeoECS::ECSEntity* entity = findEntityById(viewportPrefabDragState.rootEntityIds[i]);
        if(!entity){
            continue;
        }
        auto* transformComp = manager->getECSComponent<TransformComponent>(entity);
        if(!transformComp){
            continue;
        }
        transformComp->local.position = anchorPos + viewportPrefabDragState.rootOffsets[i];
    }
}

void EditorScene::finalizeViewportPrefabDragPreview(){
    if(!viewportPrefabDragState.active){
        return;
    }
    std::filesystem::path placedPath = viewportPrefabDragState.prefabPath;
    viewportPrefabDragState = ViewportPrefabDragState{};
    setIoStatus("Prefab instantiated: " + placedPath.generic_string(), false);
}

void EditorScene::cancelViewportPrefabDragPreview(){
    if(!viewportPrefabDragState.active){
        return;
    }
    if(targetScene && targetScene->getECS()){
        for(const std::string& rootId : viewportPrefabDragState.rootEntityIds){
            NeoECS::ECSEntity* entity = findEntityById(rootId);
            if(!entity || targetScene->isSceneRootEntity(entity)){
                continue;
            }
            std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(targetScene->getECS()->getContext(), entity));
            if(wrapper){
                targetScene->destroyECSGameObject(wrapper.get());
            }
        }
    }
    viewportPrefabDragState = ViewportPrefabDragState{};
    if(!selectedEntityId.empty() && !findEntityById(selectedEntityId)){
        selectEntity("");
    }
}

void EditorScene::drawToolbar(float width, float height){
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##Toolbar", nullptr, kPanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar);

    bool saveSceneClicked = false;
    bool saveSceneAsClicked = false;
    bool loadSceneClicked = false;
    if(ImGui::BeginMenuBar()){
        if(ImGui::BeginMenu("File")){
            saveSceneClicked = ImGui::MenuItem("Save Scene", "Ctrl+S", false, playState == PlayState::Edit);
            saveSceneAsClicked = ImGui::MenuItem("Save Scene As", "Ctrl+Shift+S", false, playState == PlayState::Edit);
            loadSceneClicked = ImGui::MenuItem("Load Scene", "Ctrl+O", false, playState == PlayState::Edit);
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Edit")){
            ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
            ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("View")){
            ImGui::MenuItem("Maximize On Play", nullptr, &maximizeOnPlay);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGuiIO& io = ImGui::GetIO();
    if(playState == PlayState::Edit && !io.WantTextInput){
        if(io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)){
            saveSceneAsClicked = true;
        }else if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)){
            saveSceneClicked = true;
        }
        if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)){
            loadSceneClicked = true;
        }
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

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
    ImGui::TextUnformatted(playState == PlayState::Edit ? "File > Save/Save As/Load Scene" : "Scene IO locked during play");
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

    if(!ioStatusMessage.empty()){
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        const ImVec4 statusColor = ioStatusIsError ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                                                   : ImVec4(0.45f, 0.95f, 0.55f, 1.0f);
        ImGui::TextColored(statusColor, "%s", ioStatusMessage.c_str());
    }

    if(playClicked){
        pendingToolbarPlayCommand = true;
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

    if(saveSceneClicked){
        pendingToolbarSaveSceneCommand = true;
    }
    if(saveSceneAsClicked){
        pendingToolbarSaveSceneAsCommand = true;
    }
    if(loadSceneClicked){
        pendingToolbarLoadSceneCommand = true;
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
    cancelViewportPrefabDragPreview();
    playState = PlayState::Edit;
    viewportRect = ViewportRect{};
    viewportHovered = false;
    previewWindowInitialized = false;
    if(targetFactory){
        std::function<PScene(RenderWindow*)> resetFactory = targetFactory;
        PScene replacementScene = resetFactory(getWindow());
        setActiveScene(replacementScene);
        targetFactory = resetFactory;
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
        },
        [this](const std::string& entityId){
            exportEntityAsPrefabToWorkspaceDirectory(entityId);
        },
        [this](const std::filesystem::path& prefabPath, const std::string& parentEntityId, std::string* outError) -> bool {
            const bool ok = instantiatePrefabUnderParentEntity(prefabPath, parentEntityId);
            if(!ok && outError){
                *outError = ioStatusMessage.empty() ? "Instantiate Prefab failed." : ioStatusMessage;
            }
            return ok;
        }
    );
}

void EditorScene::drawViewportPanel(float x, float y, float w, float h){
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(
        "Viewport",
        nullptr,
        kPanelFlags |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
    );
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    viewportHovered = ImGui::IsWindowHovered();
    viewportRect.x = pos.x;
    viewportRect.y = pos.y;
    viewportRect.w = size.x;
    viewportRect.h = size.y;
    viewportRect.valid = (size.x > 1.0f && size.y > 1.0f);

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##ViewportDragDropTarget", size);

    bool sawViewportPrefabPayload = false;
    bool deliveredViewportPrefabPayload = false;
    std::filesystem::path viewportPrefabPayloadPath;
    if(playState == PlayState::Edit && ImGui::BeginDragDropTarget()){
        EditorAssetUI::AssetTransaction droppedAsset;
        bool isDelivery = false;
        if(EditorAssetUI::AcceptAssetDropInCurrentTarget(EditorAssetUI::AssetKind::Any, droppedAsset, true, &isDelivery) &&
           isPathWithExtension(droppedAsset.absolutePath, ".prefab")){
            sawViewportPrefabPayload = true;
            deliveredViewportPrefabPayload = isDelivery;
            viewportPrefabPayloadPath = droppedAsset.absolutePath.lexically_normal();
        }
        ImGui::EndDragDropTarget();
    }

    if(playState != PlayState::Edit){
        cancelViewportPrefabDragPreview();
    }else if(sawViewportPrefabPayload){
        if(!viewportPrefabDragState.active || viewportPrefabDragState.prefabPath != viewportPrefabPayloadPath){
            beginViewportPrefabDragPreview(viewportPrefabPayloadPath);
        }
        if(viewportPrefabDragState.active){
            updateViewportPrefabDragPreviewFromMouse();
            if(deliveredViewportPrefabPayload){
                finalizeViewportPrefabDragPreview();
            }
        }
    }else if(viewportPrefabDragState.active){
        cancelViewportPrefabDragPreview();
    }

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
            bool forcePreviewLayout = false;

            if(!previewWindowInitialized){
                float defaultW = std::clamp(viewportRect.w * 0.32f, minPreviewW, std::min(380.0f, maxPreviewW));
                float defaultH = std::clamp((defaultW * (9.0f / 16.0f)) + 44.0f, minPreviewH, maxPreviewH);
                previewWindowSize = Math3D::Vec2(defaultW, defaultH);
                previewWindowLocalPos = Math3D::Vec2(viewportRect.w - defaultW - margin, 28.0f);
                previewWindowInitialized = true;
                forcePreviewLayout = true;
            }

            Math3D::Vec2 unclampedSize = previewWindowSize;
            previewWindowSize.x = std::clamp(previewWindowSize.x, minPreviewW, maxPreviewW);
            previewWindowSize.y = std::clamp(previewWindowSize.y, minPreviewH, maxPreviewH);
            if(!Math3D::AreClose(unclampedSize.x, previewWindowSize.x) ||
               !Math3D::AreClose(unclampedSize.y, previewWindowSize.y)){
                forcePreviewLayout = true;
            }

            if(previewWindowPinned){
                previewWindowLocalPos.x = viewportRect.w - previewWindowSize.x - margin;
                previewWindowLocalPos.y = 28.0f;
                forcePreviewLayout = true;
            }

            Math3D::Vec2 unclampedPos = previewWindowLocalPos;
            float maxLocalX = std::max(0.0f, viewportRect.w - previewWindowSize.x);
            float maxLocalY = std::max(0.0f, viewportRect.h - previewWindowSize.y);
            previewWindowLocalPos.x = std::clamp(previewWindowLocalPos.x, 0.0f, maxLocalX);
            previewWindowLocalPos.y = std::clamp(previewWindowLocalPos.y, 0.0f, maxLocalY);
            if(!Math3D::AreClose(unclampedPos.x, previewWindowLocalPos.x) ||
               !Math3D::AreClose(unclampedPos.y, previewWindowLocalPos.y)){
                forcePreviewLayout = true;
            }

            ImVec2 windowPos(viewportRect.x + previewWindowLocalPos.x, viewportRect.y + previewWindowLocalPos.y);
            ImVec2 windowSize(previewWindowSize.x, previewWindowSize.y);
            const ImGuiCond layoutCond = forcePreviewLayout ? ImGuiCond_Always : ImGuiCond_Appearing;

            ImGui::SetNextWindowPos(windowPos, layoutCond);
            ImGui::SetNextWindowSize(windowSize, layoutCond);
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
                    if(previewWindowPinned){
                        previewWindowLocalPos.x = viewportRect.w - previewWindowSize.x - margin;
                        previewWindowLocalPos.y = 28.0f;
                    }
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
    workspacePanel.draw(
        x,
        y,
        w,
        h,
        selectedAssetPath,
        [this](const std::string& entityId, const std::filesystem::path& exportDirectory, std::string* outError) -> bool {
            const bool ok = exportEntityAsPrefabToDirectory(entityId, exportDirectory);
            if(!ok && outError){
                *outError = ioStatusMessage.empty() ? "Create Prefab failed." : ioStatusMessage;
            }
            return ok;
        }
    );
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
    cancelViewportPrefabDragPreview();
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
