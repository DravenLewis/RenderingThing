/**
 * @file src/ECS/Core/ECSComponents.h
 * @brief ECS component types used by runtime systems and editor tooling.
 */

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

/// @brief Holds data for IEditorCompatibleComponent.
struct IEditorCompatibleComponent : public NeoECS::ECSComponent{
    using NeoECS::ECSComponent::ECSComponent;
    bool editorPanelEnabled = true;
    bool editorPanelHidden = false;

    /**
     * @brief Gets a mutable pointer to the editor-enabled flag.
     * @return Pointer to the enabled flag, or `nullptr` when not applicable.
     */
    virtual bool* getEditorEnabledState(){
        return &editorPanelEnabled;
    }
    /**
     * @brief Gets a read-only pointer to the editor-enabled flag.
     * @return Pointer to the enabled flag, or `nullptr` when not applicable.
     */
    virtual const bool* getEditorEnabledState() const{
        return &editorPanelEnabled;
    }
    /**
     * @brief Gets a mutable pointer to the editor-hidden flag.
     * @return Pointer to the hidden flag, or `nullptr` when not applicable.
     */
    virtual bool* getEditorHiddenState(){
        return &editorPanelHidden;
    }
    /**
     * @brief Gets a read-only pointer to the editor-hidden flag.
     * @return Pointer to the hidden flag, or `nullptr` when not applicable.
     */
    virtual const bool* getEditorHiddenState() const{
        return &editorPanelHidden;
    }

    /**
     * @brief Checks whether editor hidden.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool isEditorHidden() const{
        const bool* hidden = getEditorHiddenState();
        return hidden ? *hidden : false;
    }

    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    virtual void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) = 0;
};

/**
 * @brief Checks whether component active.
 * @param component Component to query.
 * @return True when the condition is satisfied; otherwise false.
 */
inline bool IsComponentActive(const IEditorCompatibleComponent* component){
    if(!component){
        return false;
    }
    const bool* enabled = component->getEditorEnabledState();
    return (enabled == nullptr) || *enabled;
}

/**
 * @brief Sets whether a component is active in the editor.
 * @param component Component to update.
 * @param active New enabled state.
 */
inline void SetComponentActive(IEditorCompatibleComponent* component, bool active){
    if(!component){
        return;
    }
    if(bool* enabled = component->getEditorEnabledState()){
        *enabled = active;
    }
}

/// @brief Holds data for EntityPropertiesComponent.
struct EntityPropertiesComponent : public IEditorCompatibleComponent {
    bool ignoreRaycastHit = false;

    /**
     * @brief Constructs the entity properties component.
     * @param parentEntity Owning entity pointer.
     */
    explicit EntityPropertiesComponent(NeoECS::ECSEntity* parentEntity = nullptr)
        : IEditorCompatibleComponent(parentEntity){
        editorPanelHidden = true;
    }

    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for TransformComponent.
struct TransformComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    Math3D::Transform local;

