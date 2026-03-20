/**
 * @file src/Rendering/Materials/PBRMaterial.cpp
 * @brief Implementation for PBRMaterial.
 */

#include "Rendering/Materials/PBRMaterial.h"

#include "Assets/Core/Asset.h"
#include "Foundation/Logging/Logbot.h"
#include "Rendering/Core/Screen.h"
#include "Rendering/Lighting/LightUtils.h"
#include "Rendering/Lighting/ShadowRenderer.h"
#include "Rendering/Textures/SkyBox.h"

#include <chrono>

namespace {
    constexpr int BASE_COLOR_SLOT = 0;
    constexpr int METAL_ROUGH_SLOT = 1;
    constexpr int NORMAL_SLOT = 2;
    constexpr int EMISSIVE_SLOT = 3;
    constexpr int OCCLUSION_SLOT = 4;
    constexpr int ENV_SLOT = 5;
    constexpr int HEIGHT_SLOT = 6;
    constexpr int ROUGHNESS_SLOT = 7;
    constexpr int LOCAL_PROBE_SLOT = 11;

    const std::vector<Light>& GetActiveLights(){
        auto env = Screen::GetCurrentEnvironment();
        if(env){
            return env->getLightsForUpload();
        }
        static const std::vector<Light> EMPTY;
        return EMPTY;
    }

    std::shared_ptr<ShaderProgram> compileShaderProgramSafe(const char* cacheName, const char* vertexAssetRef, const char* fragmentAssetRef){
        auto vertexShader = AssetManager::Instance.getOrLoad(vertexAssetRef);
        auto fragmentShader = AssetManager::Instance.getOrLoad(fragmentAssetRef);
        if(!vertexShader || !fragmentShader){
            LogBot.Log(
                LOG_ERRO,
                "[PBRMaterial] Missing shader asset(s) for '%s' (vert='%s', frag='%s').",
                cacheName ? cacheName : "<unnamed>",
                vertexAssetRef ? vertexAssetRef : "<null>",
                fragmentAssetRef ? fragmentAssetRef : "<null>"
            );
            return nullptr;
        }
        return ShaderCacheManager::INSTANCE.getOrCompile(
            cacheName ? cacheName : "PBRMaterialShader",
            vertexShader->asString(),
            fragmentShader->asString()
        );
    }

    float getMaterialTimeSeconds(){
        using clock = std::chrono::steady_clock;
        static const clock::time_point START = clock::now();
        const clock::time_point now = clock::now();
        return std::chrono::duration<float>(now - START).count();
    }
}

