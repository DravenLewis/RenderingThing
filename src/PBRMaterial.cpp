#include "PBRMaterial.h"

#include "Asset.h"
#include "Logbot.h"
#include "Screen.h"
#include "LightUtils.h"
#include "ShadowRenderer.h"

namespace {
    constexpr int BASE_COLOR_SLOT = 0;
    constexpr int METAL_ROUGH_SLOT = 1;
    constexpr int NORMAL_SLOT = 2;
    constexpr int EMISSIVE_SLOT = 3;
    constexpr int OCCLUSION_SLOT = 4;
    constexpr int ENV_SLOT = 5;

    const std::vector<Light>& GetActiveLights(){
        auto env = Screen::GetCurrentEnvironment();
        if(env){
            return env->getLightsForUpload();
        }
        static const std::vector<Light> EMPTY;
        return EMPTY;
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

    Metallic.onChange([this](float, float newValue) -> bool{
        set<float>("u_metallic", newValue);
        return true;
    });

    Roughness.onChange([this](float, float newValue) -> bool{
        set<float>("u_roughness", newValue);
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

    ViewPos.onChange([this](Math3D::Vec3, Math3D::Vec3 newValue) -> bool{
        set<Math3D::Vec3>("u_viewPos", newValue);
        return true;
    });

    BaseColor = Color::WHITE;
    Metallic = 0.0f;
    Roughness = 1.0f;
    NormalScale = 1.0f;
    EmissiveColor = Math3D::Vec3(0, 0, 0);
    EmissiveStrength = 1.0f;
    OcclusionStrength = 1.0f;
    EnvStrength = 1.0f;
    UseEnvMap = 0;
    UVScale = Math3D::Vec2(1, 1);
    UVOffset = Math3D::Vec2(0, 0);
    AlphaCutoff = 0.5f;
    UseAlphaClip = 0;
    ViewPos = Math3D::Vec3(0, 0, 0);

    BaseColorTex = nullptr;
    MetallicRoughnessTex = nullptr;
    NormalTex = nullptr;
    EmissiveTex = nullptr;
    OcclusionTex = nullptr;
    EnvMap = nullptr;

    set<Math3D::Vec4>("u_baseColor", Color::WHITE);
    set<Math3D::Vec4>("u_color", Color::WHITE);
    set<float>("u_metallic", 0.0f);
    set<float>("u_roughness", 1.0f);
    set<float>("u_normalScale", 1.0f);
    set<Math3D::Vec3>("u_emissiveColor", Math3D::Vec3(0, 0, 0));
    set<float>("u_emissiveStrength", 1.0f);
    set<float>("u_aoStrength", 1.0f);
    set<float>("u_envStrength", 1.0f);
    set<Math3D::Vec2>("u_uvScale", Math3D::Vec2(1, 1));
    set<Math3D::Vec2>("u_uvOffset", Math3D::Vec2(0, 0));
    set<float>("u_alphaCutoff", 0.5f);
    set<int>("u_useAlphaClip", 0);
    set<int>("u_useBaseColorTex", 0);
    set<int>("u_useMetallicRoughnessTex", 0);
    set<int>("u_useNormalTex", 0);
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

    Material::bind();
    LightUniformUploader::UploadLights(this->getShader(), GetActiveLights());
    ShadowRenderer::BindShadowSamplers(this->getShader());
}

std::shared_ptr<PBRMaterial> PBRMaterial::Create(Math3D::Vec4 baseColor){
    std::shared_ptr<Asset> vertexShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Lit.vert"));
    std::shared_ptr<Asset> fragmentShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_PBR.frag"));

    auto program = ShaderCacheManager::INSTANCE.getOrCompile("PBRMaterialLit_v2", vertexShader->asString(), fragmentShader->asString());
    if(!program || program->getID() == 0){
        if(program){
            LogBot.Log(LOG_ERRO, "Failed to link PBRMaterialLit: \n%s", program->getLog().c_str());
        }

        // Fallback to lit color shader so the mesh still renders
        std::shared_ptr<Asset> fallbackFrag = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_LitColor.frag"));
        program = ShaderCacheManager::INSTANCE.getOrCompile("PBRMaterialLit_fallback", vertexShader->asString(), fallbackFrag->asString());
        if(program && program->getID() == 0){
            LogBot.Log(LOG_ERRO, "Failed to link PBRMaterialLit_fallback: \n%s", program->getLog().c_str());
        }else{
            LogBot.Log(LOG_WARN, "PBR shader failed. Using fallback lit shader for PBRMaterial.");
        }
    }

    auto material = std::make_shared<PBRMaterial>(program);
    material->BaseColor = baseColor;
    return material;
}
