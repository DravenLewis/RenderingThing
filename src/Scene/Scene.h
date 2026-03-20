/**
 * @file src/Scene/Scene.h
 * @brief Declarations for Scene.
 */

#ifndef SCENE_H
#define SCENE_H

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "Foundation/Math/Color.h"
#include "Rendering/Core/View.h"
#include "Platform/Input/InputManager.h"
#include "Rendering/Geometry/Model.h"
#include "Rendering/Lighting/DeferredScreenGI.h"
#include "Rendering/Lighting/DeferredSSR.h"
#include "Rendering/Lighting/DeferredSSAO.h"
#include "Rendering/Lighting/Light.h"
#include "neoecs.hpp"
#include "Rendering/Materials/MaterialDefaults.h"

class Scene;
typedef std::shared_ptr<Scene> PScene;
class ShaderProgram;

/// @brief Represents the Scene type.
class Scene : public View {
    public:
        /**
         * @brief Constructs a new Scene instance.
         * @param window Window that owns the rendering context for this scene.
         */
        explicit Scene(RenderWindow* window = nullptr);
        /**
         * @brief Destroys this Scene instance.
         */
        virtual ~Scene();

        /**
         * @brief Initializes scene resources and entities.
         */
        virtual void init() {}
        /**
         * @brief Updates scene simulation state.
         * @param deltaTime Delta time in seconds.
         */
        virtual void update(float deltaTime) {}
        /**
         * @brief Renders scene content.
         */
        virtual void render() {}
        /**
         * @brief Releases scene-owned resources.
         */
        virtual void dispose();
        /**
         * @brief Sets the input manager used by this scene.
         * @param manager Shared input manager instance.
         */
        virtual void setInputManager(std::shared_ptr<InputManager> manager) { inputManager = manager; }
        /**
         * @brief Handles a state switch into this scene.
         * @param newState Scene being activated.
         * @param oldState Scene being deactivated.
         * @return True when the switch is accepted.
         */
        virtual bool switchState(PScene newState, PScene oldState) { return true; }
        /**
         * @brief Returns whether update ticks should run on the render thread.
         * @return True if ticking on the render thread is required.
         */
        virtual bool shouldTickOnRenderThread() const { return false; }
        /**
         * @brief Renders the scene into the active viewport target.
         */
        void renderViewportContents();

        /**
         * @brief Updates ECS transforms and component-driven runtime state.
         * @param deltaTime Delta time in seconds.
         */
        void updateECS(float deltaTime);
        /**
         * @brief Rebuilds the render snapshot used by the render pass.
         */
        void refreshRenderState();
        /**
         * @brief Applies camera-defined screen effects and deferred-lighting overrides.
         * @param screen Destination screen/post-process chain.
         * @param manager ECS component manager.
         * @param cameraEntity Camera entity owning effect components.
         * @param clearExisting True to clear previously queued effects.
         * @param entityManager Optional entity manager for cross-entity lookups.
         */
        static void ApplyCameraEffectsToScreen(
            PScreen screen,
            NeoECS::ECSComponentManager* manager,
            NeoECS::ECSEntity* cameraEntity,
            bool clearExisting = true,
            NeoECS::ECSEntityManager* entityManager = nullptr,
            NeoECS::ECSComponentManager* effectSourceManager = nullptr
        );
        /**
         * @brief Applies camera effects using this scene's ECS context.
         * @param screen Destination screen/post-process chain.
         * @param cameraEntity Camera entity owning effect components.
         * @param clearExisting True to clear previously queued effects.
         * @param adaptiveFocusSourceScene Optional source scene for adaptive-focus sampling.
         */
        void applyCameraEffectsToScreen(PScreen screen,
                                        NeoECS::ECSEntity* cameraEntity,
                                        bool clearExisting = true,
                                        const Scene* adaptiveFocusSourceScene = nullptr);
        /**
         * @brief Computes adaptive focus distance from the current render snapshot.
         * @param camera Camera used for ray construction.
         * @param outDistance Output focus distance in scene units.
         * @return True when a valid distance is found.
         */
        bool computeAdaptiveFocusDistanceFromSnapshotForCamera(const PCamera& camera, float& outDistance) const;

