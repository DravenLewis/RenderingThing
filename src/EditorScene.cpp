#include "EditorScene.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

#include "ECSComponents.h"
#include "File.h"
#include "ImGuiLayer.h"
#include "Logbot.h"
#include "RenderWindow.h"
#include "Asset.h"
#include "Logbot.h"
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include "neoecs.hpp"

namespace {
    constexpr float kToolbarHeight = 32.0f;
    constexpr float kLeftPanelWidth = 260.0f;
    constexpr float kRightPanelWidth = 320.0f;
    constexpr float kBottomPanelHeight = 220.0f;
    constexpr float kPanelGap = 4.0f;

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
    assetDir = assetRoot;
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
        targetCamera = mainScreen->getCamera();
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

    if(resetCompleted.exchange(false)){
        restoreSelectionAfterReset();
    }

    if(playState == PlayState::Play){
        if(auto mainScreen = targetScene->getMainScreen()){
            if(targetCamera){
                mainScreen->setCamera(targetCamera);
            }
        }
        bool mouseInViewport = isMouseInViewport();
        ImGuiLayer::SetInputEnabled(!mouseInViewport);
        if(targetScene->consumeCloseRequest()){
            handleQuitRequest();
            return;
        }
        targetScene->updateECS(deltaTime);
        targetScene->update(deltaTime);
    }else{
        if(inputManager){
            bool rmb = inputManager->isRMBDown();
            bool rmbPressed = rmb && !prevRmb;
            prevRmb = rmb;
            bool mouseInViewport = isMouseInViewport();
            bool lmb = inputManager->isLMBDown();
            bool lmbPressed = lmb && !prevLmb;
            bool lmbReleased = !lmb && prevLmb;
            prevLmb = lmb;

            if(!rmb){
                editorCameraActive = false;
            }else if(rmbPressed && mouseInViewport){
                editorCameraActive = true;
            }

            bool allowControl = editorCameraActive && rmb;
            inputManager->setMouseCaptureMode(allowControl ? MouseLockMode::LOCKED : MouseLockMode::FREE);
            ImGuiLayer::SetInputEnabled(!allowControl);

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
            if(playState == PlayState::Edit && editorCamera && targetScene && targetScene->getECS()){
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
                            editorCamera,
                            viewport,
                            worldPos,
                            transformComp->local,
                            mouseInViewport && !allowControl,
                            lmbPressed,
                            lmb,
                            lmbReleased
                        );
                        if(auto* lightComp = components->getECSComponent<LightComponent>(entity)){
                            if(!widgetConsumed){
                                widgetConsumed = lightWidget.update(
                                    this,
                                    inputManager.get(),
                                    editorCamera,
                                    viewport,
                                    worldPos,
                                    worldForward,
                                    lightComp->light,
                                    lightComp->syncTransform,
                                    lightComp->syncDirection,
                                    mouseInViewport && !allowControl,
                                    lmbPressed,
                                    lmb,
                                    lmbReleased
                                );
                                if(lightComp->light.type == LightType::POINT){
                                    ensurePointLightBounds(entity, lightComp->light.range);
                                }
                            }
                        }
                    }
                }
            }

            if(playState != PlayState::Play && lmbPressed && mouseInViewport && !allowControl && !widgetConsumed){
                if(editorCamera){
                    Math3D::Vec2 mouse = inputManager->getMousePosition();
                    std::string picked = pickEntityIdAtScreen(mouse.x, mouse.y, editorCamera);
                    if(!picked.empty()){
                        selectEntity(picked);
                    }else{
                        selectEntity("");
                    }
                }
            }
        }

        if(editorCamera && focusActive){
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
        }

        if(editorCamera && viewportRect.valid){
            editorCamera->resize(viewportRect.w, viewportRect.h);
        }

        if(auto mainScreen = targetScene->getMainScreen()){
            if(editorCamera){
                mainScreen->setCamera(editorCamera);
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
        targetScene->render();
    }

    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;

    drawToolbar(width, kToolbarHeight);

    const float panelsTop = kToolbarHeight + kPanelGap;
    const float panelsHeight = height - kToolbarHeight - kBottomPanelHeight - (kPanelGap * 2.0f);
    const float bottomTop = height - kBottomPanelHeight;
    const float centerWidth = width - kLeftPanelWidth - kRightPanelWidth - (kPanelGap * 2.0f);

    drawEcsPanel(0.0f, panelsTop, kLeftPanelWidth, panelsHeight);
    drawViewportPanel(kLeftPanelWidth + kPanelGap, panelsTop, centerWidth, panelsHeight);
    drawPropertiesPanel(width - kRightPanelWidth, panelsTop, kRightPanelWidth, panelsHeight);
    drawAssetsPanel(0.0f, bottomTop, width, kBottomPanelHeight);
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
    modeButton("Move [W]", TransformWidget::Mode::Translate);
    modeButton("Rotate [E]", TransformWidget::Mode::Rotate);
    modeButton("Scale [R]", TransformWidget::Mode::Scale);

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
    transformWidget.reset();
    if(targetScene){
        targetScene->setSelectedEntityId(id);
    }
}

