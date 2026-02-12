#ifndef ECSCOMPONENTS_H
#define ECSCOMPONENTS_H

#include "neoecs.hpp"
#include "Math.h"
#include "Mesh.h"
#include "Material.h"
#include "Model.h"
#include "Light.h"

struct TransformComponent : public NeoECS::ECSComponent {
    using NeoECS::ECSComponent::ECSComponent;
    Math3D::Transform local;
};

struct MeshRendererComponent : public NeoECS::ECSComponent {
    using NeoECS::ECSComponent::ECSComponent;
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    PModel model;
    Math3D::Transform localOffset;
    bool visible = true;
    bool enableBackfaceCulling = true;
};

struct LightComponent : public NeoECS::ECSComponent {
    using NeoECS::ECSComponent::ECSComponent;
    Light light;
    bool syncTransform = true;
    bool syncDirection = true;
};

enum class BoundsType {
    Box,
    Sphere,
    Capsule
};

struct BoundsComponent : public NeoECS::ECSComponent {
    using NeoECS::ECSComponent::ECSComponent;
    BoundsType type = BoundsType::Sphere;
    Math3D::Vec3 size = Math3D::Vec3(1.0f, 1.0f, 1.0f); // Box extents (half-size)
    float radius = 0.5f; // Sphere/Capsule radius
    float height = 1.0f; // Capsule full height
};

struct CameraComponent : public NeoECS::ECSComponent {
    using NeoECS::ECSComponent::ECSComponent;
    PCamera camera;
};

#endif // ECSCOMPONENTS_H