        /**
         * @brief Returns the scene ECS instance.
         * @return Raw ECS instance pointer.
         */
        NeoECS::NeoECS* getECS() const { return ecsInstance; }
        /**
         * @brief Returns the scene ECS helper API.
         * @return Raw ECS API pointer.
         */
        NeoECS::NeoAPI* getECSAPI() const { return ecsAPI; }
        /**
         * @brief Creates an ECS game object under the scene root.
         * @param name Display name for the object.
         * @param parent Optional parent object.
         * @return Pointer to the created game object.
         */
        NeoECS::GameObject* createECSGameObject(const std::string& name, NeoECS::GameObject* parent = nullptr);
        /**
         * @brief Destroys an ECS game object.
         * @param object Object to destroy.
         * @return True when the object is removed.
         */
        bool destroyECSGameObject(NeoECS::GameObject* object);
        /**
         * @brief Creates a game object configured with model rendering components.
         * @param name Display name for the object.
         * @param model Model instance to assign.
         * @param parent Optional parent object.
         * @return Pointer to the created game object.
         */
        NeoECS::GameObject* createModelGameObject(const std::string& name, const PModel& model, NeoECS::GameObject* parent = nullptr);
        /**
         * @brief Creates a game object configured with a light component.
         * @param name Display name for the object.
         * @param light Light value to copy.
         * @param parent Optional parent object.
         * @param syncTransform True to keep light position synced to transform.
         * @param syncDirection True to keep light direction synced to transform.
         * @return Pointer to the created game object.
         */
        NeoECS::GameObject* createLightGameObject(const std::string& name, const Light& light, NeoECS::GameObject* parent = nullptr, bool syncTransform = true, bool syncDirection = false);
        /**
         * @brief Creates a game object configured with a camera component.
         * @param name Display name for the object.
         * @param parent Optional parent object.
         * @return Pointer to the created game object.
         */
        NeoECS::GameObject* createCameraGameObject(const std::string& name, NeoECS::GameObject* parent = nullptr);
        /**
         * @brief Creates a game object configured with an environment component.
         * @param name Display name for the object.
         * @param parent Optional parent object.
         * @return Pointer to the created game object.
         */
        NeoECS::GameObject* createEnvironmentGameObject(const std::string& name, NeoECS::GameObject* parent = nullptr);
        /**
         * @brief Returns the scene root game object.
         * @return Pointer to the scene root object.
         */
        NeoECS::GameObject* getSceneRootGameObject() const { return sceneRootObject; }
        /**
         * @brief Returns the scene root ECS entity.
         * @return Pointer to the root entity.
         */
        NeoECS::ECSEntity* getSceneRootEntity() const;
        /**
         * @brief Checks whether an entity is the scene root entity.
         * @param entity Entity to test.
         * @return True when the entity is the root.
         */
        bool isSceneRootEntity(NeoECS::ECSEntity* entity) const;

        /// @brief Holds data for DebugStats.
        struct DebugStats {
            std::atomic<float> snapshotMs{0.0f};
            std::atomic<float> shadowMs{0.0f};
            std::atomic<float> drawMs{0.0f};
            std::atomic<float> postFxMs{0.0f};
            std::atomic<int> drawCount{0};
            std::atomic<int> lightCount{0};
            std::atomic<int> postFxEffectCount{0};
        };

        /**
         * @brief Returns aggregate debug counters for the last rendered frame.
         * @return Reference to scene debug statistics.
         */
        const DebugStats& getDebugStats() const { return debugStats; }
        /**
         * @brief Requests scene closure.
         */
        virtual void requestClose();
        /**
         * @brief Consumes and clears a pending close request.
         * @return True when a request was pending.
         */
        virtual bool consumeCloseRequest();

    protected:
        /**
         * @brief Builds a world transform matrix for an entity.
         * @param entity Entity to evaluate.
         * @param manager ECS component manager.
         * @return World transform matrix.
         */
        Math3D::Mat4 buildWorldMatrix(NeoECS::ECSEntity* entity, NeoECS::ECSComponentManager* manager) const;

        std::shared_ptr<InputManager> inputManager;
        NeoECS::NeoECS* ecsInstance = nullptr;
        NeoECS::NeoAPI* ecsAPI = nullptr;
        NeoECS::GameObject* sceneRootObject = nullptr;