PBRMaterial::PBRMaterial(std::shared_ptr<ShaderProgram> program) : Material(program) {
    BaseColor.onChange([this](Math3D::Vec4, Math3D::Vec4 newValue) -> bool{
        set<Math3D::Vec4>("u_baseColor", newValue);
        set<Math3D::Vec4>("u_color", newValue); // fallback shader compatibility
        return true;
    });

    BaseColorTex.onChange([this](std::shared_ptr<Texture>, std::shared_ptr<Texture> newValue) -> bool{
        set<GLUniformUpload::TextureSlot>("u_baseColorTex", GLUniformUpload::TextureSlot(newValue, BASE_COLOR_SLOT));
        set<int>("u_useBaseColorTex", newValue ? 1 : 0);
        return true;
    });

    BsdfModel.onChange([this](int, int newValue) -> bool{
        set<int>("u_bsdfModel", Math3D::Clamp(newValue, 0, 2));
        return true;
    });

    Metallic.onChange([this](float, float newValue) -> bool{
        set<float>("u_metallic", newValue);
        return true;
    });

    Roughness.onChange([this](float, float newValue) -> bool{
        set<float>("u_roughness", newValue);
        return true;
    });

    RoughnessTex.onChange([this](std::shared_ptr<Texture>, std::shared_ptr<Texture> newValue) -> bool{
        set<GLUniformUpload::TextureSlot>("u_roughnessTex", GLUniformUpload::TextureSlot(newValue, ROUGHNESS_SLOT));
        set<int>("u_useRoughnessTex", newValue ? 1 : 0);
        return true;
    });

    MetallicRoughnessTex.onChange([this](std::shared_ptr<Texture>, std::shared_ptr<Texture> newValue) -> bool{
        set<GLUniformUpload::TextureSlot>("u_metallicRoughnessTex", GLUniformUpload::TextureSlot(newValue, METAL_ROUGH_SLOT));
        set<int>("u_useMetallicRoughnessTex", newValue ? 1 : 0);
        return true;
    });

    NormalTex.onChange([this](std::shared_ptr<Texture>, std::shared_ptr<Texture> newValue) -> bool{
        set<GLUniformUpload::TextureSlot>("u_normalTex", GLUniformUpload::TextureSlot(newValue, NORMAL_SLOT));
        set<int>("u_useNormalTex", newValue ? 1 : 0);
        return true;
    });

    NormalScale.onChange([this](float, float newValue) -> bool{
        set<float>("u_normalScale", newValue);
        return true;
    });

    HeightTex.onChange([this](std::shared_ptr<Texture>, std::shared_ptr<Texture> newValue) -> bool{
        set<GLUniformUpload::TextureSlot>("u_heightTex", GLUniformUpload::TextureSlot(newValue, HEIGHT_SLOT));
        set<int>("u_useHeightTex", newValue ? 1 : 0);
        return true;
    });

    HeightScale.onChange([this](float, float newValue) -> bool{
        set<float>("u_heightScale", newValue);
        return true;
    });

    EmissiveTex.onChange([this](std::shared_ptr<Texture>, std::shared_ptr<Texture> newValue) -> bool{
        set<GLUniformUpload::TextureSlot>("u_emissiveTex", GLUniformUpload::TextureSlot(newValue, EMISSIVE_SLOT));
        set<int>("u_useEmissiveTex", newValue ? 1 : 0);
        return true;
    });

    EmissiveColor.onChange([this](Math3D::Vec3, Math3D::Vec3 newValue) -> bool{
        set<Math3D::Vec3>("u_emissiveColor", newValue);
        return true;
    });

    EmissiveStrength.onChange([this](float, float newValue) -> bool{
        set<float>("u_emissiveStrength", newValue);
        return true;
    });

    OcclusionTex.onChange([this](std::shared_ptr<Texture>, std::shared_ptr<Texture> newValue) -> bool{
        set<GLUniformUpload::TextureSlot>("u_occlusionTex", GLUniformUpload::TextureSlot(newValue, OCCLUSION_SLOT));
        set<int>("u_useOcclusionTex", newValue ? 1 : 0);
        return true;
    });

    OcclusionStrength.onChange([this](float, float newValue) -> bool{
        set<float>("u_aoStrength", newValue);
        return true;
    });

    EnvMap.onChange([this](std::shared_ptr<CubeMap>, std::shared_ptr<CubeMap> newValue) -> bool{
        set<GLUniformUpload::CubeMapSlot>("u_envMap", GLUniformUpload::CubeMapSlot(newValue, ENV_SLOT));
        set<int>("u_useEnvMap", newValue ? 1 : 0);
        return true;
    });

    EnvStrength.onChange([this](float, float newValue) -> bool{
        set<float>("u_envStrength", newValue);
        return true;
    });

    UseEnvMap.onChange([this](int, int newValue) -> bool{
        set<int>("u_useEnvMap", newValue);
        return true;
    });

    UVScale.onChange([this](Math3D::Vec2, Math3D::Vec2 newValue) -> bool{
        set<Math3D::Vec2>("u_uvScale", newValue);
        return true;
    });

    UVOffset.onChange([this](Math3D::Vec2, Math3D::Vec2 newValue) -> bool{
        set<Math3D::Vec2>("u_uvOffset", newValue);
        return true;
    });

    AlphaCutoff.onChange([this](float, float newValue) -> bool{
        set<float>("u_alphaCutoff", newValue);
        return true;
    });

    UseAlphaClip.onChange([this](int, int newValue) -> bool{
        set<int>("u_useAlphaClip", newValue);
        return true;
    });

    Transmission.onChange([this](float, float newValue) -> bool{
        set<float>("u_transmission", Math3D::Clamp(newValue, 0.0f, 1.0f));
        return true;
    });

    Ior.onChange([this](float, float newValue) -> bool{
        set<float>("u_ior", Math3D::Clamp(newValue, 1.0f, 2.5f));
        return true;
    });

    Thickness.onChange([this](float, float newValue) -> bool{
        set<float>("u_thickness", Math3D::Max(0.001f, newValue));
        return true;
    });

    AttenuationColor.onChange([this](Math3D::Vec3, Math3D::Vec3 newValue) -> bool{
        set<Math3D::Vec3>(
            "u_attenuationColor",
            Math3D::Vec3(
                Math3D::Clamp(newValue.x, 0.001f, 1.0f),
                Math3D::Clamp(newValue.y, 0.001f, 1.0f),
                Math3D::Clamp(newValue.z, 0.001f, 1.0f)
            )
        );
        return true;
    });

    AttenuationDistance.onChange([this](float, float newValue) -> bool{
        set<float>("u_attenuationDistance", Math3D::Max(0.001f, newValue));
        return true;
    });

    ScatteringStrength.onChange([this](float, float newValue) -> bool{
        set<float>("u_scatteringStrength", Math3D::Clamp(newValue, 0.0f, 4.0f));
        return true;
    });

    EnableWaveDisplacement.onChange([this](int, int newValue) -> bool{
        set<int>("u_enableWaveDisplacement", newValue != 0 ? 1 : 0);
        return true;
    });

    WaveAmplitude.onChange([this](float, float newValue) -> bool{
        set<float>("u_waveAmplitude", Math3D::Max(0.0f, newValue));
        return true;
    });

    WaveFrequency.onChange([this](float, float newValue) -> bool{
        set<float>("u_waveFrequency", Math3D::Max(0.0f, newValue));
        return true;
    });

    WaveSpeed.onChange([this](float, float newValue) -> bool{
        set<float>("u_waveSpeed", newValue);
        return true;
    });

    WaveChoppiness.onChange([this](float, float newValue) -> bool{
        set<float>("u_waveChoppiness", Math3D::Clamp(newValue, 0.0f, 1.0f));
        return true;
    });

    WaveSecondaryScale.onChange([this](float, float newValue) -> bool{
        set<float>("u_waveSecondaryScale", Math3D::Max(0.01f, newValue));
        return true;
    });

    WaveDirection.onChange([this](Math3D::Vec2, Math3D::Vec2 newValue) -> bool{
        set<Math3D::Vec2>("u_waveDirection", newValue);
        return true;
    });

    WaveTextureInfluence.onChange([this](float, float newValue) -> bool{
        set<float>("u_waveTextureInfluence", Math3D::Max(0.0f, newValue));
        return true;
    });

    WaveTextureSpeed.onChange([this](Math3D::Vec2, Math3D::Vec2 newValue) -> bool{
        set<Math3D::Vec2>("u_waveTextureSpeed", newValue);
        return true;
    });

    ViewPos.onChange([this](Math3D::Vec3, Math3D::Vec3 newValue) -> bool{
        set<Math3D::Vec3>("u_viewPos", newValue);
        return true;
    });

    BaseColor = Color::WHITE;
    BsdfModel = static_cast<int>(PBRBsdfModel::Standard);
    Metallic = 0.0f;
    Roughness = 1.0f;
    NormalScale = 1.0f;
    HeightScale = 0.02f;
    EmissiveColor = Math3D::Vec3(0, 0, 0);
    EmissiveStrength = 1.0f;
    OcclusionStrength = 1.0f;
    EnvStrength = 1.0f;
    UseEnvMap = 0;
    UVScale = Math3D::Vec2(1, 1);
    UVOffset = Math3D::Vec2(0, 0);
    AlphaCutoff = 0.5f;
    UseAlphaClip = 0;
    Transmission = 0.0f;
    Ior = 1.50f;
    Thickness = 0.10f;
    AttenuationColor = Math3D::Vec3(1.0f, 1.0f, 1.0f);
    AttenuationDistance = 8.0f;
    ScatteringStrength = 0.0f;
    EnableWaveDisplacement = 0;
    WaveAmplitude = 0.0f;
    WaveFrequency = 1.0f;
    WaveSpeed = 0.75f;
    WaveChoppiness = 0.25f;
    WaveSecondaryScale = 1.0f;
    WaveDirection = Math3D::Vec2(0.85f, 0.45f);
    WaveTextureInfluence = 0.6f;
    WaveTextureSpeed = Math3D::Vec2(0.03f, 0.01f);
    ViewPos = Math3D::Vec3(0, 0, 0);

    BaseColorTex = nullptr;
    RoughnessTex = nullptr;
    MetallicRoughnessTex = nullptr;
    NormalTex = nullptr;
    HeightTex = nullptr;
    EmissiveTex = nullptr;
    OcclusionTex = nullptr;
    EnvMap = nullptr;

    set<Math3D::Vec4>("u_baseColor", Color::WHITE);
    set<Math3D::Vec4>("u_color", Color::WHITE);
    set<int>("u_bsdfModel", static_cast<int>(PBRBsdfModel::Standard));
    set<float>("u_metallic", 0.0f);
    set<float>("u_roughness", 1.0f);
    set<float>("u_normalScale", 1.0f);
    set<float>("u_heightScale", 0.02f);
    set<Math3D::Vec3>("u_emissiveColor", Math3D::Vec3(0, 0, 0));
    set<float>("u_emissiveStrength", 1.0f);
    set<float>("u_aoStrength", 1.0f);
    set<float>("u_envStrength", 1.0f);
    set<Math3D::Vec2>("u_uvScale", Math3D::Vec2(1, 1));
    set<Math3D::Vec2>("u_uvOffset", Math3D::Vec2(0, 0));
    set<float>("u_alphaCutoff", 0.5f);
    set<int>("u_useAlphaClip", 0);
    set<float>("u_transmission", 0.0f);
    set<float>("u_ior", 1.50f);
    set<float>("u_thickness", 0.10f);
    set<Math3D::Vec3>("u_attenuationColor", Math3D::Vec3(1.0f, 1.0f, 1.0f));
    set<float>("u_attenuationDistance", 8.0f);
    set<float>("u_scatteringStrength", 0.0f);
    set<int>("u_enableWaveDisplacement", 0);
    set<float>("u_waveAmplitude", 0.0f);
    set<float>("u_waveFrequency", 1.0f);
    set<float>("u_waveSpeed", 0.75f);
    set<float>("u_waveChoppiness", 0.25f);
    set<float>("u_waveSecondaryScale", 1.0f);
    set<Math3D::Vec2>("u_waveDirection", Math3D::Vec2(0.85f, 0.45f));
    set<float>("u_waveTextureInfluence", 0.6f);
    set<Math3D::Vec2>("u_waveTextureSpeed", Math3D::Vec2(0.03f, 0.01f));
    set<float>("u_time", 0.0f);
    set<GLUniformUpload::TextureSlot>("u_ssrColor", GLUniformUpload::TextureSlot(nullptr, 8));
    set<int>("u_useSceneColor", 0);
    set<GLUniformUpload::TextureSlot>("u_sceneDepth", GLUniformUpload::TextureSlot(nullptr, 9));
    set<int>("u_useSceneDepth", 0);
    set<int>("u_useSsr", 0);
    set<float>("u_ssrIntensity", 0.0f);
    set<float>("u_ssrMaxDistance", 80.0f);
    set<float>("u_ssrThickness", 0.18f);
    set<float>("u_ssrStride", 0.75f);
    set<float>("u_ssrJitter", 0.35f);
    set<int>("u_ssrMaxSteps", 56);
    set<float>("u_ssrRoughnessCutoff", 0.82f);
    set<float>("u_ssrEdgeFade", 0.18f);
    set<Math3D::Mat4>("u_invProjection", Math3D::Mat4());
    set<GLUniformUpload::TextureSlot>("u_planarReflectionTex", GLUniformUpload::TextureSlot(nullptr, 8));
    set<int>("u_usePlanarReflection", 0);
    set<Math3D::Mat4>("u_planarReflectionMatrix", Math3D::Mat4());
    set<float>("u_planarReflectionStrength", 1.0f);
    set<GLUniformUpload::CubeMapSlot>("u_localProbe", GLUniformUpload::CubeMapSlot(nullptr, LOCAL_PROBE_SLOT));
    set<int>("u_useLocalProbe", 0);
    set<Math3D::Vec3>("u_localProbeCenter", Math3D::Vec3(0.0f, 0.0f, 0.0f));
    set<Math3D::Vec3>("u_localProbeCaptureMin", Math3D::Vec3(0.0f, 0.0f, 0.0f));
    set<Math3D::Vec3>("u_localProbeCaptureMax", Math3D::Vec3(0.0f, 0.0f, 0.0f));
    set<Math3D::Vec3>("u_localProbeInfluenceMin", Math3D::Vec3(0.0f, 0.0f, 0.0f));
    set<Math3D::Vec3>("u_localProbeInfluenceMax", Math3D::Vec3(0.0f, 0.0f, 0.0f));
    set<int>("u_useBaseColorTex", 0);
    set<int>("u_useRoughnessTex", 0);
    set<int>("u_useMetallicRoughnessTex", 0);
    set<int>("u_useNormalTex", 0);
    set<int>("u_useHeightTex", 0);
    set<int>("u_useEmissiveTex", 0);
    set<int>("u_useOcclusionTex", 0);
    set<int>("u_useEnvMap", 0);
}

