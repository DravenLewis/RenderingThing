#ifndef SCENEOBJECT_H
#define SCENEOBJECT_H

#include <memory>
#include <utility>

#include "Math.h"
#include "Light.h"
#include "Model.h"

class SceneObject {
    public:
        virtual ~SceneObject() = default;

        virtual void draw(PCamera cam) {}
        virtual void drawShadows() {}
        virtual void addLights(LightManager& manager) const {}
};

typedef std::shared_ptr<SceneObject> PSceneObject;

class ModelSceneObject : public SceneObject {
    public:
        explicit ModelSceneObject(PModel model) : model(std::move(model)) {}

        void draw(PCamera cam) override {
            if(model){
                model->draw(cam);
            }
        }

        void drawShadows() override {
            if(model){
                model->drawShadows();
            }
        }

        PModel getModel() const { return model; }
        void setModel(PModel nextModel) { model = std::move(nextModel); }

        static std::shared_ptr<ModelSceneObject> Create(PModel model){
            return std::make_shared<ModelSceneObject>(std::move(model));
        }

    private:
        PModel model;
};

typedef std::shared_ptr<ModelSceneObject> PModelSceneObject;

class LightSceneObject : public SceneObject {
    public:
        explicit LightSceneObject(const Light& light) : light(light) {}

        void addLights(LightManager& manager) const override {
            manager.addLight(light);
        }

        Light& getLight() { return light; }
        const Light& getLight() const { return light; }
        void setLight(const Light& nextLight) { light = nextLight; }

        static std::shared_ptr<LightSceneObject> Create(const Light& light){
            return std::make_shared<LightSceneObject>(light);
        }

    private:
        Light light;
};

typedef std::shared_ptr<LightSceneObject> PLightSceneObject;

class GameObject : public SceneObject {
    public:
        GameObject() = default;
        ~GameObject() override = default;

        Math3D::Transform& transform() { return localTransform; }
        const Math3D::Transform& transform() const { return localTransform; }

    private:
        Math3D::Transform localTransform;
};

typedef std::shared_ptr<GameObject> PGameObject;

#endif // SCENEOBJECT_H
