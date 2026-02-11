#ifndef SCENE_H
#define SCENE_H

#include <memory>
#include <vector>

#include "View.h"
#include "Model.h"
#include "Light.h"
#include "InputManager.h"

class Scene;
typedef std::shared_ptr<Scene> PScene;

class Scene : public View {
    public:
        explicit Scene(RenderWindow* window = nullptr);
        virtual ~Scene() = default;

        virtual void init() {}
        virtual void update(float deltaTime) {}
        virtual void render() {}
        virtual void dispose() {}
        virtual void setInputManager(std::shared_ptr<InputManager> manager) { inputManager = manager; }
        virtual bool switchState(PScene newState, PScene oldState) { return true; }

        void addModel(const PModel& model);
        void clearModels();
        const std::vector<PModel>& getModels() const { return models; }

        void addLight(const Light& light);
        void clearLights();

    protected:
        std::vector<PModel> models;
        std::shared_ptr<InputManager> inputManager;
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
