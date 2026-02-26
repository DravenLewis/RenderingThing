#ifndef EDITOR_SCENE_H
#define EDITOR_SCENE_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <atomic>

#include "Scene/Scene.h"
#include <imgui.h>
#include "neoecs.hpp"
#include "ECS/Core/ECSComponents.h"
#include "Editor/Widgets/TransformWidget.h"
#include "Editor/Widgets/LightWidget.h"
#include "Editor/Widgets/CameraWidget.h"
#include "Editor/Widgets/ECSViewPanel.h"
#include "Editor/Widgets/PropertiesPanel.h"
#include "Editor/Widgets/WorkspacePanel.h"
#include "Rendering/Textures/Texture.h"

// EditorScene is a wrapper/editor-host scene that contains and edits another scene instance.
// It must keep its own editor camera for viewport navigation, while target-scene cameras are
// edited/previewed and can be marked current for the target scene's runtime/play camera.
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
        bool shouldTickOnRenderThread() const override { return true; }
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
        bool maximizeOnPlay = false;
        bool inputBlockerRegistered = false;

        ViewportRect viewportRect;
        bool viewportHovered = false;
        std::string selectedEntityId;

        PCamera editorCamera;
        PCamera targetCamera;
        PCamera viewportCamera;
        float editorYaw = -90.0f;
        float editorPitch = 0.0f;
        float editorMoveSpeed = 8.0f;
        float editorZoomSpeed = 6.0f;
        float editorMoveSmoothing = 12.0f;
        float editorZoomImpulse = 8.0f;
        float editorZoomDamping = 12.0f;
        float editorLookSensitivity = 0.1f;
        float editorFastScale = 2.0f;
        Math3D::Vec3 editorMoveVelocity = Math3D::Vec3::zero();
        float editorZoomVelocity = 0.0f;
        bool editorCameraActive = false;
        bool prevRmb = false;
        bool prevLmb = false;
        std::shared_ptr<IEventHandler> inputBlocker;
        bool focusActive = false;
        Math3D::Vec3 focusTarget;
        Math3D::Vec3 focusForward;
        float focusDistance = 5.0f;
        float focusSpeed = 6.0f;
        bool playViewportMouseRectConstrained = false;
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
        std::filesystem::path selectedAssetPath;
        float leftPanelWidth = 260.0f;
        float rightPanelWidth = 320.0f;
        float bottomPanelHeight = 220.0f;
        float playModePanelRefreshAccum = 0.0f;
        float playModePanelRefreshInterval = 0.10f; // 10 Hz heavy panel refresh during play
        bool playModeHeavyPanelsRefreshDue = true;
        ECSViewPanel ecsViewPanel;
        PropertiesPanel propertiesPanel;
        WorkspacePanel workspacePanel;
        TransformWidget transformWidget;
        bool prevKeyW = false;
        bool prevKeyE = false;
        bool prevKeyR = false;
        LightWidget lightWidget;
        CameraWidget cameraWidget;
        std::unordered_set<std::string> migratedLightSyncTransform;
        std::unordered_set<std::string> migratedLightDefaults;
        bool editorIconsLoaded = false;
        PTexture iconCamera;
        PTexture iconLightPoint;
        PTexture iconLightSpot;
        PTexture iconLightDirectional;
        PTexture iconAudio;
        PFrameBuffer previewCaptureBuffer;
        PTexture previewTexture;
        PCamera previewCamera;
        bool previewWindowPinned = true;
        bool previewWindowInitialized = false;
        Math3D::Vec2 previewWindowLocalPos = Math3D::Vec2(0.0f, 0.0f);
        Math3D::Vec2 previewWindowSize = Math3D::Vec2(280.0f, 210.0f);

        void ensureTargetInitialized();
        void drawToolbar(float width, float height);
        void drawEcsPanel(float x, float y, float w, float h, bool lightweight = false);
        void drawViewportPanel(float x, float y, float w, float h);
        void drawPropertiesPanel(float x, float y, float w, float h, bool lightweight = false);
        void drawAssetsPanel(float x, float y, float w, float h, bool lightweight = false);

        NeoECS::ECSEntity* findEntityById(const std::string& id) const;
        PCamera resolveSelectedTargetCamera() const;
        bool isMouseInViewport() const;
        void selectEntity(const std::string& id);
        std::string pickEntityIdAtScreen(float x, float y, PCamera cam);
        void focusOnEntity(const std::string& id);
        void performStop();
        void storeSelectionForPlay();
        void restoreSelectionAfterReset();
        void ensureEditorIconsLoaded();
        void drawBillboardIcon(ImDrawList* drawList,
                               const Math3D::Vec3& worldPos,
                               PTexture texture,
                               float sizePx,
                               ImU32 tint) const;
        void ensurePointLightBounds(NeoECS::ECSEntity* entity, float radius);
        bool computeSceneBounds(Math3D::Vec3& outCenter, float& outRadius) const;
};

#endif // EDITOR_SCENE_H
