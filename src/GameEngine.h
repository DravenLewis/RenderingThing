#ifndef GAMEENGINE_H
#define GAMEENGINE_H

#include "InputManager.h"
#include "RenderWindow.h"


#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

#include "Scene.h"


class GameEngine{
    private:
        std::shared_ptr<RenderWindow> windowPtr;
        std::shared_ptr<InputManager> inputManager;
        DisplayMode windowDisplayMode;
        std::string windowInitialTitle;

        std::thread mainRenderThread;
        std::atomic<bool> running{false};
        std::atomic<bool> renderReady{false};

        std::mutex sceneMutex;
        std::condition_variable initCv;
        std::mutex initMutex;

        struct StateEntry{
            PScene scene;
            bool initialized = false;
        };

        std::unordered_map<int, StateEntry> states;
        int nextStateId = 1;
        int activeStateId = -1;
        std::atomic<int> pendingStateId{-1};

        PScene activeScene;

        void init(); // Initialize The Engine
        void run();
        void tick(float deltaTime); // Update the Engine (Delta Time Interval using nano time)
        void render(); // Update as fast as possible.
        void processEvents(SDL_Event& event); // Handle all events
        void dispose(); // Clean up the engine;
    public:

        GameEngine(DisplayMode displayMode = DisplayMode::New(), const std::string& windowTitle = "[Untitled Game Engine Game]") :
            windowDisplayMode(displayMode),
            windowInitialTitle(windowTitle)
        {
            GameEngine::Engine = this;
        };

        ~GameEngine(){
            if(GameEngine::Engine == this) GameEngine::Engine = nullptr;
            if(running){
                exit(0);
            }
        }

        RenderWindow* window() const { return this->windowPtr.get(); }
        InputManager* input() const {return this->inputManager.get(); }

        int addState(PScene scene);
        bool enterState(int id);

        void start();
        void exit(int code = 0);

        static GameEngine* Engine;
};

#endif//GAMEENGINE_H
