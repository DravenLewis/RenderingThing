/**
 * @file src/App/Demo/DemoScene.h
 * @brief Declarations for DemoScene.
 */

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
#include "Debug/ProfilerPieChart.h"

namespace NeoECS {
    class GameObject;
}
struct CameraComponent;
struct TransformComponent;

/// @brief Represents the DemoScene type.
class DemoScene : public Scene3D {
    public:
        /**
         * @brief Constructs a new DemoScene instance.
         * @param window Value for window.
          * @return Result of this operation.
         */
        explicit DemoScene(RenderWindow* window);
        /**
         * @brief Destroys this DemoScene instance.
         */
        ~DemoScene() override = default;

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
         * @brief Disposes this object.
         */
        void dispose() override;
        /**
         * @brief Sets the input manager.
         * @param manager Value for manager.
         */
        void setInputManager(std::shared_ptr<InputManager> manager) override;

        /**
         * @brief Converts to ggle debug widgets.
         */
        void toggleDebugWidgets();
        /**
         * @brief Converts to ggle debug shadows.
         */
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
        float displayedAverageFps = 0.0f;
        float displayedAverageFrameMs = 0.0f;
        bool showDebugWidgets = false;
        bool showDebugShadows = false;

        FrameTimeGraph frameTimeGraph;
        ProfilerPieChart profilerPieChart;

        std::shared_ptr<IEventHandler> sceneInputHandler;
};

#endif // DEMOSCENE_H
