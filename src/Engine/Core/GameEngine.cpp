/**
 * @file src/Engine/Core/GameEngine.cpp
 * @brief Implementation for GameEngine.
 */


#include "Engine/Core/GameEngine.h"

#include <chrono>
#include <algorithm>

#include "App/Demo/DefaultState.h"
#include "Assets/Bundles/AssetBundleRegistry.h"
#include "Foundation/Logging/Logbot.h"
#include "Editor/Core/ImGuiLayer.h"
#include "Editor/Core/EditorScene.h"
#include "Platform/Crash/CrashReporter.h"
#include "Rendering/Textures/Texture.h"

GameEngine* GameEngine::Engine = nullptr;

bool GameEngine::setVSyncMode(VSyncMode mode){
    requestedVSyncMode.store(static_cast<int>(mode), std::memory_order_relaxed);
    return true;
}

VSyncMode GameEngine::getVSyncMode() const{
    return static_cast<VSyncMode>(requestedVSyncMode.load(std::memory_order_relaxed));
}

bool GameEngine::setFrameCap(int fps){
    if(fps == kFrameCapUncapped || (fps >= kFrameCapMin && fps <= kFrameCapMax)){
        frameCapFps.store(fps, std::memory_order_relaxed);
        return true;
    }

    LogBot.Log(
        LOG_WARN,
        "GameEngine::setFrameCap(%d) rejected. Valid values are -1 (uncapped) or [%d, %d].",
        fps,
        kFrameCapMin,
        kFrameCapMax
    );
    return false;
}

int GameEngine::getFrameCap() const{
    return frameCapFps.load(std::memory_order_relaxed);
}

bool GameEngine::setFixedUpdateRate(int hz){
    if(hz < kFixedUpdateRateMin || hz > kFixedUpdateRateMax){
        LogBot.Log(
            LOG_WARN,
            "GameEngine::setFixedUpdateRate(%d) rejected. Valid range is [%d, %d].",
            hz,
            kFixedUpdateRateMin,
            kFixedUpdateRateMax
        );
        return false;
    }

    fixedUpdateRateHz.store(hz, std::memory_order_relaxed);
    return true;
}

int GameEngine::getFixedUpdateRate() const{
    return fixedUpdateRateHz.load(std::memory_order_relaxed);
}

float GameEngine::getFixedUpdateStepSeconds() const{
    int hz = fixedUpdateRateHz.load(std::memory_order_relaxed);
    if(hz <= 0){
        return 0.0f;
    }
    return 1.0f / static_cast<float>(hz);
}

void GameEngine::applyPendingVSyncMode(){
    if(!windowPtr){
        return;
    }

    int requestedRaw = requestedVSyncMode.load(std::memory_order_relaxed);
    int appliedRaw = appliedVSyncMode.load(std::memory_order_relaxed);
    if(requestedRaw == appliedRaw){
        return;
    }

    VSyncMode requestedMode = static_cast<VSyncMode>(requestedRaw);
    windowPtr->setVSyncMode(requestedMode);

    int actualRaw = static_cast<int>(windowPtr->getVSyncMode());
    requestedVSyncMode.store(actualRaw, std::memory_order_relaxed);
    appliedVSyncMode.store(actualRaw, std::memory_order_relaxed);
}

void GameEngine::stepFixedUpdates(float frameDeltaSeconds, float& accumulatorSeconds){
    if(frameDeltaSeconds <= 0.0f){
        return;
    }

    frameDeltaSeconds = std::min(frameDeltaSeconds, kMaxAccumulatedDeltaSeconds);
    const int hz = fixedUpdateRateHz.load(std::memory_order_relaxed);
    if(hz <= 0){
        return;
    }

    const float fixedStepSeconds = 1.0f / static_cast<float>(hz);
    const float maxAccumulation = fixedStepSeconds * static_cast<float>(kMaxFixedTicksPerCycle);
    accumulatorSeconds = std::min(accumulatorSeconds + frameDeltaSeconds, maxAccumulation);

    int steps = 0;
    while(running && accumulatorSeconds >= fixedStepSeconds && steps < kMaxFixedTicksPerCycle){
        tick(fixedStepSeconds);
        accumulatorSeconds -= fixedStepSeconds;
        ++steps;
    }

    if(steps == kMaxFixedTicksPerCycle && accumulatorSeconds >= fixedStepSeconds){
        // Drop excessive backlog under overload so rendering/input stay responsive.
        accumulatorSeconds = 0.0f;
    }
}

