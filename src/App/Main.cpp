#include "Engine/Core/GameEngine.h"
#include "App/Demo/DemoScene.h"
#include "Editor/Core/EditorScene.h"
#include "App/Bootstrap/ManifestSceneInstaller.h"

int main(int argc, char** argv){
    (void)argc;
    (void)argv;

    DisplayMode mode = DisplayMode::New(1280, 720);
    mode.resizable = true;
    GameEngine engine(mode, "Modern OpenGL 4 - Render Engine - Editor");
    engine.setRenderStrategy(EngineRenderStrategy::Deferred);
    engine.setVSyncMode(VSyncMode::Off);

    auto demoFactory = [](RenderWindow* window) -> PScene {
        return std::make_shared<DemoScene>(window);
    };
    auto demoScene = demoFactory(engine.window());
    int id = engine.addState(std::make_shared<EditorScene>(engine.window(), demoScene, demoFactory));
    engine.enterState(id);

    
    /*
    // Optional: manifest-driven bootstrap takeover.
    AppBootstrap::ManifestSceneInstaller installer(engine);
    AppBootstrap::ManifestSceneInstaller::InstallOptions installOptions;
    installOptions.fallbackFactory = demoFactory;
    installOptions.preferManifestSceneInEditor = true;
    if(installer.tryInstallFromCommandLine(argc, argv, installOptions, &id)){
        // Manifest scene state was installed and entered.
    }
    */

    engine.start();
    return 0;
}
