/**
 * @file src/Editor/Core/EditorScene.h
 * @brief Declarations for EditorScene.
 */

#ifndef EDITOR_SCENE_H
#define EDITOR_SCENE_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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
#include "Editor/Widgets/BoundsWidget.h"
#include "Editor/Widgets/ECSViewPanel.h"
#include "Editor/Widgets/PropertiesPanel.h"
#include "Editor/Widgets/WorkspacePanel.h"
#include "Rendering/Textures/Texture.h"
#include "Serialization/IO/PrefabIO.h"

class LoadedScene;

// EditorScene is a wrapper/editor-host scene that contains and edits another scene instance.
// It must keep its own editor camera for viewport navigation, while target-scene cameras are
// edited/previewed and can be marked current for the target scene's runtime/play camera.
/// @brief Represents the EditorScene type.
class EditorScene : public Scene {
    public:
        /**
         * @brief Constructs a new EditorScene instance.
         * @param window Value for window.
         * @param targetScene Value for target scene.
          * @return Result of this operation.
         */
        explicit EditorScene(RenderWindow* window, PScene targetScene);
        /**
         * @brief Constructs a new EditorScene instance.
         * @param window Value for window.
         * @param targetScene Value for target scene.
         * @param factory Value for factory.
         */
        EditorScene(RenderWindow* window, PScene targetScene, std::function<PScene(RenderWindow*)> factory);

