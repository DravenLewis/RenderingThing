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

#endif // ECSCOMPONENTS_H
