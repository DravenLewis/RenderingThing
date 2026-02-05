#ifndef FIRST_PERSON_CONTROLLER_H
#define FIRST_PERSON_CONTROLLER_H

#include <memory>
#include <SDL3/SDL.h>
#include "InputManager.h"
#include "Camera.h"
#include "Math.h"

/**
 * A First Person Controller that implements IEventHandler.
 * Uses std::enable_shared_from_this for safe registration with InputManager.
 */
class FirstPersonController : public IEventHandler, public std::enable_shared_from_this<FirstPersonController> {
private:
    std::shared_ptr<Camera> m_Camera;
    
    // Internal orientation state
    float m_Yaw   = -90.0f; 
    float m_Pitch = 0.0f;

    // Movement Settings
    float m_MoveSpeed    = 10.0f;
    float m_LookSensitivity = 0.1f;
    float m_fastScale = 2.0f;


public:
    FirstPersonController(std::shared_ptr<Camera> cam) 
        : m_Camera(cam) {}

    /**
     * Registers this controller with the InputManager.
     * Must be called after the shared_ptr for the controller is created.
     */
    void init(std::shared_ptr<InputManager> inputManager) {
        if (inputManager) {
            inputManager->addEventHandler(shared_from_this());
        }
    }

    /**
     * Call this every frame in your main loop.
     * @param dt Delta time (time elapsed since last frame)
     * @param input Reference to your InputManager to check key states
     */
    void update(float dt, std::shared_ptr<InputManager> input) {
        if (!m_Camera) return;

        auto transform = m_Camera->transform();
        float velocity = ((input->isKeyDown(SDL_SCANCODE_LSHIFT)) ? ((m_MoveSpeed * m_fastScale) * dt) : (m_MoveSpeed * dt));

        // --- Keyboard Movement ---
        // Forward/Backward
        if (input->isKeyDown(SDL_SCANCODE_W)) transform.position -= transform.forward() * velocity;
        if (input->isKeyDown(SDL_SCANCODE_S)) transform.position += transform.forward() * velocity;
        
        // Strafe Left/Right
        if (input->isKeyDown(SDL_SCANCODE_A)) transform.position -= transform.right() * velocity;
        if (input->isKeyDown(SDL_SCANCODE_D)) transform.position += transform.right() * velocity;
        
        // Vertical (Fly)
        if (input->isKeyDown(SDL_SCANCODE_SPACE))  transform.position += transform.up() * velocity;
        if (input->isKeyDown(SDL_SCANCODE_C)) transform.position -= transform.up() * velocity;

        // --- Arrow Key Rotation ---
        float rotationSpeed = 100.0f * dt;
        if (input->isKeyDown(SDL_SCANCODE_LEFT))  m_Yaw += rotationSpeed;
        if (input->isKeyDown(SDL_SCANCODE_RIGHT)) m_Yaw -= rotationSpeed;
        if (input->isKeyDown(SDL_SCANCODE_UP))    m_Pitch += rotationSpeed;
        if (input->isKeyDown(SDL_SCANCODE_DOWN))  m_Pitch -= rotationSpeed;

        // Apply final state to the camera's internal Transform
        updateCameraRotation(transform);
        
        // Note: You may need to add a method to Camera to set its private transform
        // or make it public. Assuming a setter exists:
        // m_Camera->setTransform(transform); 
        m_Camera->setTransform(transform);
    }

    // --- IEventHandler Implementation ---

    virtual bool onMouseMoved(int x, int y,InputManager& manager) override {

        if(manager.getMouseCaptureMode() != MouseLockMode::LOCKED) return false;

        /*
        // Simple Delta Calculation (Last position should be tracked by InputManager or static here)
        static int lastX = x, lastY = y;
        float dx = (float)(x - lastX) * m_LookSensitivity;
        float dy = (float)(lastY - y) * m_LookSensitivity; // Y is inverted in screen space
        lastX = x; lastY = y;

        m_Yaw   += dx;
        m_Pitch += dy;

        // Clamp Pitch to prevent "gymbal lock" flipping
        m_Pitch = Math3D::Clamp(m_Pitch, -89.0f, 89.0f);
        return true;
        */

        if (manager.getMouseCaptureMode() != MouseLockMode::LOCKED) return false;

        // When locked, x/y are already deltas
        float dx = (float)x * m_LookSensitivity;
        float dy = (float)-y * m_LookSensitivity;

        m_Yaw   -= dx;
        m_Pitch += dy;
        m_Pitch = Math3D::Clamp(m_Pitch, -89.0f, 89.0f);
        return true;

    }

    // Empty overrides for required pure virtuals
    virtual bool onKeyUp(int k,InputManager& m) override { 
        if(k == SDL_SCANCODE_GRAVE && (m.getMouseCaptureMode() == MouseLockMode::LOCKED || m.getMouseCaptureMode() == MouseLockMode::CAPTURED)){
            m.setMouseCaptureMode(MouseLockMode::FREE);
        }
        return false;
    }
    virtual bool onKeyDown(int k,InputManager& m) override { return false; }
    virtual bool onMousePressed(int b,InputManager& m) override { 
        if(b == SDL_BUTTON_LEFT && m.getMouseCaptureMode() != MouseLockMode::LOCKED){
            m.setMouseCaptureMode(MouseLockMode::LOCKED); // lock the mouse if the player clicks the screen.
        }
        return false; 
    }
    virtual bool onMouseReleased(int b,InputManager& m) override { return false; }
    virtual bool onMouseScroll(float z,InputManager& m) override { 
        if(m.getMouseCaptureMode() == MouseLockMode::LOCKED) this->m_fastScale += z / 10.0f;
        if(this->m_fastScale < 0) this->m_fastScale = 0.1;
        return false; 
    }

private:
    void updateCameraRotation(Math3D::Transform& tx) {
        // Construct a Quaternion from our Euler angles
        // Note: Using YXZ or XYZ order depends on your Math.h convention
        tx.rotation = Math3D::Quat::FromEuler(Math3D::Vec3(m_Pitch, m_Yaw, 0.0f));
    }
};

#endif // FIRST_PERSON_CONTROLLER_H