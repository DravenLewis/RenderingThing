#ifndef DEMOSCENE_H
#define DEMOSCENE_H

#include "Scene/Scene.h"

#include <memory>

#include "Scene/Camera.h"
#include "Rendering/Core/Screen.h"
#include "Rendering/Core/Graphics2D.h"
#include "Scene/FirstPersonController.h"
#include "Rendering/Geometry/Model.h"
#include "Rendering/Textures/SkyBox.h"
#include "Debug/FrameTimeGraph.h"

namespace NeoECS {
    class GameObject;
}
struct CameraComponent;
struct TransformComponent;

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
        PModel renderBox;

        PModel orb;
        PSkyBox skybox;

        NeoECS::GameObject* groundObject = nullptr;
        NeoECS::GameObject* meshObject = nullptr;
        NeoECS::GameObject* cubeObject = nullptr;
        NeoECS::GameObject* lucilleObject = nullptr;
        NeoECS::GameObject* orbObject = nullptr;
        NeoECS::GameObject* demoCameraObject = nullptr;
        NeoECS::GameObject* renderBoxGameObject = nullptr;

        CameraComponent* demoCameraComponent = nullptr;
        TransformComponent* demoCameraTransform = nullptr;

        PCamera cam;
        PScreen uiScreen;
        PScreen mainScreen;

        std::shared_ptr<FirstPersonController> fpsController;
        std::shared_ptr<Graphics2D> graphics2d;

        float totalTime = 0.0f;
        float lastDeltaTime = 0.0f;
        float frameTimeSpike = 0.0f;
        float fpsSampleAccumTime = 0.0f;
        int fpsSampleAccumFrames = 0;
        float displayedAverageFps = 0.0f;
        float displayedAverageFrameMs = 0.0f;
        bool showDebugWidgets = false;
        bool showDebugShadows = false;

        FrameTimeGraph frameTimeGraph;

        std::shared_ptr<IEventHandler> sceneInputHandler;
};

#endif // DEMOSCENE_H
