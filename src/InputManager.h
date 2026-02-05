#ifndef INPUTMANAGER_H
#define INPUTMANAGER_H

#include <memory>
#include <vector>
#include "RenderWindow.h"

#include "Math.h"

class InputManager;

enum class MouseLockMode{
    FREE,
    CAPTURED,
    LOCKED
};

struct IEventHandler{
    virtual bool onKeyUp(int keyCode, InputManager& manager) = 0;
    virtual bool onKeyDown(int keyCode, InputManager& manager) = 0;
    virtual bool onMousePressed(int button, InputManager& manager) = 0;
    virtual bool onMouseReleased(int button, InputManager& manager) = 0;
    virtual bool onMouseMoved(int x, int y, InputManager& manager) = 0;
    virtual bool onMouseScroll(float dz, InputManager& manager) = 0;
};

struct InputInformation{
    const static int KEYMAP_SIZE = 512;

    std::vector<bool> keyMap;
    int mouseX = 0;
    int mouseY = 0;
    bool mouse_left_button = false;
    bool mouse_middle_button = false;
    bool mouse_right_button = false;
    float wheel = 0;
    Math3D::Vec2 mouseAxis = Math3D::Vec2(0,0);
};

class InputManager{
    private:
        std::shared_ptr<RenderWindow> windowPtr;
        std::vector<std::weak_ptr<IEventHandler>> handlers;
        InputInformation inputInfo;
        MouseLockMode mouseCaptureMode = MouseLockMode::FREE;
    public:
        InputManager() : windowPtr(nullptr) {};
        InputManager(std::shared_ptr<RenderWindow> window);
        ~InputManager();


        void onKeyUp(int keyCode);
        void onKeyDown(int keyCode);
        void onMousePressed(int button);
        void onMouseReleased(int button);
        void onMouseMoved(int x, int y);
        void onMouseScroll(float dz);
        void addEventHandler(std::shared_ptr<IEventHandler> handler);
        bool isKeyDown(int keyCode);

        void setMouseCaptureMode(MouseLockMode mode);



        InputInformation getInputInfo() const {
            return inputInfo;
        }

        Math3D::Vec2 getMousePosition() const { return Math3D::Vec2(this->inputInfo.mouseX, this->inputInfo.mouseY);}
        bool isLMBDown() const { return this->inputInfo.mouse_left_button;}
        bool isMMBDown() const { return this->inputInfo.mouse_middle_button;}
        bool isRMBDown() const { return this->inputInfo.mouse_right_button;}
        float getScrollWheel() const {return this->inputInfo.wheel;}
        Math3D::Vec2 getMouseAxisDelta() const {return this->inputInfo.mouseAxis;}
        MouseLockMode getMouseCaptureMode() const {return this->mouseCaptureMode;}

};

#endif // INPUTMANAGER_H