void GameEngine::init(){
    windowPtr = std::make_shared<RenderWindow>(windowInitialTitle, windowDisplayMode);
    inputManager = std::make_shared<InputManager>(windowPtr, true);

    if(windowPtr){
        int activeMode = static_cast<int>(windowPtr->getVSyncMode());
        requestedVSyncMode.store(activeMode, std::memory_order_relaxed);
        appliedVSyncMode.store(activeMode, std::memory_order_relaxed);
    }

    windowPtr->addWindowEventHandler([this](SDL_Event& event){
        processEvents(event);
    });

    std::string bundleScanError;
    const size_t mountedBundleCount = AssetBundleRegistry::Instance.scanKnownLocations(&bundleScanError);
    if(mountedBundleCount > 0){
        LogBot.Log(LOG_INFO, "Mounted %llu asset bundle(s).", static_cast<unsigned long long>(mountedBundleCount));
    }else if(!bundleScanError.empty()){
        LogBot.Log(LOG_WARN, "Asset bundle scan completed without mounted bundles: %s", bundleScanError.c_str());
    }

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
    auto renderTickLast = frameClock::now();
    bool renderTickClockPrimed = false;
    float renderTickAccumulator = 0.0f;

    while(running){
        auto frameStart = frameClock::now();

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
            if(!renderTickClockPrimed){
                renderTickLast = now;
                renderTickClockPrimed = true;
                renderTickAccumulator = 0.0f;
            }
            float deltaTime = std::chrono::duration<float>(now - renderTickLast).count();
            renderTickLast = now;
            stepFixedUpdates(deltaTime, renderTickAccumulator);
        }else{
            renderTickClockPrimed = false;
            renderTickAccumulator = 0.0f;
        }

        render();

        if(windowPtr){
            applyPendingVSyncMode();
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

        int frameCap = frameCapFps.load(std::memory_order_relaxed);
        if(frameCap != kFrameCapUncapped){
            auto targetFrameDuration = std::chrono::duration<float>(1.0f / static_cast<float>(frameCap));
            auto frameDeadline = frameStart + std::chrono::duration_cast<frameClock::duration>(targetFrameDuration);
            auto now = frameClock::now();
            if(now < frameDeadline){
                auto remaining = frameDeadline - now;
                if(remaining > std::chrono::milliseconds(2)){
                    std::this_thread::sleep_for(remaining - std::chrono::milliseconds(1));
                }
                while(running && frameClock::now() < frameDeadline){
                    std::this_thread::yield();
                }
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
    auto logicTickLast = clock::now();
    bool logicTickClockPrimed = false;
    float logicTickAccumulator = 0.0f;

    while(running){
        auto now = clock::now();
        if(!logicTickClockPrimed){
            logicTickLast = now;
            logicTickClockPrimed = true;
            logicTickAccumulator = 0.0f;
        }
        float deltaTime = std::chrono::duration<float>(now - logicTickLast).count();
        logicTickLast = now;

        bool tickOnLogicThread = true;
        {
            std::lock_guard<std::mutex> lock(sceneMutex);
            if(activeScene){
                tickOnLogicThread = !activeScene->shouldTickOnRenderThread();
            }
        }
        if(tickOnLogicThread){
            stepFixedUpdates(deltaTime, logicTickAccumulator);
        }else{
            logicTickClockPrimed = false;
            logicTickAccumulator = 0.0f;
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