        /// @brief Holds data for RenderItem.
        struct RenderItem {
            std::shared_ptr<Mesh> mesh;
            std::shared_ptr<Material> material;
            Math3D::Mat4 model;
            bool enableBackfaceCulling = true;
            bool isTransparent = false;
            bool isDeferredCompatible = false;
            bool planarReflectionSource = false;
            std::string entityId;
            bool ignoreRaycastHit = false;
            bool castsShadows = true;
            bool hasBounds = false;
            Math3D::Vec3 boundsMin = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 boundsMax = Math3D::Vec3(0.0f, 0.0f, 0.0f);
        };

        /// @brief Holds data for ReflectionProbeSnapshot.
        struct ReflectionProbeSnapshot {
            std::string entityId;
            int resolution = 128;
            int priority = 0;
            bool autoUpdate = false;
            int updateIntervalFrames = 30;
            Math3D::Vec3 center = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 captureBoundsMin = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 captureBoundsMax = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 influenceBoundsMin = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 influenceBoundsMax = Math3D::Vec3(0.0f, 0.0f, 0.0f);
        };

        /// @brief Holds data for RenderSnapshot.
        struct RenderSnapshot {
            std::vector<RenderItem> drawItems;
            std::vector<ReflectionProbeSnapshot> reflectionProbes;
            std::vector<Light> lights;
        };

        std::array<RenderSnapshot, 2> renderSnapshots{};
        std::atomic<int> renderSnapshotIndex{0};
        DebugStats debugStats{};
        std::atomic<bool> closeRequested{false};
        std::string selectedEntityId;
        int selectedLightUploadIndex = -1;
        int assetChangeListenerHandle = -1;
        std::shared_ptr<MaterialDefaults::ColorMaterial> deferredIncompatibleMaterial;
        bool outlineEnabled = false;
        PFrameBuffer outlineMaskBuffer;
        std::shared_ptr<ShaderProgram> outlineMaskShader;
        std::shared_ptr<ShaderProgram> outlineCompositeShader;
        std::shared_ptr<ModelPart> outlineCompositeQuad;
        int outlineMaskWidth = 0;
        int outlineMaskHeight = 0;
        PFrameBuffer gBuffer;
        PFrameBuffer deferredDirectLightBuffer;
        std::shared_ptr<ShaderProgram> gBufferShader;
        std::shared_ptr<ShaderProgram> deferredLightShader;
        std::shared_ptr<ModelPart> deferredQuad;
        PTexture deferredLightTileTexture;
        std::vector<int> deferredLightTileCpuData;
        int gBufferWidth = 0;
        int gBufferHeight = 0;
        int deferredLightTileGridWidth = 0;
        int deferredLightTileGridHeight = 0;
        int deferredLightTileSize = 16;
        bool gBufferValidationDirty = true;
        bool gBufferValidated = false;
        bool deferredDisabled = false;
        PCamera preferredCamera;
        NeoECS::ECSEntity* activeCameraEntity = nullptr;
        std::shared_ptr<DeferredSSAO> deferredSsaoPass;
        std::shared_ptr<DeferredScreenGI> deferredScreenGiPass;
        std::shared_ptr<DeferredSSR> deferredSsrPass;
        struct PlanarReflectionSurface {
            PFrameBuffer buffer = nullptr;
            std::string entityId;
            Math3D::Vec3 center = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 normal = Math3D::Vec3(0.0f, 1.0f, 0.0f);
            Math3D::Mat4 viewProjection;
            float strength = 1.0f;
            float receiverFadeDistance = 1.0f;
            bool valid = false;
        };
        PlanarReflectionSurface activePlanarReflection{};
        bool userClipPlaneActive = false;
        Math3D::Vec4 userClipPlane = Math3D::Vec4(0.0f, 1.0f, 0.0f, 0.0f);
        struct LocalReflectionProbe {
            PCubeMap cubeMap = nullptr;
            unsigned int captureFbo = 0;
            unsigned int captureDepthRenderBuffer = 0;
            int faceSize = 0;
            bool valid = false;
            std::string anchorEntityId;
            Math3D::Vec3 center = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 captureBoundsMin = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 captureBoundsMax = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 influenceBoundsMin = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 influenceBoundsMax = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            unsigned long long lastUpdateFrame = 0;
        };
        LocalReflectionProbe deferredLocalReflectionProbe{};
        bool localReflectionProbeCaptureActive = false;
        unsigned long long reflectionCaptureFrameCounter = 0;
        PFrameBuffer transparentSsrSourceBuffer;
        DeferredSSRSettings transparentSsrSettings{};
        bool transparentSsrEnabled = false;
        bool hasDeferredSsaoOverride = false;
        DeferredSSAOSettings deferredSsaoOverrideSettings{};
        bool hasDeferredSsrOverride = false;
        DeferredSSRSettings deferredSsrOverrideSettings{};

