#include "View.h"

View::View(int width, int height){
    this->width = width;
    this->height = height;

    screens.reserve(2);
    screens.push_back(std::make_shared<Screen>(width, height)); // 3D
    screens.push_back(std::make_shared<Screen>(width, height)); // UI
}

void View::resize(int w, int h){
    this->width = w;
    this->height = h;

    for(auto& screen : screens){
        if(screen){
            screen->resize(w, h);
        }
    }
}

PScreen View::getScreen(size_t index) const{
    if(index >= screens.size()) return nullptr;
    return screens[index];
}

PScreen View::getMainScreen() const{
    return getScreen(0);
}

PScreen View::getUIScreen() const{
    return getScreen(1);
}

void View::addScreen(PScreen screen){
    if(!screen) return;
    screen->resize(width, height);
    screens.push_back(screen);
}

void View::drawToWindow(RenderWindow* window, bool clearWindow){
    if(!window) return;

    for(size_t i = 0; i < screens.size(); ++i){
        auto& screen = screens[i];
        if(!screen) continue;
        bool clear = (i == 0) ? clearWindow : false;
        screen->drawToWindow(window, clear);
    }
}

void View::drawToView(RenderWindow* window, float x, float y, float w, float h, bool clearWindow){
    if(!window) return;

    for(size_t i = 0; i < screens.size(); ++i){
        auto& screen = screens[i];
        if(!screen) continue;
        bool clear = (i == 0) ? clearWindow : false;
        screen->drawToView(window, clear, x, y, w, h);
    }
}
