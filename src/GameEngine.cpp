
#include "GameEngine.h"

#include <chrono>

#include "DefaultState.h"
#include "Logbot.h"
#include "ImGuiLayer.h"
#include "EditorScene.h"
#include "CrashReporter.h"

GameEngine* GameEngine::Engine = nullptr;

void GameEngine::init(){
    windowPtr = std::make_shared<RenderWindow>(windowInitialTitle, windowDisplayMode);
    inputManager = std::make_shared<InputManager>(windowPtr, true);

    windowPtr->addWindowEventHandler([this](SDL_Event& event){
        processEvents(event);
    });

    ImGuiLayer::Init(windowPtr.get());
    CrashReporter::Install();

    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        for(auto& kvp : states){
            if(kvp.second.scene){
                kvp.second.scene->attachWindow(windowPtr.get());
                kvp.second.scene->setInputManager(inputManager);
            }
        }

        if(activeStateId == -1){
            int requestedId = pendingStateId.load();
            auto requestedIt = (requestedId != -1) ? states.find(requestedId) : states.end();

            if(requestedIt != states.end()){
                activeStateId = requestedId;
                activeScene = requestedIt->second.scene;
                pendingStateId = -1;
            }else if(!states.empty()){
                activeStateId = states.begin()->first;
                activeScene = states.begin()->second.scene;
            }else{
                auto defaultState = std::make_shared<DefaultState>(windowPtr.get());
                int defaultId = nextStateId++;
                states.emplace(defaultId, StateEntry{defaultState, false});
                defaultState->attachWindow(windowPtr.get());
                defaultState->setInputManager(inputManager);
                activeStateId = defaultId;
                activeScene = defaultState;
            }
        }else{
            auto it = states.find(activeStateId);
            if(it != states.end()){
                activeScene = it->second.scene;
            }
        }

        if(activeScene){
            auto it = states.find(activeStateId);
            if(it != states.end() && !it->second.initialized){
                activeScene->init();
                it->second.initialized = true;
            }
        }
    }
} // Initialize The Engine

void GameEngine::tick(float deltaTime){
    std::lock_guard<std::mutex> lock(sceneMutex);
    if(activeScene){
        if(activeScene->consumeCloseRequest()){
            LogBot.Log(LOG_WARN, "GameEngine::tick() close requested by active scene.");
            if(windowPtr){
                windowPtr->close();
            }
            running = false;
            return;
        }
        if(!CrashReporter::IsCrashed()){
            try{
                activeScene->updateECS(deltaTime);
                activeScene->update(deltaTime);
            }catch(const std::exception& e){
                CrashReporter::ReportCrash(e.what());
            }catch(...){
                CrashReporter::ReportCrash("Unknown exception in tick");
            }
        }
    }
} // Update the Engine (Delta Time Interval using nano time)

void GameEngine::render(){
    std::lock_guard<std::mutex> lock(sceneMutex);
    int pendingId = pendingStateId.exchange(-1);
    if(pendingId != -1 && pendingId != activeStateId){
        auto it = states.find(pendingId);
        if(it != states.end() && it->second.scene){
            if(activeScene){
                activeScene->dispose();
            }

            activeStateId = pendingId;
            activeScene = it->second.scene;
            activeScene->attachWindow(windowPtr.get());
            activeScene->setInputManager(inputManager);

            if(!it->second.initialized){
                activeScene->init();
                it->second.initialized = true;
            }
        }
    }

    ImGuiLayer::BeginFrame();

    if(activeScene && !CrashReporter::IsCrashed()){
        try{
            activeScene->render();
            activeScene->drawToWindow();
        }catch(const std::exception& e){
            CrashReporter::ReportCrash(e.what());
        }catch(...){
            CrashReporter::ReportCrash("Unknown exception in render");
        }
    }

    CrashReporter::RenderImGui();
    ImGuiLayer::EndFrame();
} // Update as fast as possible.

void GameEngine::processEvents(SDL_Event& event){
    if(event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED){
        LogBot.Log(LOG_WARN, "GameEngine::processEvents() close event type=%d", (int)event.type);
        if(activeScene){
            activeScene->requestClose();
            if(auto editor = std::dynamic_pointer_cast<EditorScene>(activeScene)){
                editor->handleQuitRequest();
                return;
            }
        }
        return;
    }

    if(event.type == SDL_EVENT_WINDOW_RESIZED){
        std::lock_guard<std::mutex> lock(sceneMutex);
        if(activeScene && windowPtr){
            activeScene->resize(windowPtr->getWindowWidth(), windowPtr->getWindowHeight());
        }
        return;
    }
} // Handle all events

void GameEngine::dispose(){
    std::lock_guard<std::mutex> lock(sceneMutex);
    if(activeScene){
        activeScene->dispose();
        activeScene.reset();
    }

    ImGuiLayer::Shutdown();

    if(windowPtr){
        windowPtr->dispose();
        windowPtr.reset();
    }
} // Clean up the engine;

int GameEngine::addState(PScene scene){
    if(!scene) return -1;

    std::lock_guard<std::mutex> lock(sceneMutex);
    int id = nextStateId++;
    states.emplace(id, StateEntry{scene, false});

    if(windowPtr){
        scene->attachWindow(windowPtr.get());
    }
    if(inputManager){
        scene->setInputManager(inputManager);
    }

    return id;
}

bool GameEngine::enterState(int id){
    std::lock_guard<std::mutex> lock(sceneMutex);
    auto it = states.find(id);
    if(it == states.end() || !it->second.scene) return false;

    PScene newScene = it->second.scene;
    PScene oldScene = activeScene;

    if(activeStateId == id){
        return true;
    }

    if(oldScene && !oldScene->switchState(newScene, oldScene)){
        return false;
    }

    if(newScene && !newScene->switchState(newScene, oldScene)){
        return false;
    }

    pendingStateId = id;
    return true;
}

void GameEngine::run(){ // Render Thread.
    try{
        init();
    }catch(const std::exception& e){
        LogBot.Log(LOG_ERRO, "Exception caught in GameEngine::init: %s", e.what());
        running = false;
    }catch(...){
        LogBot.Log(LOG_FATL, "Unknown exception caught in GameEngine::init");
        running = false;
    }

    {
        std::lock_guard<std::mutex> lock(initMutex);
        renderReady = true;
    }
    initCv.notify_one();

    while(running){
        if(windowPtr){
            windowPtr->process();
        }

        render();

        if(windowPtr){
            windowPtr->swap();
            if(windowPtr->isCloseRequested()){
                LogBot.Log(LOG_WARN, "GameEngine::run() detected window close request.");
                running = false;
            }
        }
    }

    dispose();
}

void GameEngine::start(){ // Local Logic Thread.
    if(running) return;
    if(GameEngine::Engine != this) GameEngine::Engine = this;

    running = true;
    renderReady = false;

    mainRenderThread = std::thread(&GameEngine::run, this);

    {
        std::unique_lock<std::mutex> lock(initMutex);
        initCv.wait(lock, [&](){ return renderReady.load(); });
    }

    using clock = std::chrono::steady_clock;
    auto last = clock::now();

    while(running){
        auto now = clock::now();
        float deltaTime = std::chrono::duration<float>(now - last).count();
        last = now;

        tick(deltaTime);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if(mainRenderThread.joinable()){
        mainRenderThread.join();
    }
}

void GameEngine::exit(int code){
    LogBot.Log(LOG_WARN, "GameEngine::exit(%d) called.", code);
    running = false;

    if(mainRenderThread.joinable() && std::this_thread::get_id() != mainRenderThread.get_id()){
        mainRenderThread.join();
    }
}