void PBRMaterial::bind(){
    if(Screen::GetCurrentCamera()){
        ViewPos = Screen::GetCurrentCamera()->transform().position;
    }else{
        ViewPos = Math3D::Vec3(0,0,0);
    }
    set<float>("u_time", getMaterialTimeSeconds());

    std::shared_ptr<CubeMap> activeEnvMap = EnvMap.get();
    if(!activeEnvMap){
        if(auto env = Screen::GetCurrentEnvironment()){
            if(auto skybox = env->getSkyBox()){
                activeEnvMap = skybox->getCubeMap();
            }
        }
    }

    const int useBoundEnvMap = (UseEnvMap.get() != 0 && activeEnvMap) ? 1 : 0;
    set<GLUniformUpload::CubeMapSlot>(
        "u_envMap",
        GLUniformUpload::CubeMapSlot(useBoundEnvMap ? activeEnvMap : nullptr, ENV_SLOT)
    );
    set<int>("u_useEnvMap", useBoundEnvMap);

    Material::bind();
    LightUniformUploader::UploadLights(this->getShader(), GetActiveLights());
    ShadowRenderer::BindShadowSamplers(this->getShader());
}

std::shared_ptr<PBRMaterial> PBRMaterial::Create(Math3D::Vec4 baseColor){
    auto program = compileShaderProgramSafe(
        "PBRMaterialLit_v3",
        "@assets/shader/Shader_Vert_Lit.vert",
        "@assets/shader/Shader_Frag_PBR.frag"
    );
    if(!program || program->getID() == 0){
        if(program){
            LogBot.Log(LOG_ERRO, "Failed to link PBRMaterialLit: \n%s", program->getLog().c_str());
        }

        // Fallback to lit color shader so the mesh still renders
        program = compileShaderProgramSafe(
            "PBRMaterialLit_fallback",
            "@assets/shader/Shader_Vert_Lit.vert",
            "@assets/shader/Shader_Frag_LitColor.frag"
        );
        if(program && program->getID() == 0){
            LogBot.Log(LOG_ERRO, "Failed to link PBRMaterialLit_fallback: \n%s", program->getLog().c_str());
        }else{
            LogBot.Log(LOG_WARN, "PBR shader failed. Using fallback lit shader for PBRMaterial.");
        }
    }

    if(!program){
        program = std::make_shared<ShaderProgram>();
    }

    auto material = std::make_shared<PBRMaterial>(program);
    material->BaseColor = baseColor;
    material->BsdfModel = static_cast<int>(PBRBsdfModel::Standard);
    material->Transmission = Math3D::Clamp(1.0f - baseColor.w, 0.0f, 1.0f);
    return material;
}

