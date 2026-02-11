#include "GameEngine.h"
#include "DemoScene.h"

int main(){

    DisplayMode mode = DisplayMode::New(1280,720);
    mode.resizable = true;
    GameEngine engine(mode, "Modern OpenGL 4 - Render Thingy");

    int id = engine.addState(std::make_shared<DemoScene>(engine.window()));
    engine.enterState(id);
    engine.start();

    return 0;
}