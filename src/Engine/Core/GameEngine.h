/**
 * @file src/Engine/Core/GameEngine.h
 * @brief Declarations for GameEngine.
 */

#ifndef GAMEENGINE_H
#define GAMEENGINE_H

#include "Platform/Input/InputManager.h"
#include "Platform/Window/RenderWindow.h"


#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

#include "Scene/Scene.h"


/// @brief Enumerates values for EngineRenderStrategy.
enum class EngineRenderStrategy{
    Forward = 0,
    Deferred = 1
};

/// @brief Represents the GameEngine type.
class GameEngine{
    private:
        static constexpr int kFrameCapUncapped = -1;
        static constexpr int kFrameCapMin = 1;
        static constexpr int kFrameCapMax = 400;
        static constexpr int kFixedUpdateRateMin = 15;
        static constexpr int kFixedUpdateRateMax = 400;
        static constexpr int kDefaultFixedUpdateRate = 60;
        static constexpr int kMaxFixedTicksPerCycle = 4;
        static constexpr float kMaxAccumulatedDeltaSeconds = 0.25f;

        std::shared_ptr<RenderWindow> windowPtr;
        std::shared_ptr<InputManager> inputManager;
        DisplayMode windowDisplayMode;
        std::string windowInitialTitle;

        std::thread mainRenderThread;
        std::atomic<bool> running{false};
        std::atomic<bool> renderReady{false};

        std::mutex sceneMutex;
        std::mutex sceneExecutionMutex;
        std::condition_variable initCv;
        std::mutex initMutex;

        /// @brief Holds data for StateEntry.
        struct StateEntry{
            PScene scene;
            bool initialized = false;
        };

        std::unordered_map<int, StateEntry> states;
        int nextStateId = 1;
        int activeStateId = -1;
        std::atomic<int> pendingStateId{-1};
        std::atomic<int> requestedVSyncMode{static_cast<int>(VSyncMode::Off)};
        std::atomic<int> appliedVSyncMode{static_cast<int>(VSyncMode::Off)};
        std::atomic<int> frameCapFps{kFrameCapUncapped};
        std::atomic<int> fixedUpdateRateHz{kDefaultFixedUpdateRate};

        PScene activeScene;
        EngineRenderStrategy renderStrategy = EngineRenderStrategy::Forward;

        /// @brief Holds data for RuntimeDebugStats.
        struct RuntimeDebugStats{
            std::atomic<float> updateMs{0.0f};
            std::atomic<float> updateWaitMs{0.0f};
            std::atomic<float> updateWorkMs{0.0f};
            std::atomic<float> renderMs{0.0f};
            std::atomic<float> renderWaitMs{0.0f};
            std::atomic<float> renderWorkMs{0.0f};
            std::atomic<float> renderSceneMs{0.0f};
            std::atomic<float> renderBlitMs{0.0f};
            std::atomic<float> renderImGuiMs{0.0f};
            std::atomic<float> swapMs{0.0f};
        };

        RuntimeDebugStats runtimeDebugStats{};

        /**
         * @brief Initializes this object.
         */
        void init(); // Initialize The Engine
        /**
         * @brief Runs the engine loop until shutdown.
         */
        void run();
        /**
         * @brief Applies a pending VSync mode change to the window.
         */
        void applyPendingVSyncMode();
        /**
         * @brief Executes fixed-timestep updates for the current frame.
         * @param frameDeltaSeconds Frame delta time in seconds.
         * @param accumulatorSeconds Time accumulator used for fixed-step simulation.
         */
        void stepFixedUpdates(float frameDeltaSeconds, float& accumulatorSeconds);
        /**
         * @brief Advances the active scene by one variable-timestep update.
         * @param deltaTime Delta time in seconds.
         */
        void tick(float deltaTime); // Update the Engine (Delta Time Interval using nano time)
        /**
         * @brief Renders one frame for the active scene.
         */
        void render(); // Update as fast as possible.
        /**
         * @brief Dispatches a single SDL event to engine systems.
         * @param event Event to process.
         */
        void processEvents(SDL_Event& event); // Handle all events
        /**
         * @brief Releases engine-owned resources.
         */
        void dispose(); // Clean up the engine;
    public:

        /**
         * @brief Constructs a new GameEngine instance.
         * @param displayMode Mode or type selector.
         * @param windowTitle Value for window title.
         */
        GameEngine(DisplayMode displayMode = DisplayMode::New(), const std::string& windowTitle = "[Untitled Game Engine Game]") :
            windowDisplayMode(displayMode),
            windowInitialTitle(windowTitle),
            requestedVSyncMode(static_cast<int>(displayMode.vSyncMode)),
            appliedVSyncMode(static_cast<int>(displayMode.vSyncMode))
        {
            GameEngine::Engine = this;
        };

        /**
         * @brief Destroys this GameEngine instance.
         */
        ~GameEngine(){
            if(GameEngine::Engine == this) GameEngine::Engine = nullptr;
            if(running){
                exit(0);
            }
        }

