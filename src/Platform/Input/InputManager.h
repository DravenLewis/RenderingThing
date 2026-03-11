/**
 * @file src/Platform/Input/InputManager.h
 * @brief Declarations for InputManager.
 */

#ifndef INPUTMANAGER_H
#define INPUTMANAGER_H

#include <memory>
#include <vector>
#include "Platform/Window/RenderWindow.h"

#include "Foundation/Math/Math3D.h"

class InputManager;

/// @brief Enumerates values for MouseLockMode.
enum class MouseLockMode{
    FREE,
    CAPTURED,
    LOCKED
};

/// @brief Holds data for IEventHandler.
struct IEventHandler{
    /**
     * @brief Checks whether on key up.
     * @param keyCode Value for key code.
     * @param manager Value for manager.
     * @return True when the operation succeeds; otherwise false.
     */
    virtual bool onKeyUp(int keyCode, InputManager& manager) = 0;
    /**
     * @brief Checks whether on key down.
     * @param keyCode Value for key code.
     * @param manager Value for manager.
     * @return True when the operation succeeds; otherwise false.
     */
    virtual bool onKeyDown(int keyCode, InputManager& manager) = 0;
    /**
     * @brief Checks whether on mouse pressed.
     * @param button Value for button.
     * @param manager Value for manager.
     * @return True when the operation succeeds; otherwise false.
     */
    virtual bool onMousePressed(int button, InputManager& manager) = 0;
    /**
     * @brief Checks whether on mouse released.
     * @param button Value for button.
     * @param manager Value for manager.
     * @return True when the operation succeeds; otherwise false.
     */
    virtual bool onMouseReleased(int button, InputManager& manager) = 0;
    /**
     * @brief Checks whether on mouse moved.
     * @param x Spatial value used by this operation.
     * @param y Spatial value used by this operation.
     * @param manager Value for manager.
     * @return True when the operation succeeds; otherwise false.
     */
    virtual bool onMouseMoved(int x, int y, InputManager& manager) = 0;
    /**
     * @brief Checks whether on mouse scroll.
     * @param dz Value for dz.
     * @param manager Value for manager.
     * @return True when the operation succeeds; otherwise false.
     */
    virtual bool onMouseScroll(float dz, InputManager& manager) = 0;
};

/// @brief Holds data for InputInformation.
struct InputInformation{
    const static int KEYMAP_SIZE = 512;

    std::vector<bool> keyMap;
    int mouseX = 0;
    int mouseY = 0;
    bool mouse_left_button = false;
    bool mouse_middle_button = false;
    bool mouse_right_button = false;
    float wheel = 0;
    float wheelDelta = 0;
    Math3D::Vec2 mouseAxis = Math3D::Vec2(0,0);

    
    float _wheelLast = wheel;
};

/// @brief Represents the InputManager type.
class InputManager{
    private:
        std::shared_ptr<RenderWindow> windowPtr;
        std::vector<std::weak_ptr<IEventHandler>> handlers;
        InputInformation inputInfo;
        MouseLockMode mouseCaptureMode = MouseLockMode::FREE;
        bool hasLastMouse = false;
        int lastMouseX = 0;
        int lastMouseY = 0;
    public:
        /**
         * @brief Constructs a new InputManager instance.
         */
        InputManager() : windowPtr(nullptr) {};
        InputManager(std::shared_ptr<RenderWindow> window, bool attachHandlers = true);
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
        float getscrollDelta() const {return this->inputInfo.wheelDelta;}
        Math3D::Vec2 getMouseAxisDelta() const {return this->inputInfo.mouseAxis;}
        Math3D::Vec2 consumeMouseAxisDelta();
        float consumeScrollDelta();
        MouseLockMode getMouseCaptureMode() const {return this->mouseCaptureMode;}

};

#endif // INPUTMANAGER_H
