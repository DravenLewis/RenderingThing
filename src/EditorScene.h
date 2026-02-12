#ifndef EDITOR_SCENE_H
#define EDITOR_SCENE_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <atomic>

#include "Scene.h"
#include <imgui.h>
#include "neoecs.hpp"
#include "ECSComponents.h"
#include "Widgets/TransformWidget.h"

class EditorScene : public Scene {
    public:
        explicit EditorScene(RenderWindow* window, PScene targetScene);
        EditorScene(RenderWindow* window, PScene targetScene, std::function<PScene(RenderWindow*)> factory);

        void init() override;
        void update(float deltaTime) override;
        void render() override;
        void drawToWindow(bool clearWindow = true, float x = -1, float y = -1, float w = -1, float h = -1) override;
        void dispose() override;
        void setInputManager(std::shared_ptr<InputManager> manager) override;
        void requestClose() override;
        bool consumeCloseRequest() override;
        bool handleQuitRequest();

    private:
        enum class PlayState {
            Edit = 0,
            Play,
            Pause
        };

        struct ViewportRect {
            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            bool valid = false;
        };

        PScene targetScene;
        std::function<PScene(RenderWindow*)> targetFactory;
        bool targetInitialized = false;
        PlayState playState = PlayState::Edit;
        bool inputBlockerRegistered = false;

        ViewportRect viewportRect;
        bool viewportHovered = false;
        std::string selectedEntityId;

        PCamera editorCamera;
        PCamera targetCamera;
        float editorYaw = -90.0f;
        float editorPitch = 0.0f;
        float editorMoveSpeed = 8.0f;
        float editorLookSensitivity = 0.1f;
        float editorFastScale = 2.0f;
        bool editorCameraActive = false;
        bool prevRmb = false;
        bool prevLmb = false;
        std::shared_ptr<IEventHandler> inputBlocker;
        bool focusActive = false;
        Math3D::Vec3 focusTarget;
        Math3D::Vec3 focusForward;
        float focusDistance = 5.0f;
        float focusSpeed = 6.0f;
        NeoECS::GameObject* editorCameraObject = nullptr;
        TransformComponent* editorCameraTransform = nullptr;
        CameraComponent* editorCameraComponent = nullptr;
        std::atomic<bool> resetRequested{false};
        std::atomic<bool> resetCompleted{false};
        struct ResetContext {
            std::string selectedId;
            bool hadSelection = false;
            Math3D::Transform editorCameraTransform;
            bool hadCamera = false;
        };
        ResetContext resetContext;

        std::filesystem::path assetRoot;
        std::filesystem::path assetDir;
        std::filesystem::path selectedAssetPath;
        char newAssetName[128] = {};
        uint64_t lastLogVersion = 0;
        std::string logBuffer;
        std::vector<std::string> logLines;
        std::vector<ImVec4> logColors;
        TransformWidget transformWidget;
        bool prevKeyW = false;
        bool prevKeyE = false;
        bool prevKeyR = false;

        void ensureTargetInitialized();
        void drawToolbar(float width, float height);
        void drawEcsPanel(float x, float y, float w, float h);
        void drawViewportPanel(float x, float y, float w, float h);
        void drawPropertiesPanel(float x, float y, float w, float h);
        void drawAssetsPanel(float x, float y, float w, float h);

        NeoECS::ECSEntity* findEntityById(const std::string& id) const;
        void drawEntityTree(NeoECS::ECSEntity* entity);
        bool isMouseInViewport() const;
        void selectEntity(const std::string& id);
        std::string pickEntityIdAtScreen(float x, float y, PCamera cam);
        void focusOnEntity(const std::string& id);
        void performStop();
        void storeSelectionForPlay();
        void restoreSelectionAfterReset();
};

#endif // EDITOR_SCENE_H