    /**
     * @brief Transform components are always shown in editor panels.
     * @return Always returns `nullptr` to indicate no toggle flag.
     */
    bool* getEditorEnabledState() override { return nullptr; }
    const bool* getEditorEnabledState() const override { return nullptr; }
    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for MeshRendererComponent.
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

    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for LightComponent.
struct LightComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    Light light;
    bool syncTransform = true;
    bool syncDirection = true;

    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Enumerates values for BoundsType.
enum class BoundsType {
    Box,
    Sphere,
    Capsule
};

/// @brief Holds data for BoundsComponent.
struct BoundsComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    BoundsType type = BoundsType::Sphere;
    Math3D::Vec3 size = Math3D::Vec3(1.0f, 1.0f, 1.0f); // Box extents (half-size)
    float radius = 0.5f; // Sphere/Capsule radius
    float height = 1.0f; // Capsule cylinder height
    Math3D::Vec3 offset = Math3D::Vec3(0.0f, 0.0f, 0.0f); // Local-space center offset

    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Enumerates values for PhysicsBodyType.
enum class PhysicsBodyType {
    Static,
    Dynamic,
    Kinematic
};

/// @brief Enumerates values for PhysicsColliderShape.
enum class PhysicsColliderShape {
    Box,
    Sphere,
    Capsule
};

/// @brief Enumerates values for PhysicsLayer.
enum class PhysicsLayer : std::uint8_t {
    Default = 0,
    StaticWorld = 1,
    DynamicBody = 2,
    Character = 3,
    Trigger = 4
};

using PhysicsLayerMask = std::uint32_t;

/**
 * @brief Builds a bit mask for a physics layer.
 * @param layer Layer to convert into a bit position.
 * @return Bit mask with only the requested layer enabled.
 */
constexpr PhysicsLayerMask PhysicsLayerBit(PhysicsLayer layer){
    return static_cast<PhysicsLayerMask>(1u << static_cast<std::uint32_t>(layer));
}

namespace PhysicsLayerMasks {
    /** @brief Mask with no layers enabled. */
    constexpr PhysicsLayerMask None = 0u;
    /** @brief Mask with all 32 layer bits enabled. */
    constexpr PhysicsLayerMask All = 0xFFFFFFFFu;
    /** @brief Common gameplay mask used for default collision filtering. */
    constexpr PhysicsLayerMask Gameplay =
        PhysicsLayerBit(PhysicsLayer::Default) |
        PhysicsLayerBit(PhysicsLayer::StaticWorld) |
        PhysicsLayerBit(PhysicsLayer::DynamicBody) |
        PhysicsLayerBit(PhysicsLayer::Character);
}

/// @brief Holds data for PhysicsMaterialProperties.
struct PhysicsMaterialProperties {
    float staticFriction = 0.6f;
    float dynamicFriction = 0.5f;
    float restitution = 0.0f;
    float density = 1.0f;
};

/// @brief Holds data for ColliderComponent.
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

    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for RigidBodyComponent.
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

    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for CameraComponent.
struct CameraComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    PCamera camera;

    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for SkyboxComponent.
struct SkyboxComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    std::string skyboxAssetRef;
    std::string loadedSkyboxAssetRef;
    std::shared_ptr<SkyBox> runtimeSkyBox;

    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for SSAOComponent.
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

    /**
     * @brief Gets a mutable pointer to the editor-enabled flag.
     * @return Pointer to the enabled flag.
     */
    bool* getEditorEnabledState() override { return &enabled; }
    const bool* getEditorEnabledState() const override { return &enabled; }
    /**
     * @brief Builds the SSAO post-process effect for camera rendering.
     * @param settings Active camera settings.
     * @return Shared effect instance configured from this component.
     */
    Graphics::PostProcessing::PPostProcessingEffect getEffectForCamera(const CameraSettings& settings);
    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for DepthOfFieldComponent.
struct DepthOfFieldComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    bool enabled = false;
    bool adaptiveFocus = false;
    bool adaptiveFocusDebugDraw = false;
    float focusDistance = 8.0f;
    float focusRange = 4.0f;
    float focusBandWidth = 0.85f;
    float blurRamp = 2.0f;
    float blurDistanceLerp = 0.35f;
    float fallbackFocusRange = 6.0f;
    float blurStrength = 0.65f;
    float maxBlurPx = 7.0f;
    int sampleCount = 6;
    bool debugCocView = false;
    std::shared_ptr<DepthOfFieldEffect> runtimeEffect;