        /**
         * @brief Initializes this object.
         */
        void init() override;
        /**
         * @brief Updates internal state.
         * @param deltaTime Delta time in seconds.
         */
        void update(float deltaTime) override;
        /**
         * @brief Renders this object.
         */
        void render() override;
        /**
         * @brief Draws to window.
         * @param clearWindow Flag controlling clear window.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param w Value for w.
         * @param h Value for h.
         */
        void drawToWindow(bool clearWindow = true, float x = -1, float y = -1, float w = -1, float h = -1) override;
        /**
         * @brief Disposes this object.
         */
        void dispose() override;
        /**
         * @brief Sets the input manager.
         * @param manager Value for manager.
         */
        void setInputManager(std::shared_ptr<InputManager> manager) override;
        /**
         * @brief Requests close.
         */
        void requestClose() override;
        /**
         * @brief Checks whether consume close request.
         * @return True when the operation succeeds; otherwise false.
         */
        bool consumeCloseRequest() override;
        /**
         * @brief Checks whether tick on render thread.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool shouldTickOnRenderThread() const override { return true; }
        bool handleQuitRequest();
        void setActiveScene(PScene scene);
        PScene getActiveScene() const { return targetScene; }

    private:
        /// @brief Enumerates values for PlayState.
        enum class PlayState {
            Edit = 0,
            Play,
            Pause
        };

        /// @brief Enumerates values for SceneFileDialogMode.
        enum class SceneFileDialogMode {
            None = 0,
            SaveAs,
            Load
        };

        /// @brief Holds data for ViewportRect.
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
        bool showEditorCameraSettings = false;

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
        /// @brief Holds data for ResetContext.
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
        BoundsWidget boundsWidget;
        std::unordered_set<std::string> migratedLightSyncTransform;
        std::unordered_set<std::string> migratedLightDefaults;
        bool editorIconsLoaded = false;
        bool showSceneGrid = true;
        bool showSceneGizmos = true;
        bool showScenePerformanceInfo = false;
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
        /// @brief Holds data for ViewportPrefabDragState.
        struct ViewportPrefabDragState {
            bool active = false;
            std::filesystem::path prefabPath;
            std::vector<std::string> rootEntityIds;
            std::vector<Math3D::Vec3> rootOffsets;
            float placementPlaneY = 0.0f;
        };
        ViewportPrefabDragState viewportPrefabDragState;
        /// @brief Holds data for SceneFileDialogState.
        struct SceneFileDialogState {
            bool openRequested = false;
            bool focusNameInput = false;
            SceneFileDialogMode mode = SceneFileDialogMode::None;
            std::filesystem::path currentDirectory;
            std::filesystem::path selectedPath;
            char fileNameBuffer[256] = {};
            std::string errorMessage;
        };
        SceneFileDialogState sceneFileDialogState;
        std::filesystem::path activeScenePath;
        bool pendingPlayAfterSceneSave = false;
        bool pendingToolbarPlayCommand = false;
        bool pendingToolbarUndoCommand = false;
        bool pendingToolbarRedoCommand = false;
        bool pendingToolbarSaveSceneCommand = false;
        bool pendingToolbarSaveSceneAsCommand = false;
        bool pendingToolbarLoadSceneCommand = false;
        std::string ioStatusMessage;
        bool ioStatusIsError = false;
        float ioStatusTimeRemaining = 0.0f;
        /// @brief Holds data for EditorEntityState.
        struct EditorEntityState {
            std::uint64_t stableId = 0;
            std::string name;
            std::vector<JsonSchema::EntitySnapshotSchemaBase::ComponentRecord> components;
        };
        /// @brief Holds data for EditorSubtreeSnapshot.
        struct EditorSubtreeSnapshot {
            std::vector<JsonSchema::EntitySnapshotSchemaBase::EntityRecord> entities;
            std::vector<std::uint64_t> rootIds;
            std::vector<std::uint64_t> rootParentIds;
        };
        /// @brief Holds data for EditorSceneChange.
        struct EditorSceneChange {
            /// @brief Enumerates values for Kind.
            enum class Kind {
                EntityState = 0,
                EntityReparent,
                SubtreePresence
            };

            Kind kind = Kind::EntityState;
            std::string label;
            std::string valuePath;
            std::uint64_t targetStableId = 0;
            EditorEntityState beforeEntityState;
            EditorEntityState afterEntityState;
            EditorSubtreeSnapshot subtreeSnapshot;
            std::uint64_t beforeParentStableId = 0;
            std::uint64_t afterParentStableId = 0;
            bool subtreePresentBefore = false;
            bool subtreePresentAfter = false;
        };
        std::vector<EditorSceneChange> editHistoryChanges;
        size_t editHistoryIndex = 0;
        bool editHistoryApplying = false;
        bool trackedEntityObservationValid = false;
        EditorEntityState lastObservedTrackedEntityState;
        bool trackedEntityChangePending = false;
        EditorEntityState pendingTrackedEntityStateBefore;
        std::unordered_map<std::uint64_t, std::string> stableEntityRuntimeIds;
        std::unordered_map<std::uint64_t, EditorSubtreeSnapshot> pendingDeletedSubtreeSnapshots;
        bool editorSessionDirty = false;
        bool editorSessionSaveRequested = false;
        float editorSessionSaveDelaySeconds = 0.0f;
        bool observedEditorCameraStateValid = false;
        Math3D::Transform lastObservedEditorCameraTransform;
        float lastObservedEditorYaw = -90.0f;
        float lastObservedEditorPitch = 0.0f;
        bool observedPropertiesPanelStateValid = false;
        PropertiesPanel::State lastObservedPropertiesPanelState;
        bool observedSceneViewStateValid = false;
        bool lastObservedShowSceneGrid = true;
        bool lastObservedShowSceneGizmos = true;
        bool lastObservedShowScenePerformanceInfo = false;
        enum class StartupLoadPhase {
            None = 0,
            CreateEntities,
            DeserializeComponents,
            Finalize
        };
        bool startupBootstrapPending = true;
        bool startupUiFramePresented = false;
        bool startupLoadingOverlayActive = true;
        bool startupSessionParsed = false;
        bool startupSessionHasCachedScene = false;
        std::filesystem::path startupCachedScenePath;
        PropertiesPanel::State startupSessionPropertiesState;
        bool startupSessionHasCameraTransform = false;
        Math3D::Transform startupSessionCameraTransform;
        float startupSessionEditorYaw = -90.0f;
        float startupSessionEditorPitch = 0.0f;
        bool startupSessionShowSceneGrid = true;
        bool startupSessionShowSceneGizmos = true;
        bool startupSessionShowScenePerformanceInfo = false;
        StartupLoadPhase startupLoadPhase = StartupLoadPhase::None;
        std::shared_ptr<LoadedScene> startupLoadScene;
        std::filesystem::path startupLoadScenePath;
        JsonSchema::SceneSchema startupLoadSchema;
        std::unordered_map<std::uint64_t, const JsonSchema::EntitySnapshotSchemaBase::EntityRecord*> startupLoadRecordsById;
        std::vector<const JsonSchema::EntitySnapshotSchemaBase::EntityRecord*> startupLoadPendingRecords;
        std::unordered_map<std::uint64_t, NeoECS::GameObject*> startupLoadGameObjectsBySnapshotId;
        std::unordered_map<std::uint64_t, NeoECS::ECSEntity*> startupLoadEntitiesBySnapshotId;
        size_t startupLoadDeserializeIndex = 0;

        void ensureTargetInitialized();
        void applyActiveSceneState();
        bool parseStartupSessionState(std::string* outError);
        bool beginStartupCachedSceneLoad(const std::filesystem::path& scenePath, std::string* outError);
        bool stepStartupCachedSceneLoad(std::string* outError);
        void clearStartupCachedSceneLoadState();
        void applyStartupSessionUiState();
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
        void drawWorldGridOverlay(ImDrawList* drawList, const TransformWidget::Viewport& viewport) const;
        void ensurePointLightBounds(NeoECS::ECSEntity* entity, float radius);
        bool computeSceneBounds(Math3D::Vec3& outCenter, float& outRadius) const;
        void setIoStatus(const std::string& message, bool isError);
        bool saveSceneFromEditorCommand();
        bool saveSceneAsFromEditorCommand();
        bool loadSceneFromEditorCommand();
        bool openSceneFileDialog(SceneFileDialogMode mode);
        bool saveSceneToAbsolutePath(const std::filesystem::path& savePath);
        bool loadSceneFromAbsolutePath(const std::filesystem::path& loadPath);
        std::filesystem::path resolveCurrentSceneSourcePath() const;
        void updateSceneSourceAfterSave(const std::filesystem::path& savePath);
        bool beginPlayModeFromEditor();
        bool enterPlayModeFromScenePath(const std::filesystem::path& scenePath);
        void processDeferredToolbarCommands();
        void drawSceneFileDialog();
        bool exportEntityAsPrefabToDirectory(const std::string& entityId, const std::filesystem::path& directoryPath);
        bool exportEntityAsPrefabToWorkspaceDirectory(const std::string& entityId);
        bool instantiatePrefabUnderParentEntity(const std::filesystem::path& prefabPath, const std::string& parentEntityId);
        bool instantiatePrefabFromAbsolutePath(const std::filesystem::path& prefabPath,
                                               NeoECS::GameObject* parentObject,
                                               PrefabIO::PrefabInstantiateResult* outResult,
                                               std::string* outError);
        bool beginViewportPrefabDragPreview(const std::filesystem::path& prefabPath);
        void updateViewportPrefabDragPreviewFromMouse();
        void finalizeViewportPrefabDragPreview();
        void cancelViewportPrefabDragPreview();
        bool computeViewportMousePlacement(float planeY, Math3D::Vec3& outWorldPosition) const;
        std::filesystem::path resolveEditorCameraPrefabPath() const;
        std::filesystem::path resolveEditorSessionPath() const;
        bool loadEditorCameraFromPrefab(std::string* outError = nullptr);
        bool saveEditorCameraToPrefab(std::string* outError = nullptr) const;
        bool createDefaultEditorCameraObject();
        void syncEditorCameraEnvironmentFromActiveScene();
        void setEditorCameraSettingsOpen(bool open);
        bool buildSceneHistoryEditorStateRawJson(JsonSchema::RawJsonValue& outEditorState,
                                                 std::string* outError = nullptr) const;
        void applySceneHistoryEditorStateRawJson(const JsonSchema::RawJsonValue& editorState);
        bool buildCurrentEditSnapshot(JsonSchema::SceneSchema& outSchema,
                                      std::string* outJson,
                                      std::string* outError = nullptr) const;
        bool applyEditSnapshot(const JsonSchema::SceneSchema& schema, std::string* outError = nullptr);
        void resetEditHistoryToCurrentScene();
        void resetTrackedEntityObservation();
        std::uint64_t computeStableEntityId(const std::string& runtimeId) const;
        std::uint64_t computeStableEntityId(NeoECS::ECSEntity* entity) const;
        void refreshStableEntityMappings();
        NeoECS::ECSEntity* findEntityByStableId(std::uint64_t stableId) const;
        bool captureEntityState(NeoECS::ECSEntity* entity, EditorEntityState& outState, std::string* outError = nullptr) const;
        bool captureEntityStateByStableId(std::uint64_t stableId, EditorEntityState& outState, std::string* outError = nullptr) const;
        bool applyEntityState(const EditorEntityState& state, std::string* outError = nullptr);
        bool captureSubtreeSnapshot(const std::vector<NeoECS::ECSEntity*>& roots,
                                    EditorSubtreeSnapshot& outSnapshot,
                                    std::string* outError = nullptr) const;
        bool applySubtreePresence(const EditorSubtreeSnapshot& snapshot, bool present, std::string* outError = nullptr);
        bool applyEntityReparent(std::uint64_t entityStableId,
                                 std::uint64_t parentStableId,
                                 std::string* outError = nullptr);
        bool applyEditHistoryChange(const EditorSceneChange& change,
                                    bool applyAfterState,
                                    std::string* outError = nullptr);
        void pushEditHistoryChange(EditorSceneChange change);
        void observeCurrentEditState(bool interactionActive);
        void flushEditHistoryObservation(bool forceCommit);
        bool canUndoEditHistory() const;
        bool canRedoEditHistory() const;
        bool performUndo();
        bool performRedo();
        void beginPendingDeletedSubtreeCapture(const std::string& entityId);
        void commitPendingDeletedSubtreeCapture(const std::string& entityId);
        void recordCreatedSubtreeChange(const std::vector<NeoECS::ECSEntity*>& roots,
                                        const std::string& label,
                                        const std::string& valuePath);
        void recordCreatedEntityChange(const std::string& entityId,
                                       const std::string& label,
                                       const std::string& valuePath);
        void recordRenamedEntityChange(const std::string& entityId,
                                       const std::string& oldName,
                                       const std::string& newName);
        void recordReparentedEntityChange(const std::string& entityId,
                                          const std::string& oldParentId,
                                          const std::string& newParentId);
        void prepareForSceneMutationTracking();
        void syncTargetSceneAfterEditHistoryApply(std::uint64_t preferredSelectedStableId);
        void observeTransientEditorSessionState();
        void markEditorSessionDirty(float saveDelaySeconds = 0.75f);
        void advanceEditorSessionAutosave(float deltaTime);
        void flushEditorSessionAutosave(bool force, bool interactionActive);
        bool saveEditorSessionToDisk(std::string* outError = nullptr);
        bool loadEditorSessionFromDisk(std::string* outError = nullptr);
};

#endif // EDITOR_SCENE_H
