#ifndef SCENE_H
#define SCENE_H

#include <memory>
#include <vector>

#include "View.h"
#include "SceneObject.h"
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

        void addSceneObject(const PSceneObject& object);
        void removeSceneObject(const PSceneObject& object);
        void clearSceneObjects();
        const std::vector<PSceneObject>& getSceneObjects() const { return sceneObjects; }

        void addModel(const PModel& model);
        void removeModel(const PModel& model);
        void removeModelObject(const PModelSceneObject& object);
        void clearModels();
        std::vector<PModel> getModels() const;

        void addLight(const Light& light);
        void removeLightObject(const PLightSceneObject& object);
        void clearLights();

    protected:
        std::vector<PSceneObject> sceneObjects;
        std::shared_ptr<InputManager> inputManager;
        bool manageSceneLights = false;

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
