#ifndef ECSCOMPONENTS_H
#define ECSCOMPONENTS_H

#include "neoecs.hpp"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Geometry/Mesh.h"
#include "Rendering/Materials/Material.h"
#include "Rendering/Geometry/Model.h"
#include "Rendering/Lighting/Light.h"
#include "Rendering/PostFX/ScreenEffects.h"

#include "Scene/Scene.h"
#include <cstdint>
#include <string>
#include <vector>

class SkyBox;

// Converts a script path/asset-ref into an inspector-friendly name.
// Example: "@assets/scripts/FPSController.lua" -> "FPS Controller"
std::string BuildScriptDisplayNameFromPath(const std::string& scriptPath);

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
    // Serializable source refs for scene/prefab save-load. Runtime pointers remain the render path.
    std::string modelAssetRef;
    std::string modelSourceRef; // Optional direct source model ref (for non-wrapper model instances).
    int modelForceSmoothNormals = 0; // Used with modelSourceRef where relevant (OBJ import path).
    std::string materialAssetRef; // Single mesh/material mode source material.
    bool materialOverridesSource = false; // When true, scene-saved material fields override materialAssetRef on reload.
    std::vector<std::string> modelPartMaterialAssetRefs; // Per-part overrides for model mode.
    std::vector<int> modelPartMaterialOverrides; // When non-zero, scene-saved fields override source part materials on reload.
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

enum class PhysicsBodyType {
    Static,
    Dynamic,
    Kinematic
};

enum class PhysicsColliderShape {
    Box,
    Sphere,
    Capsule
};

enum class PhysicsLayer : std::uint8_t {
    Default = 0,
    StaticWorld = 1,
    DynamicBody = 2,
    Character = 3,
    Trigger = 4
};

using PhysicsLayerMask = std::uint32_t;

constexpr PhysicsLayerMask PhysicsLayerBit(PhysicsLayer layer){
    return static_cast<PhysicsLayerMask>(1u << static_cast<std::uint32_t>(layer));
}

namespace PhysicsLayerMasks {
    constexpr PhysicsLayerMask None = 0u;
    constexpr PhysicsLayerMask All = 0xFFFFFFFFu;
    constexpr PhysicsLayerMask Gameplay =
        PhysicsLayerBit(PhysicsLayer::Default) |
        PhysicsLayerBit(PhysicsLayer::StaticWorld) |
        PhysicsLayerBit(PhysicsLayer::DynamicBody) |
        PhysicsLayerBit(PhysicsLayer::Character);
}

struct PhysicsMaterialProperties {
    float staticFriction = 0.6f;
    float dynamicFriction = 0.5f;
    float restitution = 0.0f;
    float density = 1.0f;
};

struct ColliderComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    PhysicsColliderShape shape = PhysicsColliderShape::Box;
    Math3D::Vec3 boxHalfExtents = Math3D::Vec3(0.5f, 0.5f, 0.5f);
    float sphereRadius = 0.5f;
    float capsuleRadius = 0.5f;
    float capsuleHeight = 1.0f;
    Math3D::Transform localOffset;
    PhysicsLayer layer = PhysicsLayer::Default;
    PhysicsLayerMask collisionMask = PhysicsLayerMasks::Gameplay;
    bool isTrigger = false;
    PhysicsMaterialProperties material;
    // Runtime backend-owned handle (Jolt shape/body proxy). Not serialized.
    void* runtimeShapeHandle = nullptr;

    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

struct RigidBodyComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    PhysicsBodyType bodyType = PhysicsBodyType::Dynamic;
    float mass = 1.0f;
    float gravityScale = 1.0f;
    float linearDamping = 0.02f;
    float angularDamping = 0.05f;
    Math3D::Vec3 linearVelocity = Math3D::Vec3::zero();
    Math3D::Vec3 angularVelocity = Math3D::Vec3::zero();
    bool lockLinearX = false;
    bool lockLinearY = false;
    bool lockLinearZ = false;
    bool lockAngularX = false;
    bool lockAngularY = false;
    bool lockAngularZ = false;
    bool useContinuousCollision = false;
    bool canSleep = true;
    bool startAwake = true;
    // Runtime backend-owned handle (Jolt BodyID/native pointer). Not serialized.
    void* runtimeBodyHandle = nullptr;

    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

struct CameraComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    PCamera camera;

    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

struct SkyboxComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    std::string skyboxAssetRef;
    std::string loadedSkyboxAssetRef;
    std::shared_ptr<SkyBox> runtimeSkyBox;

    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

struct SSAOComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    bool enabled = true;
    float radiusPx = 3.0f;
    float depthRadius = 0.025f;
    float bias = 0.001f;
    float intensity = 1.0f;
    float giBoost = 0.12f;
    int sampleCount = 8;
    std::shared_ptr<SSAOEffect> runtimeEffect;

    Graphics::PostProcessing::PPostProcessingEffect getEffectForCamera(const CameraSettings& settings);
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

struct DepthOfFieldComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    bool enabled = false;
    bool adaptiveFocus = false;
    float focusDistance = 8.0f;
    float focusRange = 4.0f;
    float blurStrength = 0.65f;
    float maxBlurPx = 7.0f;
    int sampleCount = 6;
    std::shared_ptr<DepthOfFieldEffect> runtimeEffect;

    Graphics::PostProcessing::PPostProcessingEffect getEffectForCamera(const CameraSettings& settings);
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

struct AntiAliasingComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    AntiAliasingPreset preset = AntiAliasingPreset::FXAA_Medium;
    std::shared_ptr<FXAAEffect> runtimeEffect;

    Graphics::PostProcessing::PPostProcessingEffect getEffectForCamera(const CameraSettings& settings);
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

struct ScriptComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    std::vector<std::string> scriptAssetRefs;

    bool addScriptAsset(const std::string& scriptAssetRef);
    bool hasScriptAsset(const std::string& scriptAssetRef) const;
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

#endif // ECSCOMPONENTS_H
