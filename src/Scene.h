#ifndef SCENE_H
#define SCENE_H

#include <memory>
#include <vector>

#include "View.h"
#include "InputManager.h"
#include "Model.h"
#include "Light.h"
#include "neoecs.hpp"

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

    protected:
        std::shared_ptr<InputManager> inputManager;
        NeoECS::NeoECS* ecsInstance = nullptr;
        NeoECS::NeoAPI* ecsAPI = nullptr;

        void updateSceneLights();
        void render3DPass();
        void drawModels3D(PCamera cam);
        void drawShadowsPass();
        void drawSkybox(PCamera cam);
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