    /**
     * @brief Gets a mutable pointer to the editor-enabled flag.
     * @return Pointer to the enabled flag.
     */
    bool* getEditorEnabledState() override { return &enabled; }
    const bool* getEditorEnabledState() const override { return &enabled; }
    /**
     * @brief Builds the depth-of-field effect for camera rendering.
     * @param settings Active camera settings.
     * @return Shared effect instance configured from this component.
     */
    Graphics::PostProcessing::PPostProcessingEffect getEffectForCamera(const CameraSettings& settings);
    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for BloomComponent.
struct BloomComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    bool enabled = false;
    bool adaptiveBloom = false;
    float threshold = 0.75f;
    float softKnee = 0.5f;
    float intensity = 0.65f;
    float radiusPx = 6.0f;
    int sampleCount = 8;
    Math3D::Vec3 tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
    std::shared_ptr<BloomEffect> runtimeEffect;
    float liveThreshold = 0.75f; // Runtime effective threshold after auto-exposure coupling.
    float liveIntensity = 0.65f; // Runtime effective intensity after auto-exposure coupling.
    bool liveAutoExposureDriven = false;

    /**
     * @brief Gets a mutable pointer to the editor-enabled flag.
     * @return Pointer to the enabled flag.
     */
    bool* getEditorEnabledState() override { return &enabled; }
    const bool* getEditorEnabledState() const override { return &enabled; }
    /**
     * @brief Builds the bloom effect for camera rendering.
     * @param settings Active camera settings.
     * @return Shared effect instance configured from this component.
     */
    Graphics::PostProcessing::PPostProcessingEffect getEffectForCamera(const CameraSettings& settings);
    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for AutoExposureComponent.
struct AutoExposureComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    bool enabled = true;
    float minExposure = 0.6f;
    float maxExposure = 2.4f;
    float exposureCompensation = 0.0f;
    float adaptationSpeedUp = 1.2f;
    float adaptationSpeedDown = 0.7f;
    std::shared_ptr<AutoExposureEffect> runtimeEffect;

    /**
     * @brief Gets a mutable pointer to the editor-enabled flag.
     * @return Pointer to the enabled flag.
     */
    bool* getEditorEnabledState() override { return &enabled; }
    const bool* getEditorEnabledState() const override { return &enabled; }
    /**
     * @brief Builds the auto-exposure effect for camera rendering.
     * @param settings Active camera settings.
     * @return Shared effect instance configured from this component.
     */
    Graphics::PostProcessing::PPostProcessingEffect getEffectForCamera(const CameraSettings& settings);
    /**
     * @brief Applies auto-exposure coupling updates to bloom settings.
     * @param bloom Bloom component to update with live exposure values.
     */
    void applyBloomCoupling(BloomComponent* bloom);
    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for AntiAliasingComponent.
struct AntiAliasingComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    AntiAliasingPreset preset = AntiAliasingPreset::FXAA_Medium;
    std::shared_ptr<FXAAEffect> runtimeEffect;

    /**
     * @brief Builds the anti-aliasing effect for camera rendering.
     * @param settings Active camera settings.
     * @return Shared effect instance configured from this component.
     */
    Graphics::PostProcessing::PPostProcessingEffect getEffectForCamera(const CameraSettings& settings);
    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

/// @brief Holds data for ScriptComponent.
struct ScriptComponent : public IEditorCompatibleComponent {
    using IEditorCompatibleComponent::IEditorCompatibleComponent;
    std::vector<std::string> scriptAssetRefs;

    /**
     * @brief Adds a script asset reference to this entity.
     * @param scriptAssetRef Script asset reference to append.
     * @return True when the reference was added; false when it already exists.
     */
    bool addScriptAsset(const std::string& scriptAssetRef);
    /**
     * @brief Checks whether a script asset reference is already attached.
     * @param scriptAssetRef Script asset reference to check.
     * @return True when the script reference is present; otherwise false.
     */
    bool hasScriptAsset(const std::string& scriptAssetRef) const;
    /**
     * @brief Draws editor controls for this component.
     * @param ecsPtr ECS instance that owns the component.
     * @param scenePtr Scene context used by editor widgets.
     */
    void drawPropertyWidget(NeoECS::NeoECS* ecsPtr = nullptr, PScene scenePtr = nullptr) override;
};

#endif // ECSCOMPONENTS_H
