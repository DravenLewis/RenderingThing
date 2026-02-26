
#include "Engine/Core/GameEngine.h"

#include <chrono>

#include "App/Demo/DefaultState.h"
#include "Foundation/Logging/Logbot.h"
#include "Editor/Core/ImGuiLayer.h"
#include "Editor/Core/EditorScene.h"
#include "Platform/Crash/CrashReporter.h"
#include "Rendering/Textures/Texture.h"

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
    using clock = std::chrono::steady_clock;
    auto tickStart = clock::now();
    float execWaitMs = 0.0f;

    auto storeTickStats = [&](const clock::time_point& tickEnd){
        const float totalMs = std::chrono::duration<float, std::milli>(tickEnd - tickStart).count();
        const float workMs = (totalMs > execWaitMs) ? (totalMs - execWaitMs) : 0.0f;
        runtimeDebugStats.updateMs.store(totalMs, std::memory_order_relaxed);
        runtimeDebugStats.updateWaitMs.store(execWaitMs, std::memory_order_relaxed);
        runtimeDebugStats.updateWorkMs.store(workMs, std::memory_order_relaxed);
    };

    PScene scene;
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        scene = activeScene;
    }

    if(scene){
        auto execWaitStart = clock::now();
        std::unique_lock<std::mutex> execLock(sceneExecutionMutex);
        auto execWaitEnd = clock::now();
        execWaitMs = std::chrono::duration<float, std::milli>(execWaitEnd - execWaitStart).count();

        bool sceneStillActive = false;
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            sceneStillActive = (scene && activeScene == scene);
        }

        if(sceneStillActive){
            if(scene->consumeCloseRequest()){
                LogBot.Log(LOG_WARN, "GameEngine::tick() close requested by active scene.");
                if(windowPtr){
                    windowPtr->close();
                }
                running = false;
                auto tickEnd = clock::now();
                storeTickStats(tickEnd);
                return;
            }
            if(!CrashReporter::IsCrashed()){
                try{
                    scene->updateECS(deltaTime);
                    scene->update(deltaTime);
                }catch(const std::exception& e){
                    CrashReporter::ReportCrash(e.what());
                }catch(...){
                    CrashReporter::ReportCrash("Unknown exception in tick");
                }
            }
        }
    }

    auto tickEnd = clock::now();
    storeTickStats(tickEnd);
} // Update the Engine (Delta Time Interval using nano time)

void GameEngine::render(){
    using clock = std::chrono::steady_clock;
    auto renderStart = clock::now();
    float execWaitMs = 0.0f;
    float sceneRenderMs = 0.0f;
    float sceneBlitMs = 0.0f;
    float imguiSubmitMs = 0.0f;

    auto storeRenderStats = [&](const clock::time_point& renderEnd){
        const float totalMs = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();
        const float workMs = (totalMs > execWaitMs) ? (totalMs - execWaitMs) : 0.0f;
        runtimeDebugStats.renderMs.store(totalMs, std::memory_order_relaxed);
        runtimeDebugStats.renderWaitMs.store(execWaitMs, std::memory_order_relaxed);
        runtimeDebugStats.renderWorkMs.store(workMs, std::memory_order_relaxed);
        runtimeDebugStats.renderSceneMs.store(sceneRenderMs, std::memory_order_relaxed);
        runtimeDebugStats.renderBlitMs.store(sceneBlitMs, std::memory_order_relaxed);
        runtimeDebugStats.renderImGuiMs.store(imguiSubmitMs, std::memory_order_relaxed);
    };

    Texture::FlushPendingDeletes();

    {
        auto execWaitStart = clock::now();
        std::unique_lock<std::mutex> execLock(sceneExecutionMutex);
        auto execWaitEnd = clock::now();
        execWaitMs += std::chrono::duration<float, std::milli>(execWaitEnd - execWaitStart).count();
        int pendingId = pendingStateId.exchange(-1);

        std::lock_guard<std::mutex> lock(sceneMutex);
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
    }

    PScene scene;
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        scene = activeScene;
    }

    {
        // Keep scene updates and ImGui access serialized: EditorScene::update() also touches ImGui state.
        auto execWaitStart = clock::now();
        std::unique_lock<std::mutex> execLock(sceneExecutionMutex);
        auto execWaitEnd = clock::now();
        execWaitMs += std::chrono::duration<float, std::milli>(execWaitEnd - execWaitStart).count();

        ImGuiLayer::BeginFrame();

        if(scene && !CrashReporter::IsCrashed()){
            try{
                auto sceneRenderStart = clock::now();
                scene->render();
                auto sceneRenderEnd = clock::now();
                sceneRenderMs += std::chrono::duration<float, std::milli>(sceneRenderEnd - sceneRenderStart).count();

                auto sceneBlitStart = clock::now();
                scene->drawToWindow();
                auto sceneBlitEnd = clock::now();
                sceneBlitMs += std::chrono::duration<float, std::milli>(sceneBlitEnd - sceneBlitStart).count();
            }catch(const std::exception& e){
                CrashReporter::ReportCrash(e.what());
            }catch(...){
                CrashReporter::ReportCrash("Unknown exception in render");
            }
        }

        auto imguiSubmitStart = clock::now();
        CrashReporter::RenderImGui();
        ImGuiLayer::EndFrame();
        auto imguiSubmitEnd = clock::now();
        imguiSubmitMs += std::chrono::duration<float, std::milli>(imguiSubmitEnd - imguiSubmitStart).count();
    }

    auto renderEnd = clock::now();
    storeRenderStats(renderEnd);
} // Update as fast as possible.

