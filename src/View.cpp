#include "View.h"

View::View(){
    this->window = nullptr;
    this->width = 0;
    this->height = 0;
}

View::View(RenderWindow* window){
    this->window = nullptr;
    this->width = 0;
    this->height = 0;
    attachWindow(window);
}

void View::attachWindow(RenderWindow* window){
    if(!window) return;

    this->window = window;
    this->width = window->getWindowWidth();
    this->height = window->getWindowHeight();

    if(screens.empty()){
        screens.reserve(2);
        screens.push_back(std::make_shared<Screen>(this->width, this->height)); // 3D
        screens.push_back(std::make_shared<Screen>(this->width, this->height)); // UI
    }else{
        resize(this->width, this->height);
    }
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

void View::drawToWindow(bool clearWindow, float x, float y, float w, float h){
    if(!window) return;

    bool useWindow = (x == -1.0f && y == -1.0f && w == -1.0f && h == -1.0f);
    auto self = shared_from_this();

    for(size_t i = 0; i < screens.size(); ++i){
        auto& screen = screens[i];
        if(!screen) continue;
        bool clear = (i == 0) ? clearWindow : false;
        if(useWindow){
            screen->drawToWindow(window, clear);
        }else{
            screen->drawToView(self, clear, x, y, w, h);
        }
    }
}