        /// @brief Enumerates values for RenderFilter.
        enum class RenderFilter{
            All,
            Opaque,
            Transparent
        };

        /**
         * @brief Returns whether a material should be rendered in the transparent pass.
         * @param material Material to evaluate.
         * @return True when the material is transparent.
         */
        bool isMaterialTransparent(const std::shared_ptr<Material>& material) const;
        /**
         * @brief Returns whether a material is compatible with deferred shading.
         * @param material Material to evaluate.
         * @return True when deferred-compatible.
         */
        bool isDeferredCompatibleMaterial(const std::shared_ptr<Material>& material) const;
        /**
         * @brief Allocates or resizes deferred-render targets for the active screen.
         * @param screen Active screen target.
         */
        void ensureDeferredResources(PScreen screen);
        /**
         * @brief Allocates or resizes tiled deferred-light metadata resources.
         * @param width Active deferred viewport width.
         * @param height Active deferred viewport height.
         */
        void ensureDeferredLightTileResources(int width, int height);
        /**
         * @brief Builds per-tile deferred light lists for the active screen-space lighting pass.
         * @param cam Active camera.
         * @param lights Lights uploaded for the current frame.
         * @return True when tile metadata is ready for the deferred shader.
         */
        bool buildDeferredLightTiles(PCamera cam, const std::vector<Light>& lights);
        /**
         * @brief Allocates or resizes the selection-outline mask/composite resources.
         * @param screen Active screen target.
         */
        void ensureOutlineResources(PScreen screen);
        /**
         * @brief Computes adaptive focus distance using ECS snapshot data.
         * @param cameraEntity Active camera entity.
         * @param camera Active camera object.
         * @param outDistance Output focus distance.
         * @return True when a valid distance is found.
         */
        bool computeAdaptiveFocusDistanceFromSnapshot(NeoECS::ECSEntity* cameraEntity, const PCamera& camera, float& outDistance) const;
        /**
         * @brief Resolves deferred SSAO settings for the active camera or override source.
         * @param manager ECS component manager.
         * @param cameraEntity Camera entity to inspect when no override exists.
         * @param outSettings Output settings when SSAO is enabled.
         * @return True when deferred SSAO should run.
         */
        bool resolveDeferredSsaoSettings(NeoECS::ECSComponentManager* manager,
                                         NeoECS::ECSEntity* cameraEntity,
                                         DeferredSSAOSettings& outSettings) const;
        /**
         * @brief Resolves deferred SSR settings for the active camera.
         * @param manager ECS component manager.
         * @param cameraEntity Camera entity to inspect.
         * @param outSettings Output settings when SSR is enabled.
         * @return True when deferred SSR should run.
         */
        bool resolveDeferredSsrSettings(NeoECS::ECSComponentManager* manager,
                                        NeoECS::ECSEntity* cameraEntity,
                                        DeferredSSRSettings& outSettings) const;
        /**
         * @brief Rebuilds post-process effects for the active camera.
         * @param activeCameraEntity Active camera entity.
         * @param manager ECS component manager.
         */
        void updateActiveCameraEffects(NeoECS::ECSEntity* activeCameraEntity, NeoECS::ECSComponentManager* manager);
        /**
         * @brief Draws geometry into deferred G-buffer targets.
         * @param cam Active camera.
         */
        void drawDeferredGeometry(PCamera cam, const std::string* excludedEntityId = nullptr);
        /**
         * @brief Executes deferred lighting over the populated G-buffer.
         * @param screen Destination screen.
         * @param cam Active camera.
         */
        void drawDeferredLighting(PFrameBuffer targetBuffer,
                                  Color clearColor,
                                  PCamera cam,
                                  const std::shared_ptr<DeferredSSAO>& ssaoPass = nullptr,
                                  const DeferredSSAOSettings* ssaoSettings = nullptr,
                                  PTexture giTexture = nullptr,
                                  int lightPassMode = 0);
        bool ensureLocalReflectionProbeResources(int faceSize);
        void clearLocalReflectionProbe();
        void releaseLocalReflectionProbeResources();
        void clearPlanarReflection();
        bool ensurePlanarReflectionResources(int width, int height);
        bool updatePlanarReflection(PScreen screen, PCamera cam);
        bool updateLocalReflectionProbe(PScreen screen, PCamera cam);
        /**
         * @brief Runs the full deferred rendering pipeline.
         * @param screen Destination screen.
         * @param cam Active camera.
         */
        void renderDeferred(PScreen screen, PCamera cam);
        /**
         * @brief Draws a screen-space outline around the selected entity.
         * @param screen Destination screen/draw buffer owner.
         * @param cam Active camera.
         */
        void drawOutlines(PScreen screen, PCamera cam);

