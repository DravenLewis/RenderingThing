/**
 * @file src/Rendering/Lighting/ShadowRenderer.h
 * @brief Declarations for ShadowRenderer.
 */

#ifndef SHADOW_RENDERER_H
#define SHADOW_RENDERER_H

#include <memory>
#include <vector>
#include <cstdint>

#include "Rendering/Lighting/Light.h"
#include "Scene/Camera.h"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Lighting/ShadowMap.h"
#include "Rendering/Textures/Texture.h"

class Material;
class ShaderProgram;
class Mesh;

/// @brief Holds data for ShadowLightData.
struct ShadowLightData {
    int shadowMapIndex = -1;       // Index into 2D or cube shadow arrays
    ShadowType shadowType = ShadowType::Standard;
    float shadowStrength = 1.0f;
    float shadowBias = 0.0025f;
    float shadowNormalBias = 0.005f;
    int cascadeCount = 1;
    Math3D::Vec4 cascadeSplits = Math3D::Vec4(0,0,0,0);
    Math3D::Mat4 lightMatrices[4] = {
        Math3D::Mat4(1.0f),
        Math3D::Mat4(1.0f),
        Math3D::Mat4(1.0f),
        Math3D::Mat4(1.0f)
    };
};

/// @brief Holds data for ShadowCasterBounds.
struct ShadowCasterBounds {
    Math3D::Vec3 min;
    Math3D::Vec3 max;
};

/// @brief Represents the ShadowRenderer type.
class ShadowRenderer {
public:
    /// @brief Holds data for ShadowDrawItem.
    struct ShadowDrawItem {
        std::shared_ptr<Mesh> mesh;
        Math3D::Mat4 model;
        std::shared_ptr<Material> material;
        bool enableBackfaceCulling = true;
        bool hasBounds = false;
        Math3D::Vec3 boundsMin = Math3D::Vec3(0.0f, 0.0f, 0.0f);
        Math3D::Vec3 boundsMax = Math3D::Vec3(0.0f, 0.0f, 0.0f);
    };

    /**
     * @brief Begins frame.
     * @param camera Value for camera.
     * @param casters Value for casters.
     */
    static void BeginFrame(PCamera camera, const std::vector<ShadowCasterBounds>* casters = nullptr);
    /**
     * @brief Renders shadows.
     * @param mesh Value for mesh.
     * @param model Mode or type selector.
     * @param material Value for material.
     */
    static void RenderShadows(const std::shared_ptr<Mesh>& mesh, const Math3D::Mat4& model, const std::shared_ptr<Material>& material);
    /**
     * @brief Renders shadows batch.
     * @param items Value for items.
     */
    static void RenderShadowsBatch(const std::vector<ShadowDrawItem>& items);
    /**
     * @brief Binds shadow samplers.
     * @param program Value for program.
     */
    static void BindShadowSamplers(const std::shared_ptr<ShaderProgram>& program);
    /**
     * @brief Returns the shadow data for light.
     * @param index Identifier or index value.
     * @param light Value for light.
     * @param outData Buffer that receives data data.
     */
    static void GetShadowDataForLight(size_t index, const Light& light, ShadowLightData& outData);
    /**
     * @brief Returns the frame id.
     * @return Result of this operation.
     */
    static uint64_t GetFrameId();
    /**
     * @brief Checks whether enabled.
     * @return True when the condition is satisfied; otherwise false.
     */
    static bool IsEnabled();
    /**
     * @brief Returns the depth buffer.
     * @return Pointer to the resulting object.
     */
    static std::shared_ptr<Texture> GetDepthBuffer();
    /**
     * @brief Sets the debug shadows.
     * @param enabled Flag controlling enabled.
     */
    static void SetDebugShadows(bool enabled);
    /**
     * @brief Returns the debug shadows.
     * @return True when the operation succeeds; otherwise false.
     */
    static bool GetDebugShadows();
    /**
     * @brief Cycles the active debug shadow visualization mode.
     */
    static void CycleDebugShadows();
    /**
     * @brief Returns the debug shadows mode.
     * @return Computed numeric result.
     */
    static int GetDebugShadowsMode();
    /**
     * @brief Sets the debug shadows mode.
     * @param mode Mode or type selector.
     */
    static void SetDebugShadowsMode(int mode);
    /**
     * @brief Sets the global debug override enabled.
     * @param enabled Flag controlling enabled.
     */
    static void SetGlobalDebugOverrideEnabled(bool enabled);
    /**
     * @brief Returns the global debug override enabled.
     * @return True when the operation succeeds; otherwise false.
     */
    static bool GetGlobalDebugOverrideEnabled();
    /**
     * @brief Sets the global debug override mode.
     * @param mode Mode or type selector.
     */
    static void SetGlobalDebugOverrideMode(int mode);
    /**
     * @brief Returns the global debug override mode.
     * @return Computed numeric result.
     */
    static int GetGlobalDebugOverrideMode();
    /**
     * @brief Sets the selected light index.
     * @param index Identifier or index value.
     */
    static void SetSelectedLightIndex(int index);
    /**
     * @brief Returns the selected light index.
     * @return Computed numeric result.
     */
    static int GetSelectedLightIndex();
    /**
     * @brief Sets the directional cascade kernel margin texels.
     * @param value Value for value.
     */
    static void SetDirectionalCascadeKernelMarginTexels(float value);
    /**
     * @brief Returns the directional cascade kernel margin texels.
     * @return Computed numeric result.
     */
    static float GetDirectionalCascadeKernelMarginTexels();
    /**
     * @brief Sets the shadow receiver normal blend.
     * @param value Value for value.
     */
    static void SetShadowReceiverNormalBlend(float value);
    /**
     * @brief Returns the shadow receiver normal blend.
     * @return Computed numeric result.
     */
    static float GetShadowReceiverNormalBlend();
    /**
     * @brief Sets the debug logging.
     * @param enabled Flag controlling enabled.
     */
    static void SetDebugLogging(bool enabled);
    /**
     * @brief Returns the debug logging.
     * @return True when the operation succeeds; otherwise false.
     */
    static bool GetDebugLogging();

private:
    /**
     * @brief Ensures shadow programs.
     */
    static void ensureShadowPrograms();
    /**
     * @brief Computes directional matrix.
     * @param light Value for light.
     * @param camera Value for camera.
     * @return Result of this operation.
     */
    static Math3D::Mat4 computeDirectionalMatrix(const Light& light, PCamera camera);
    /**
     * @brief Computes spot matrix.
     * @param light Value for light.
     * @return Result of this operation.
     */
    static Math3D::Mat4 computeSpotMatrix(const Light& light);
    /**
     * @brief Computes point matrices.
     * @param light Value for light.
     * @param outMatrices Output value for matrices.
     */
    static void computePointMatrices(const Light& light, std::vector<Math3D::Mat4>& outMatrices);
};

#endif // SHADOW_RENDERER_H