void EditorScene::focusOnEntity(const std::string& id){
    if(id.empty() || !targetScene || !editorCamera) return;

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

    float fov = editorCamera->getSettings().fov;
    float distance = radius / std::tan(glm::radians(fov * 0.5f));
    distance *= 1.5f;

    auto transform = editorCamera->transform();
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
        auto* bounds = componentManager->getECSComponent<BoundsComponent>(entity);
        if(!transform || !renderer) continue;

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

void EditorScene::drawEcsPanel(float x, float y, float w, float h){
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("ECS Graph", nullptr, kPanelFlags);

    if(!targetScene || !targetScene->getECS()){
        ImGui::TextUnformatted("No ECS loaded.");
        ImGui::End();
        return;
    }

    auto* entityManager = targetScene->getECS()->getEntityManager();
    const auto& entities = entityManager->getEntities();

    if(entities.empty()){
        ImGui::TextUnformatted("No entities.");
        ImGui::End();
        return;
    }

    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(!entity || entity->getParent() != nullptr) continue;
        drawEntityTree(entity);
    }

    ImGui::End();
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

    if(playState == PlayState::Edit && targetScene && editorCamera && viewportRect.valid){
        auto* entity = findEntityById(selectedEntityId);
        if(entity && targetScene->getECS()){
            auto* components = targetScene->getECS()->getComponentManager();
            if(auto* transformComp = components->getECSComponent<TransformComponent>(entity)){
                TransformWidget::Viewport viewport{viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, viewportRect.valid};
                Math3D::Mat4 world = buildWorldMatrix(entity, components);
                Math3D::Vec3 worldPos = world.getPosition();
                Math3D::Transform worldTx = Math3D::Transform::fromMat4(world);
                Math3D::Vec3 worldForward = worldTx.forward();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                transformWidget.draw(drawList, this, editorCamera, viewport, worldPos, transformComp->local, viewportHovered);
                ensureEditorIconsLoaded();
                if(auto* cameraComp = components->getECSComponent<CameraComponent>(entity)){
                    (void)cameraComp;
                    drawBillboardIcon(drawList, worldPos, iconCamera, 64.0f, IM_COL32(255, 255, 255, 255));
                }
                if(auto* lightComp = components->getECSComponent<LightComponent>(entity)){
                    lightWidget.draw(drawList, this, editorCamera, viewport, worldPos, worldForward, lightComp->light, lightComp->syncTransform, lightComp->syncDirection);
                }
            }
        }
    }

    ImGui::TextUnformatted("Viewport");
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorScene::drawPropertiesPanel(float x, float y, float w, float h){
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Properties", nullptr, kPanelFlags);

    auto* entity = findEntityById(selectedEntityId);
    if(!entity){
        ImGui::TextUnformatted("No entity selected.");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity: %s", entity->getName().c_str());
    ImGui::Separator();

    if(targetScene && targetScene->getECS()){
        auto* componentMgr = targetScene->getECS()->getComponentManager();
        auto componentsForEntity = componentMgr->getECSComponents(entity);

        std::sort(componentsForEntity.begin(), componentsForEntity.end(), 
        [](NeoECS::ECSComponent* a, NeoECS::ECSComponent* b) {
            // If 'a' is Transform, it goes first (return true)
            if (dynamic_cast<TransformComponent*>(a)) return true;
            // If 'b' is Transform, it goes first (return false so 'a' is pushed back)
            if (dynamic_cast<TransformComponent*>(b)) return false;
            // Otherwise keep original order
            return false; 
        });

        for(auto component : componentsForEntity){
            IEditorCompatibleComponent* editorComponentPtr = dynamic_cast<IEditorCompatibleComponent*>(component);
            if(!editorComponentPtr) continue;

            ImGui::PushID(component);
            editorComponentPtr->drawPropertyWidget(targetScene->getECS(), targetScene);
            ImGui::PopID();
        }

        // --- The New Button ---
        ImGui::Dummy(ImVec2(0.0f, 20.0f)); // 20px space before button
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 10.0f)); // 10px padding

        if (ImGui::Button("Add Component", ImVec2(-FLT_MIN, 30.0f))) {
             // Logic to open "Add Component" menu
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f)); // 10px padding bottom
    }

    ImGui::End();
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
    if(!drawList || !texture || !editorCamera || !viewportRect.valid){
        return;
    }
    TransformWidget::Viewport viewport{viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, viewportRect.valid};
    Math3D::Vec3 screen = worldToScreen(editorCamera, worldPos, viewport.x, viewport.y, viewport.w, viewport.h);
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