        /**
         * @brief Uploads current scene lights to rendering systems.
         */
        void updateSceneLights();
        /**
         * @brief Executes the primary 3D render pass.
         */
        void render3DPass();
        /**
         * @brief Draws model render items for a selected filter.
         * @param cam Active camera.
         * @param filter Opaque/transparent filter selection.
         * @param skipDeferredCompatible True to skip deferred-compatible items.
         * @param excludedEntityId Optional entity id to exclude from the draw.
         */
        void drawModels3D(PCamera cam,
                          RenderFilter filter = RenderFilter::All,
                          bool skipDeferredCompatible = false,
                          const std::string* excludedEntityId = nullptr);
        /**
         * @brief Renders shadow maps for shadow-casting lights.
         */
        void drawShadowsPass();
        /**
         * @brief Draws the active skybox.
         * @param cam Active camera.
         * @param depthTested True to keep depth testing enabled while drawing.
         */
        void drawSkybox(PCamera cam, bool depthTested = false);
        /**
         * @brief Ensures the scene is subscribed to asset-change events.
         */
        void ensureAssetChangeListenerRegistered();
        /**
         * @brief Handles a changed asset by reloading dependent runtime state.
         * @param assetRequest Original asset request or path passed to the notifier.
         * @param cacheKey Normalized cache key for the changed asset.
         */
        virtual void handleAssetChanged(const std::string& assetRequest, const std::string& cacheKey);

    public:
        /**
         * @brief Sets the currently selected entity id.
         * @param id Entity id string.
         */
        void setSelectedEntityId(const std::string& id) { selectedEntityId = id; }
        /**
         * @brief Returns the currently selected entity id.
         * @return Selected entity id.
         */
        const std::string& getSelectedEntityId() const { return selectedEntityId; }
        /**
         * @brief Enables or disables outline rendering.
         * @param enabled True to enable outlines.
         */
        void setOutlineEnabled(bool enabled) { outlineEnabled = enabled; }
        /**
         * @brief Returns whether outline rendering is enabled.
         * @return True when outlines are enabled.
         */
        bool isOutlineEnabled() const { return outlineEnabled; }
        /**
         * @brief Sets a preferred camera for rendering and optional post-process updates.
         * @param cam Preferred camera.
         * @param applyToScreen True to apply camera effects immediately.
         */
        void setPreferredCamera(PCamera cam, bool applyToScreen = true);
        /**
         * @brief Returns the preferred camera.
         * @return Preferred camera pointer.
         */
        PCamera getPreferredCamera() const { return preferredCamera; }
        /**
         * @brief Returns world matrix for an entity.
         * @param entity Entity to evaluate.
         * @param manager ECS component manager.
         * @return World transform matrix.
         */
        Math3D::Mat4 getWorldMatrix(NeoECS::ECSEntity* entity, NeoECS::ECSComponentManager* manager) const { return buildWorldMatrix(entity, manager); }
        /**
         * @brief Returns world-space position for an entity.
         * @param entity Entity to evaluate.
         * @return World-space position.
         */
        Math3D::Vec3 getWorldPosition(NeoECS::ECSEntity* entity) const;
};

/// @brief Represents the Scene3D type.
class Scene3D : public Scene {
    public:
        using Scene::Scene;
        /**
         * @brief Destroys this Scene3D instance.
         */
        ~Scene3D() override = default;
};

/// @brief Represents the Scene2D type.
class Scene2D : public Scene {
    public:
        using Scene::Scene;
        /**
         * @brief Destroys this Scene2D instance.
         */
        ~Scene2D() override = default;
};

#endif // SCENE_H
