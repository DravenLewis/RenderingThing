#include "View.h"
#include <glm/gtc/matrix_transform.hpp>

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

Math3D::Vec3 View::screenToWorld(PCamera cam, float x, float y, float depth,
                                 float viewportX, float viewportY, float viewportW, float viewportH) const{
    if(!window || !cam) return Math3D::Vec3();

    float w = (viewportW > 0.0f) ? viewportW : (float)window->getWindowWidth();
    float h = (viewportH > 0.0f) ? viewportH : (float)window->getWindowHeight();
    float vx = viewportX;
    float vy = (float)window->getWindowHeight() - (viewportY + h);

    glm::vec4 viewport(vx, vy, w, h);
    float glX = x;
    float glY = (float)window->getWindowHeight() - y;

    glm::mat4 view = (glm::mat4)cam->getViewMatrix();
    glm::mat4 proj = (glm::mat4)cam->getProjectionMatrix();
    glm::vec3 world = glm::unProject(glm::vec3(glX, glY, depth), view, proj, viewport);
    return Math3D::Vec3(world);
}

Math3D::Vec3 View::worldToScreen(PCamera cam, const Math3D::Vec3& world,
                                 float viewportX, float viewportY, float viewportW, float viewportH) const{
    if(!window || !cam) return Math3D::Vec3();

    float w = (viewportW > 0.0f) ? viewportW : (float)window->getWindowWidth();
    float h = (viewportH > 0.0f) ? viewportH : (float)window->getWindowHeight();
    float vx = viewportX;
    float vy = (float)window->getWindowHeight() - (viewportY + h);

    glm::vec4 viewport(vx, vy, w, h);
    glm::mat4 view = (glm::mat4)cam->getViewMatrix();
    glm::mat4 proj = (glm::mat4)cam->getProjectionMatrix();
    glm::vec3 projected = glm::project((glm::vec3)world, view, proj, viewport);
    float screenX = projected.x;
    float screenY = (float)window->getWindowHeight() - projected.y;
    return Math3D::Vec3(screenX, screenY, projected.z);
}
