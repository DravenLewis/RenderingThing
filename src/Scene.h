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

        struct DebugStats {
            std::atomic<float> snapshotMs{0.0f};
            std::atomic<float> shadowMs{0.0f};
            std::atomic<float> drawMs{0.0f};
            std::atomic<int> drawCount{0};
            std::atomic<int> lightCount{0};
        };

        const DebugStats& getDebugStats() const { return debugStats; }

    protected:
        Math3D::Mat4 buildWorldMatrix(NeoECS::ECSEntity* entity, NeoECS::ECSComponentManager* manager) const;

        std::shared_ptr<InputManager> inputManager;
        NeoECS::NeoECS* ecsInstance = nullptr;
        NeoECS::NeoAPI* ecsAPI = nullptr;

        struct RenderItem {
            std::shared_ptr<Mesh> mesh;
            std::shared_ptr<Material> material;
            Math3D::Mat4 model;
            bool enableBackfaceCulling = true;
            std::string entityId;
        };

        struct RenderSnapshot {
            std::vector<RenderItem> drawItems;
            std::vector<Light> lights;
        };

        std::array<RenderSnapshot, 2> renderSnapshots{};
        std::atomic<int> renderSnapshotIndex{0};
        DebugStats debugStats{};
        std::string selectedEntityId;
        std::shared_ptr<MaterialDefaults::ColorMaterial> outlineMaterial;
        bool outlineEnabled = false;

        void updateSceneLights();
        void render3DPass();
        void drawModels3D(PCamera cam);
        void drawShadowsPass();
        void drawSkybox(PCamera cam);

    public:
        void setSelectedEntityId(const std::string& id) { selectedEntityId = id; }
        const std::string& getSelectedEntityId() const { return selectedEntityId; }
        void setOutlineEnabled(bool enabled) { outlineEnabled = enabled; }
        bool isOutlineEnabled() const { return outlineEnabled; }
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
