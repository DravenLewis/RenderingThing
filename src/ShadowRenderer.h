#ifndef SHADOW_RENDERER_H
#define SHADOW_RENDERER_H

#include <memory>
#include <vector>

#include "Light.h"
#include "Camera.h"
#include "Math.h"
#include "ShadowMap.h"
#include "Texture.h"

class Material;
class ShaderProgram;
class Mesh;

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

class ShadowRenderer {
public:
    static void BeginFrame(PCamera camera);
    static void RenderShadows(const std::shared_ptr<Mesh>& mesh, const Math3D::Mat4& model, const std::shared_ptr<Material>& material);
    static void BindShadowSamplers(const std::shared_ptr<ShaderProgram>& program);
    static void GetShadowDataForLight(size_t index, const Light& light, ShadowLightData& outData);
    static bool IsEnabled();
    static std::shared_ptr<Texture> GetDepthBuffer();

private:
    static void ensureShadowPrograms();
    static Math3D::Mat4 computeDirectionalMatrix(const Light& light, PCamera camera);
    static Math3D::Mat4 computeSpotMatrix(const Light& light);
    static void computePointMatrices(const Light& light, std::vector<Math3D::Mat4>& outMatrices);
};

#endif // SHADOW_RENDERER_H
