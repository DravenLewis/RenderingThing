#include "Engine/Core/GameEngine.h"
#include "App/Demo/DemoScene.h"
#include "Editor/Core/EditorScene.h"

int main(){

    DisplayMode mode = DisplayMode::New(1280,720);
    mode.resizable = true;
    GameEngine engine(mode, "Modern OpenGL 4 - Render Engine - Editor");
    engine.setRenderStrategy(EngineRenderStrategy::Deferred);

    auto demoFactory = [](RenderWindow* window) -> PScene {
        return std::make_shared<DemoScene>(window);
    };
    auto demoScene = demoFactory(engine.window());
    int id = engine.addState(std::make_shared<EditorScene>(engine.window(), demoScene, demoFactory));
    engine.enterState(id);
    engine.start();

    return 0;
}