std::shared_ptr<PBRMaterial> PBRMaterial::CreateGlass(Math3D::Vec4 baseColor){
    auto material = Create(baseColor);
    if(!material){
        return nullptr;
    }

    material->BsdfModel = static_cast<int>(PBRBsdfModel::Glass);
    material->Metallic = 0.0f;
    material->Roughness = 0.02f;
    material->NormalScale = 1.0f;
    material->UseEnvMap = 1;
    material->EnvStrength = 1.15f;
    material->UseAlphaClip = 0;
    material->Transmission = 0.98f;
    material->Ior = 1.52f;
    material->Thickness = 0.12f;
    material->AttenuationColor = Math3D::Vec3(0.92f, 0.97f, 1.0f);
    material->AttenuationDistance = 0.25f;
    material->ScatteringStrength = 0.03f;
    material->EnableWaveDisplacement = 0;
    material->setCastsShadows(false);
    material->setReceivesShadows(false);
    return material;
}

std::shared_ptr<PBRMaterial> PBRMaterial::CreateWater(Math3D::Vec4 baseColor){
    auto material = Create(baseColor);
    if(!material){
        return nullptr;
    }

    material->BsdfModel = static_cast<int>(PBRBsdfModel::Water);
    material->Metallic = 0.0f;
    material->Roughness = 0.04f;
    material->NormalScale = 1.30f;
    material->UseEnvMap = 1;
    material->EnvStrength = 1.35f;
    material->UseAlphaClip = 0;
    material->Transmission = 0.96f;
    material->Ior = 1.333f;
    material->Thickness = 1.35f;
    material->AttenuationColor = Math3D::Vec3(0.18f, 0.46f, 0.78f);
    material->AttenuationDistance = 2.75f;
    material->ScatteringStrength = 0.62f;
    material->EnableWaveDisplacement = 1;
    material->WaveAmplitude = 0.06f;
    material->WaveFrequency = 1.65f;
    material->WaveSpeed = 0.92f;
    material->WaveChoppiness = 0.48f;
    material->WaveSecondaryScale = 1.55f;
    material->WaveDirection = Math3D::Vec2(0.92f, 0.39f);
    material->WaveTextureInfluence = 0.55f;
    material->WaveTextureSpeed = Math3D::Vec2(0.035f, 0.012f);
    material->setCastsShadows(false);
    material->setReceivesShadows(false);
    return material;
}
