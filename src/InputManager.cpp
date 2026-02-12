#include "InputManager.h"

InputManager::InputManager(std::shared_ptr<RenderWindow> window, bool attachHandlers){
    if(!window) return;

    this->windowPtr = window;

    if(attachHandlers){
        this->windowPtr->addWindowEventHandler([&](SDL_Event& event){
            if (event.type == SDL_EVENT_KEY_UP){
                this->onKeyUp(event.key.scancode);
            } 

            if (event.type == SDL_EVENT_KEY_DOWN){
                this->onKeyDown(event.key.scancode);
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP){
                this->onMouseReleased(event.button.button);
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN){
                this->onMousePressed(event.button.button);
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION){
                if(this->mouseCaptureMode == MouseLockMode::LOCKED){
                    this->onMouseMoved(event.motion.xrel,event.motion.yrel);
                }else{
                    this->onMouseMoved(event.motion.x,event.motion.y);
                }
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL){
                this->onMouseScroll((event.wheel.y));
            }
        });
    }

    this->inputInfo.keyMap.resize(InputInformation::KEYMAP_SIZE);
}

InputManager::~InputManager(){
    this->handlers.clear();
}

bool InputManager::isKeyDown(int keyCode){
    if(keyCode >= 0 && keyCode < InputInformation::KEYMAP_SIZE){
        return this->inputInfo.keyMap[keyCode];
    }

    return false;
}

void InputManager::onKeyUp(int keyCode){

    if(keyCode >= 0 && keyCode < InputInformation::KEYMAP_SIZE) this->inputInfo.keyMap[keyCode] = false;

    // Dispach Event.
    for(auto it = handlers.begin(); it != handlers.end();){
        if(auto eventHandler = it->lock()){
            if(eventHandler->onKeyUp(keyCode, *this)) break;
            ++it;
        }else{
            it = handlers.erase(it);
        }
    }
}

void InputManager::onKeyDown(int keyCode){

    if(keyCode >= 0 && keyCode < InputInformation::KEYMAP_SIZE) this->inputInfo.keyMap[keyCode] = true;

    // Dispach Event.
    for(auto it = handlers.begin(); it != handlers.end();){
        if(auto eventHandler = it->lock()){
            if(eventHandler->onKeyDown(keyCode, *this)) break;
            ++it;
        }else{
            it = handlers.erase(it);
        }
    }
}

void InputManager::onMousePressed(int button){

    if(button == SDL_BUTTON_LEFT) this->inputInfo.mouse_left_button = true;
    if(button == SDL_BUTTON_MIDDLE) this->inputInfo.mouse_middle_button = true;
    if(button == SDL_BUTTON_RIGHT) this->inputInfo.mouse_right_button = true;

    // Dispach Event.
    for(auto it = handlers.begin(); it != handlers.end();){
        if(auto eventHandler = it->lock()){
            if(eventHandler->onMousePressed(button, *this)) break;
            ++it;
        }else{
            it = handlers.erase(it);
        }
    }
}

void InputManager::onMouseReleased(int button){

    if(button == SDL_BUTTON_LEFT) this->inputInfo.mouse_left_button = false;
    if(button == SDL_BUTTON_MIDDLE) this->inputInfo.mouse_middle_button = false;
    if(button == SDL_BUTTON_RIGHT) this->inputInfo.mouse_right_button = false;

    // Dispach Event.
    for(auto it = handlers.begin(); it != handlers.end();){
        if(auto eventHandler = it->lock()){
             if(eventHandler->onMouseReleased(button, *this)) break;
            ++it;
        }else{
            it = handlers.erase(it);
        }
    }
}

void InputManager::onMouseMoved(int x, int y){

    this->inputInfo.mouseX = x;
    this->inputInfo.mouseY = y;
    if(this->mouseCaptureMode == MouseLockMode::LOCKED){
        this->inputInfo.mouseAxis = Math3D::Vec2((float)x, (float)y);
    }else{
        if(this->hasLastMouse){
            this->inputInfo.mouseAxis = Math3D::Vec2((float)(x - this->lastMouseX), (float)(y - this->lastMouseY));
        }else{
            this->inputInfo.mouseAxis = Math3D::Vec2(0,0);
            this->hasLastMouse = true;
        }
        this->lastMouseX = x;
        this->lastMouseY = y;
    }

 
    // Dispach Event.
    for(auto it = handlers.begin(); it != handlers.end();){
        if(auto eventHandler = it->lock()){
            if(eventHandler->onMouseMoved(x,y,*this)) break;
            ++it;
        }else{
            it = handlers.erase(it);
        }
    }
}

void InputManager::onMouseScroll(float dz){

    // SDL provides a delta per event; accumulate so multiple events per frame stack.
    this->inputInfo.wheel += dz;
    this->inputInfo.wheelDelta += dz;

    // Dispach Event.
    for(auto it = handlers.begin(); it != handlers.end();){
        if(auto eventHandler = it->lock()){
            if(eventHandler->onMouseScroll(dz,*this)) break;
            ++it;
        }else{
            it = handlers.erase(it);
        }
    }
}

void InputManager::addEventHandler(std::shared_ptr<IEventHandler> handler){
    this->handlers.push_back(handler);
}

void InputManager::setMouseCaptureMode(MouseLockMode mode){
    if(!this->windowPtr) return;
    if(this->getMouseCaptureMode() == mode) return; // no changes needed.


    this->mouseCaptureMode = mode;
    SDL_Window* nativeWindowPtr = this->windowPtr->getWindowPtr();
    this->hasLastMouse = false;
    this->inputInfo.mouseAxis = Math3D::Vec2(0,0);

    switch(mode){
        case MouseLockMode::CAPTURED:
            SDL_SetWindowMouseGrab(nativeWindowPtr, true);
            SDL_SetWindowRelativeMouseMode(nativeWindowPtr,false);
            SDL_ShowCursor();
            break;
        case MouseLockMode::LOCKED:
            SDL_SetWindowMouseGrab(nativeWindowPtr, true);
            SDL_SetWindowRelativeMouseMode(nativeWindowPtr,true);
            SDL_ShowCursor();
            break;
        case MouseLockMode::FREE:
        default:
            SDL_SetWindowMouseGrab(nativeWindowPtr, false);
            SDL_SetWindowRelativeMouseMode(nativeWindowPtr,false);
            SDL_ShowCursor();
            break;
    }

}

Math3D::Vec2 InputManager::consumeMouseAxisDelta(){
    Math3D::Vec2 delta = this->inputInfo.mouseAxis;
    this->inputInfo.mouseAxis = Math3D::Vec2(0,0);
    return delta;
}

float InputManager::consumeScrollDelta(){
    float delta = this->inputInfo.wheelDelta;
    this->inputInfo.wheelDelta = 0.0f;
    return delta;
}
