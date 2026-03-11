/**
 * @file src/Scene/FirstPersonController.h
 * @brief First-person camera controller driven by keyboard and mouse input.
 */

#ifndef FIRST_PERSON_CONTROLLER_H
#define FIRST_PERSON_CONTROLLER_H

#include <memory>
#include <SDL3/SDL.h>
#include "Platform/Input/InputManager.h"
#include "Scene/Camera.h"
#include "Foundation/Math/Math3D.h"

/**
 * @brief Handles first-person movement and look controls for a camera.
 *
 * Implements `IEventHandler` so it can receive input callbacks from `InputManager`.
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
    /**
     * @brief Constructs the controller for a camera.
     * @param cam Camera instance controlled by this object.
     */
    FirstPersonController(std::shared_ptr<Camera> cam)
        : m_Camera(cam) {}

    /**
     * @brief Registers this controller with the input manager.
     * @param inputManager Input manager used to dispatch events.
     */
    void init(std::shared_ptr<InputManager> inputManager) {
        if (inputManager) {
            inputManager->addEventHandler(shared_from_this());
        }
    }

    /**
     * @brief Applies per-frame movement and rotation input to the camera.
     * @param dt Frame delta time in seconds.
     * @param input Input manager used to query key state.
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

    /**
     * @brief Handles mouse movement events for look rotation.
     * @param x Mouse delta X.
     * @param y Mouse delta Y.
     * @param manager Input manager dispatching the event.
     * @return True when the event was handled; otherwise false.
     */
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

    /**
     * @brief Handles key release events.
     * @param k Released key scan code.
     * @param m Input manager dispatching the event.
     * @return True when the event was handled; otherwise false.
     */
    virtual bool onKeyUp(int k,InputManager& m) override { 
        if(k == SDL_SCANCODE_GRAVE && (m.getMouseCaptureMode() == MouseLockMode::LOCKED || m.getMouseCaptureMode() == MouseLockMode::CAPTURED)){
            m.setMouseCaptureMode(MouseLockMode::FREE);
        }
        return false;
    }
    /**
     * @brief Handles key press events.
     * @param k Pressed key scan code.
     * @param m Input manager dispatching the event.
     * @return True when the event was handled; otherwise false.
     */
    virtual bool onKeyDown(int k,InputManager& m) override { return false; }
    /**
     * @brief Handles mouse button press events.
     * @param b Pressed mouse button code.
     * @param m Input manager dispatching the event.
     * @return True when the event was handled; otherwise false.
     */
    virtual bool onMousePressed(int b,InputManager& m) override { 
        if(b == SDL_BUTTON_LEFT && m.getMouseCaptureMode() != MouseLockMode::LOCKED){
            m.setMouseCaptureMode(MouseLockMode::LOCKED); // lock the mouse if the player clicks the screen.
        }
        return false; 
    }
    /**
     * @brief Handles mouse button release events.
     * @param b Released mouse button code.
     * @param m Input manager dispatching the event.
     * @return True when the event was handled; otherwise false.
     */
    virtual bool onMouseReleased(int b,InputManager& m) override { return false; }
    /**
     * @brief Handles mouse wheel events used to adjust fast movement scaling.
     * @param z Scroll delta.
     * @param m Input manager dispatching the event.
     * @return True when the event was handled; otherwise false.
     */
    virtual bool onMouseScroll(float z,InputManager& m) override { 
        if(m.getMouseCaptureMode() == MouseLockMode::LOCKED) this->m_fastScale += z / 10.0f;
        if(this->m_fastScale < 0) this->m_fastScale = 0.1;
        return false; 
    }

private:
    /**
     * @brief Updates transform rotation from yaw and pitch state.
     * @param tx Transform to update in place.
     */
    void updateCameraRotation(Math3D::Transform& tx) {
        // Construct a Quaternion from our Euler angles
        // Note: Using YXZ or XYZ order depends on your math convention
        tx.rotation = Math3D::Quat::FromEuler(Math3D::Vec3(m_Pitch, m_Yaw, 0.0f));
    }
};

#endif // FIRST_PERSON_CONTROLLER_H
