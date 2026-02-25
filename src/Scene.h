#ifndef SCENE_H
#define SCENE_H

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "View.h"
#include "InputManager.h"
#include "Model.h"
#include "Light.h"
#include "neoecs.hpp"
#include "MaterialDefaults.h"

class Scene;
typedef std::shared_ptr<Scene> PScene;
class ShaderProgram;

class Scene : public View {
    public:
        explicit Scene(RenderWindow* window = nullptr);
        virtual ~Scene() = default;

        virtual void init() {}
        virtual void update(float deltaTime) {}
        virtual void render() {}
        virtual void dispose();
        virtual void setInputManager(std::shared_ptr<InputManager> manager) { inputManager = manager; }
        virtual bool switchState(PScene newState, PScene oldState) { return true; }

        void updateECS(float deltaTime);

        NeoECS::NeoECS* getECS() const { return ecsInstance; }
        NeoECS::NeoAPI* getECSAPI() const { return ecsAPI; }
        NeoECS::GameObject* createECSGameObject(const std::string& name, NeoECS::GameObject* parent = nullptr);
        bool destroyECSGameObject(NeoECS::GameObject* object);
        NeoECS::GameObject* createModelGameObject(const std::string& name, const PModel& model, NeoECS::GameObject* parent = nullptr);
        NeoECS::GameObject* createLightGameObject(const std::string& name, const Light& light, NeoECS::GameObject* parent = nullptr, bool syncTransform = true, bool syncDirection = false);
        NeoECS::GameObject* createCameraGameObject(const std::string& name, NeoECS::GameObject* parent = nullptr);
        NeoECS::GameObject* getSceneRootGameObject() const { return sceneRootObject; }
        NeoECS::ECSEntity* getSceneRootEntity() const;
        bool isSceneRootEntity(NeoECS::ECSEntity* entity) const;

        struct DebugStats {
            std::atomic<float> snapshotMs{0.0f};
            std::atomic<float> shadowMs{0.0f};
            std::atomic<float> drawMs{0.0f};
            std::atomic<int> drawCount{0};
            std::atomic<int> lightCount{0};
        };

        const DebugStats& getDebugStats() const { return debugStats; }
        virtual void requestClose();
        virtual bool consumeCloseRequest();

    protected:
        Math3D::Mat4 buildWorldMatrix(NeoECS::ECSEntity* entity, NeoECS::ECSComponentManager* manager) const;

        std::shared_ptr<InputManager> inputManager;
        NeoECS::NeoECS* ecsInstance = nullptr;
        NeoECS::NeoAPI* ecsAPI = nullptr;
        NeoECS::GameObject* sceneRootObject = nullptr;

        struct RenderItem {
            std::shared_ptr<Mesh> mesh;
            std::shared_ptr<Material> material;
            Math3D::Mat4 model;
            bool enableBackfaceCulling = true;
            bool isTransparent = false;
            bool isDeferredCompatible = false;
            std::string entityId;
            bool castsShadows = true;
            bool hasBounds = false;
            Math3D::Vec3 boundsMin = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 boundsMax = Math3D::Vec3(0.0f, 0.0f, 0.0f);
        };

        struct RenderSnapshot {
            std::vector<RenderItem> drawItems;
            std::vector<Light> lights;
        };

        std::array<RenderSnapshot, 2> renderSnapshots{};
        std::atomic<int> renderSnapshotIndex{0};
        DebugStats debugStats{};
        std::atomic<bool> closeRequested{false};
        std::string selectedEntityId;
        std::shared_ptr<MaterialDefaults::ColorMaterial> outlineMaterial;
        std::shared_ptr<MaterialDefaults::ColorMaterial> deferredIncompatibleMaterial;
        bool outlineEnabled = false;
        PFrameBuffer gBuffer;
        std::shared_ptr<ShaderProgram> gBufferShader;
        std::shared_ptr<ShaderProgram> deferredLightShader;
        std::shared_ptr<ModelPart> deferredQuad;
        int gBufferWidth = 0;
        int gBufferHeight = 0;
        bool gBufferValidationDirty = true;
        bool gBufferValidated = false;
        bool deferredDisabled = false;
        PCamera preferredCamera;

        enum class RenderFilter{
            All,
            Opaque,
            Transparent
        };

        bool isMaterialTransparent(const std::shared_ptr<Material>& material) const;
        bool isDeferredCompatibleMaterial(const std::shared_ptr<Material>& material) const;
        void ensureDeferredResources(PScreen screen);
        void updateActiveCameraEffects(NeoECS::ECSEntity* activeCameraEntity, NeoECS::ECSComponentManager* manager);
        void drawDeferredGeometry(PCamera cam);
        void drawDeferredLighting(PScreen screen, PCamera cam);
        void renderDeferred(PScreen screen, PCamera cam);
        void drawOutlines(PCamera cam);

        void updateSceneLights();
        void render3DPass();
        void drawModels3D(PCamera cam, RenderFilter filter = RenderFilter::All, bool drawOutlines = true, bool skipDeferredCompatible = false);
        void drawShadowsPass();
        void drawSkybox(PCamera cam, bool depthTested = false);

    public:
        void setSelectedEntityId(const std::string& id) { selectedEntityId = id; }
        const std::string& getSelectedEntityId() const { return selectedEntityId; }
        void setOutlineEnabled(bool enabled) { outlineEnabled = enabled; }
        bool isOutlineEnabled() const { return outlineEnabled; }
        void setPreferredCamera(PCamera cam, bool applyToScreen = true);
        PCamera getPreferredCamera() const { return preferredCamera; }
        Math3D::Mat4 getWorldMatrix(NeoECS::ECSEntity* entity, NeoECS::ECSComponentManager* manager) const { return buildWorldMatrix(entity, manager); }
        Math3D::Vec3 getWorldPosition(NeoECS::ECSEntity* entity) const;
};

class Scene3D : public Scene {
    public:
        using Scene::Scene;
        ~Scene3D() override = default;
};

class Scene2D : public Scene {
    public:
        using Scene::Scene;
        ~Scene2D() override = default;
};

#endif // SCENE_H