void EditorScene::drawAssetsPanel(float x, float y, float w, float h){
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Workspace", nullptr, kPanelFlags);

    if(ImGui::BeginTabBar("BottomTabs")){
        if(ImGui::BeginTabItem("Assets")){
            ImGui::Text("Directory: %s", assetDir.string().c_str());
            ImGui::Separator();

            if(assetDir != assetRoot && ImGui::Button("Up")){
                assetDir = assetDir.parent_path();
            }

            ImGui::SameLine();
            ImGui::InputText("New", newAssetName, sizeof(newAssetName));
            ImGui::SameLine();
            if(ImGui::Button("Create")){
                if(newAssetName[0] != '\0'){
                    std::filesystem::path newPath = assetDir / newAssetName;
                    File file(newPath.string());
                    file.createFile();
                    newAssetName[0] = '\0';
                }
            }

            ImGui::SameLine();
            if(ImGui::Button("Delete") && !selectedAssetPath.empty()){
                File file(selectedAssetPath.string());
                file.deleteFile();
                selectedAssetPath.clear();
            }

            ImGui::Separator();

            ImGui::BeginChild("AssetList", ImVec2(0.0f, 0.0f), true);
            if(std::filesystem::exists(assetDir)){
                for(const auto& entry : std::filesystem::directory_iterator(assetDir)){
                    const auto& path = entry.path();
                    std::string label = path.filename().string();
                    if(entry.is_directory()){
                        label += "/";
                    }

                    bool selected = (path == selectedAssetPath);
                    if(ImGui::Selectable(label.c_str(), selected)){
                        selectedAssetPath = path;
                        if(entry.is_directory()){
                            assetDir = path;
                            selectedAssetPath.clear();
                        }
                    }
                }
            }else{
                ImGui::TextUnformatted("Directory missing.");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if(ImGui::BeginTabItem("Log")){
            uint64_t version = Logbot::GetLogVersion();
            bool logUpdated = false;
            if(version != lastLogVersion){
                logBuffer = Logbot::GetLogHistory();
                lastLogVersion = version;
                logUpdated = true;
                logLines.clear();
                logColors.clear();

                std::string current;
                current.reserve(256);
                for(char c : logBuffer){
                    if(c == '\n'){
                        if(!current.empty()){
                            ImVec4 color(0.8f, 0.8f, 0.8f, 1.0f);
                            if(current.find("[Warning]") != std::string::npos){
                                color = ImVec4(1.0f, 0.86f, 0.2f, 1.0f);
                            }else if(current.find("[ERROR]") != std::string::npos){
                                color = ImVec4(1.0f, 0.25f, 0.25f, 1.0f);
                            }else if(current.find("[FATAL ERROR]") != std::string::npos){
                                color = ImVec4(1.0f, 0.1f, 0.1f, 1.0f);
                            }else if(current.find("[Info]") != std::string::npos){
                                color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
                            }else if(current.find("[Unknown]") != std::string::npos){
                                color = ImVec4(0.53f, 0.53f, 0.53f, 1.0f);
                            }
                            logLines.push_back(current);
                            logColors.push_back(color);
                        }
                        current.clear();
                    }else{
                        current.push_back(c);
                    }
                }
                if(!current.empty()){
                    ImVec4 color(0.8f, 0.8f, 0.8f, 1.0f);
                    if(current.find("[Warning]") != std::string::npos){
                        color = ImVec4(1.0f, 0.86f, 0.2f, 1.0f);
                    }else if(current.find("[ERROR]") != std::string::npos){
                        color = ImVec4(1.0f, 0.25f, 0.25f, 1.0f);
                    }else if(current.find("[FATAL ERROR]") != std::string::npos){
                        color = ImVec4(1.0f, 0.1f, 0.1f, 1.0f);
                    }else if(current.find("[Info]") != std::string::npos){
                        color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
                    }else if(current.find("[Unknown]") != std::string::npos){
                        color = ImVec4(0.53f, 0.53f, 0.53f, 1.0f);
                    }
                    logLines.push_back(current);
                    logColors.push_back(color);
                }
            }

            ImGui::BeginChild("LogView", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            for(size_t i = 0; i < logLines.size(); ++i){
                ImGui::TextColored(logColors[i], "%s", logLines[i].c_str());
            }
            if(logUpdated){
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
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

void EditorScene::drawEntityTree(NeoECS::ECSEntity* entity){
    if(!entity) return;

    const bool hasChildren = !entity->children().empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if(!hasChildren){
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if(entity->getNodeUniqueID() == selectedEntityId){
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool opened = ImGui::TreeNodeEx((void*)entity, flags, "%s", entity->getName().c_str());
    if(ImGui::IsItemClicked()){
        selectEntity(entity->getNodeUniqueID());
    }

    if(hasChildren && opened){
        for(const auto& kv : entity->children()){
            drawEntityTree(kv.second);
        }
        ImGui::TreePop();
    }
}

void EditorScene::dispose(){
    if(targetScene){
        targetScene->dispose();
    }
    Scene::dispose();
}