        /**
         * @brief Returns the engine render window.
         * @return Raw pointer to the managed render window.
         */
        RenderWindow* window() const { return this->windowPtr.get(); }
        /**
         * @brief Returns the engine input manager.
         * @return Raw pointer to the managed input manager.
         */
        InputManager* input() const {return this->inputManager.get(); }

        /**
         * @brief Sets the renderer strategy used by the active scene.
         * @param strategy Rendering path to use.
         */
        void setRenderStrategy(EngineRenderStrategy strategy) { renderStrategy = strategy; }
        /**
         * @brief Returns the active renderer strategy.
         * @return Current renderer strategy.
         */
        EngineRenderStrategy getRenderStrategy() const { return renderStrategy; }
        /**
         * @brief Returns runtime timing counters for update/render stages.
         * @return Reference to live runtime debug statistics.
         */
        const RuntimeDebugStats& getRuntimeDebugStats() const { return runtimeDebugStats; }
        /// @brief Returns the last update duration in milliseconds.
        float getLastUpdateMs() const { return runtimeDebugStats.updateMs.load(std::memory_order_relaxed); }
        /// @brief Returns the time spent waiting during the last update in milliseconds.
        float getLastUpdateWaitMs() const { return runtimeDebugStats.updateWaitMs.load(std::memory_order_relaxed); }
        /// @brief Returns the active work time during the last update in milliseconds.
        float getLastUpdateWorkMs() const { return runtimeDebugStats.updateWorkMs.load(std::memory_order_relaxed); }
        /// @brief Returns the last render duration in milliseconds.
        float getLastRenderMs() const { return runtimeDebugStats.renderMs.load(std::memory_order_relaxed); }
        /// @brief Returns the time spent waiting during the last render in milliseconds.
        float getLastRenderWaitMs() const { return runtimeDebugStats.renderWaitMs.load(std::memory_order_relaxed); }
        /// @brief Returns the active work time during the last render in milliseconds.
        float getLastRenderWorkMs() const { return runtimeDebugStats.renderWorkMs.load(std::memory_order_relaxed); }
        /// @brief Returns the last scene-render stage duration in milliseconds.
        float getLastRenderSceneMs() const { return runtimeDebugStats.renderSceneMs.load(std::memory_order_relaxed); }
        /// @brief Returns the last frame-buffer blit stage duration in milliseconds.
        float getLastRenderBlitMs() const { return runtimeDebugStats.renderBlitMs.load(std::memory_order_relaxed); }
        /// @brief Returns the last ImGui-render stage duration in milliseconds.
        float getLastRenderImGuiMs() const { return runtimeDebugStats.renderImGuiMs.load(std::memory_order_relaxed); }
        /// @brief Returns the last swap-buffers duration in milliseconds.
        float getLastSwapMs() const { return runtimeDebugStats.swapMs.load(std::memory_order_relaxed); }

        static constexpr int FrameCapUncapped = kFrameCapUncapped;
        static constexpr int FrameCapMin = kFrameCapMin;
        static constexpr int FrameCapMax = kFrameCapMax;
        /**
         * @brief Requests a VSync mode change.
         * @param mode VSync mode to apply.
         * @return True if the request is accepted.
         */
        bool setVSyncMode(VSyncMode mode);
        /**
         * @brief Returns the currently applied VSync mode.
         * @return Active VSync mode.
         */
        VSyncMode getVSyncMode() const;
        /**
         * @brief Sets the engine frame cap.
         * @param fps Target frames per second, or `FrameCapUncapped`.
         * @return True if the value is within supported bounds.
         */
        bool setFrameCap(int fps);
        /**
         * @brief Returns the current frame cap setting.
         * @return Configured frame cap.
         */
        int getFrameCap() const;
        /**
         * @brief Sets the fixed update rate used for simulation steps.
         * @param hz Fixed-step frequency in Hertz.
         * @return True if the value is within supported bounds.
         */
        bool setFixedUpdateRate(int hz);
        /**
         * @brief Returns the fixed update rate.
         * @return Fixed-step frequency in Hertz.
         */
        int getFixedUpdateRate() const;
        /**
         * @brief Returns the fixed-step duration in seconds.
         * @return Fixed update step size.
         */
        float getFixedUpdateStepSeconds() const;

        /**
         * @brief Registers a scene state and returns its state id.
         * @param scene Scene instance to register.
         * @return Assigned state id, or a negative value on failure.
         */
        int addState(PScene scene);
        /**
         * @brief Requests a transition to a registered scene state.
         * @param id Target state id.
         * @return True if the request is accepted.
         */
        bool enterState(int id);

        /**
         * @brief Starts the engine and enters the main loop.
         */
        void start();
        /**
         * @brief Requests engine shutdown.
         * @param code Process exit code.
         */
        void exit(int code = 0);

        /// @brief Global pointer to the active engine instance.
        static GameEngine* Engine;
};

#endif//GAMEENGINE_H
