#ifndef DEMOSCENE_H
#define DEMOSCENE_H

#include "Scene.h"

#include <memory>

#include "Camera.h"
#include "Screen.h"
#include "Graphics2D.h"
#include "FirstPersonController.h"
#include "Model.h"
#include "SkyBox.h"
#include "debug_helpers/FrameTimeGraph.h"

class DemoScene : public Scene3D {
    public:
        explicit DemoScene(RenderWindow* window);
        ~DemoScene() override = default;

        void init() override;
        void update(float deltaTime) override;
        void render() override;
        void dispose() override;
        void setInputManager(std::shared_ptr<InputManager> manager) override;

        void toggleDebugWidgets();
        void toggleDebugShadows();

    private:
        PModel ground;
        PModel meshModel;
        PModel cubeModel;
        PModel lucille;
        PModel orb;
        PSkyBox skybox;

        PCamera cam;
        PScreen uiScreen;
        PScreen mainScreen;

        std::shared_ptr<FirstPersonController> fpsController;
        std::shared_ptr<Graphics2D> graphics2d;

        float totalTime = 0.0f;
        float lastDeltaTime = 0.0f;
        float frameTimeSpike = 0.0f;
        bool showDebugWidgets = false;
        bool showDebugShadows = false;

        FrameTimeGraph frameTimeGraph;

        std::shared_ptr<IEventHandler> sceneInputHandler;
};

#endif // DEMOSCENE_H
