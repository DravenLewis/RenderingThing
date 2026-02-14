#ifndef ECSCOMPONENTS_H
#define ECSCOMPONENTS_H

#include "neoecs.hpp"
#include "Math.h"
#include "Mesh.h"
#include "Material.h"
#include "Model.h"
#include "Light.h"

#include "Scene.h"

struct IEditorCompatibleComponent : public NeoECS::ECSComponent{
    using NeoECS::ECSComponent::ECSComponent;

    virtual void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) = 0;
};

struct TransformComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    Math3D::Transform local;

    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

struct MeshRendererComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    PModel model;
    Math3D::Transform localOffset;
    bool visible = true;
    bool enableBackfaceCulling = true;

    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

struct LightComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    Light light;
    bool syncTransform = true;
    bool syncDirection = true;

    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

enum class BoundsType {
    Box,
    Sphere,
    Capsule
};

struct BoundsComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    BoundsType type = BoundsType::Sphere;
    Math3D::Vec3 size = Math3D::Vec3(1.0f, 1.0f, 1.0f); // Box extents (half-size)
    float radius = 0.5f; // Sphere/Capsule radius
    float height = 1.0f; // Capsule full height

    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

struct CameraComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    PCamera camera;

    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

#endif // ECSCOMPONENTS_H
