#ifndef CAMERA_H
#define CAMERA_H

#include <memory>
#include <glm/glm.hpp>

#include "Math.h" 


struct CameraSettings{
    float fov = 45.0f;
    float aspect = 1.33f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    Math3D::Rect viewPlane;
    bool isOrtho = false;
};

struct Camera{
    private:
        Math3D::Transform cameraTransform;
        CameraSettings settings;
        Camera(const Camera&) = delete;
        Camera& operator=(const Camera&) = delete;
        Camera() = default;

    public:

        Math3D::Transform& transform() { return cameraTransform; }
        void setTransform(const Math3D::Transform& tfm) {this->cameraTransform = tfm;}
        CameraSettings& getSettings() {return this->settings; }

        void setAspect(float a){ this->settings.aspect = a;}
        void setAspect(float width, float height) {this->settings.aspect = width / height; }


        Math3D::Mat4 getViewMatrix() const {
            auto mat = this->cameraTransform.toMat4();
            return Math3D::Mat4(glm::inverse(glm::mat4(mat.data)));
        }

        Math3D::Mat4 getProjectionMatrix(){
            if(settings.isOrtho){
                return glm::ortho(
                    settings.viewPlane.position.x,
                    settings.viewPlane.position.x + settings.viewPlane.size.x,
                    settings.viewPlane.position.y + settings.viewPlane.size.y,
                    settings.viewPlane.position.y
                );
            }

            return glm::perspective(
                glm::radians(settings.fov),
                settings.aspect,
                settings.nearPlane,
                settings.farPlane
            );
        }

        void resize(float w, float h){
            if(this->getSettings().isOrtho) {
                this->getSettings().viewPlane = Math3D::Rect(0, 0, w, h);
            }

            this->setAspect((float) w / h);
        }

        static std::shared_ptr<Camera> CreatePerspective(float fov, Math3D::Vec2 size, float zNear, float zFar){
            auto cam = std::shared_ptr<Camera>(new Camera());
            cam->settings.fov = fov;
            cam->settings.aspect = size.x / size.y;
            cam->settings.nearPlane = zNear;
            cam->settings.farPlane = zFar;
            cam->settings.isOrtho = false;

            return cam;
        }

        static std::shared_ptr<Camera> CreateOrthogonal(Math3D::Rect viewPlane, float zNear, float zFar){
            auto cam = std::shared_ptr<Camera>(new Camera());
            cam->settings.viewPlane = viewPlane;
            cam->settings.nearPlane = zNear;
            cam->settings.farPlane = zFar;
            cam->settings.isOrtho = true;

            return cam;
        }
};

typedef std::shared_ptr<Camera> PCamera;

#endif // CAMERA_H