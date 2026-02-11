#include "DefaultState.h"

#include "Camera.h"
#include "Color.h"
#include "Math.h"

DefaultState::DefaultState(RenderWindow* window) : Scene2D(window) {}

void DefaultState::init(){
    auto window = getWindow();
    if(!window) return;

    uiScreen = getUIScreen();
    if(!uiScreen) return;

    auto uiCam = Camera::CreateOrthogonal(
        Math3D::Rect(0,0,window->getWindowWidth(), window->getWindowHeight()),
        -100,
        100
    );
    uiScreen->setCamera(uiCam, false);
    uiScreen->setClearColor(Color::BLACK);
    graphics2d = std::make_shared<Graphics2D>(uiScreen);
}

void DefaultState::render(){
    auto window = getWindow();
    if(!window || !uiScreen || !graphics2d) return;

    graphics2d->begin();
    Graphics2D::SetBackgroundColor(*graphics2d, Color::BLACK);
    Graphics2D::FillRect(*graphics2d, 0, 0, uiScreen->getWidth(), uiScreen->getHeight());

    Graphics2D::SetForegroundColor(*graphics2d, Color::WHITE);
    Graphics2D::DrawString(*graphics2d, "No State Loaded", 30, 30);
    graphics2d->end();

    drawToWindow();
}

void DefaultState::dispose(){
    graphics2d.reset();
}