void GameEngine::processEvents(SDL_Event& event){
    if(event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED){
        LogBot.Log(LOG_WARN, "GameEngine::processEvents() close event type=%d", (int)event.type);
        PScene scene;
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            scene = activeScene;
        }
        if(scene){
            std::lock_guard<std::mutex> execLock(sceneExecutionMutex);
            scene->requestClose();
            if(auto editor = std::dynamic_pointer_cast<EditorScene>(scene)){
                editor->handleQuitRequest();
                return;
            }
        }
        return;
    }

    if(event.type == SDL_EVENT_WINDOW_RESIZED){
        PScene scene;
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            scene = activeScene;
        }
        if(scene && windowPtr){
            std::lock_guard<std::mutex> execLock(sceneExecutionMutex);
            scene->resize(windowPtr->getWindowWidth(), windowPtr->getWindowHeight());
        }
        return;
    }
} // Handle all events

void GameEngine::dispose(){
    std::lock_guard<std::mutex> execLock(sceneExecutionMutex);
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        if(activeScene){
            activeScene->dispose();
            activeScene.reset();
        }
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
    std::lock_guard<std::mutex> execLock(sceneExecutionMutex);
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

    using frameClock = std::chrono::steady_clock;
    auto editorLastTick = frameClock::now();
    bool editorTickClockPrimed = false;

    while(running){
        if(windowPtr){
            windowPtr->process();
        }

        bool tickOnRenderThread = false;
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            if(activeScene){
                tickOnRenderThread = activeScene->shouldTickOnRenderThread();
            }
        }

        if(tickOnRenderThread){
            auto now = frameClock::now();
            if(!editorTickClockPrimed){
                editorLastTick = now;
                editorTickClockPrimed = true;
            }
            float deltaTime = std::chrono::duration<float>(now - editorLastTick).count();
            editorLastTick = now;
            tick(deltaTime);
        }else{
            editorTickClockPrimed = false;
        }

        render();

        if(windowPtr){
            auto swapStart = std::chrono::steady_clock::now();
            windowPtr->swap();
            auto swapEnd = std::chrono::steady_clock::now();
            runtimeDebugStats.swapMs.store(
                std::chrono::duration<float, std::milli>(swapEnd - swapStart).count(),
                std::memory_order_relaxed
            );
            if(windowPtr->isCloseRequested()){
                LogBot.Log(LOG_WARN, "GameEngine::run() detected window close request.");
                running = false;
            }
        }else{
            runtimeDebugStats.swapMs.store(0.0f, std::memory_order_relaxed);
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

        bool tickOnLogicThread = true;
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            if(activeScene){
                tickOnLogicThread = !activeScene->shouldTickOnRenderThread();
            }
        }
        if(tickOnLogicThread){
            tick(deltaTime);
        }
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
