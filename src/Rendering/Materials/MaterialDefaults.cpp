/**
 * @file src/Rendering/Materials/MaterialDefaults.cpp
 * @brief Implementation for MaterialDefaults.
 */


#include "Rendering/Materials/MaterialDefaults.h"
#include "Rendering/Core/Screen.h"
#include "Foundation/Logging/Logbot.h"
#include "Rendering/Lighting/ShadowRenderer.h"

using namespace MaterialDefaults;

namespace {
    const std::vector<Light>& GetActiveLights(){
        auto env = Screen::GetCurrentEnvironment();
        if(env){
            return env->getLightsForUpload();
        }
        static const std::vector<Light> EMPTY;
        return EMPTY;
    }

    std::shared_ptr<ShaderProgram> compileMaterialProgram(const char* cacheName, const char* vertexAssetRef, const char* fragmentAssetRef){
        auto vertexShader = AssetManager::Instance.getOrLoad(vertexAssetRef);
        auto fragmentShader = AssetManager::Instance.getOrLoad(fragmentAssetRef);

        if(!vertexShader || !fragmentShader){
            LogBot.Log(
                LOG_ERRO,
                "[MaterialDefaults] Missing shader asset(s) for '%s' (vert='%s', frag='%s').",
                cacheName ? cacheName : "<unnamed>",
                vertexAssetRef ? vertexAssetRef : "<null>",
                fragmentAssetRef ? fragmentAssetRef : "<null>"
            );
            return nullptr;
        }

        return ShaderCacheManager::INSTANCE.getOrCompile(
            cacheName ? cacheName : "MaterialProgram",
            vertexShader->asString(),
            fragmentShader->asString()
        );
    }
}

#pragma region ColorMaterial
ColorMaterial::ColorMaterial(std::shared_ptr<ShaderProgram> program) : Material(program) {
    Color.onChange([this](Math3D::Vec4 oldValue,Math3D::Vec4 newValue) -> bool{
        set<Math3D::Vec4>("u_color",newValue);
        return true;
    });

    Color = Color::CLEAR;
    set<Math3D::Vec4>("u_color", Color::CLEAR);
};

std::shared_ptr<ColorMaterial> ColorMaterial::Create(Math3D::Vec4 color){
    auto program = compileMaterialProgram(
        "ColorShaderUnlit",
        "@assets/shader/Shader_Vert_Default.vert",
        "@assets/shader/Shader_Frag_ColorShader.frag"
    );
    if(!program){
        program = std::make_shared<ShaderProgram>();
    }
    if(program && program->getID() == 0){
        LogBot.Log(LOG_ERRO, "Failed to link ColorShaderUnlit: \n%s", program->getLog().c_str());
    }
    //program->setVertexShader(vertexShader->asString());
    //program->setFragmentShader(fragmentShader->asString());
    auto material = std::make_shared<ColorMaterial>(program);
    material->Color = color;

    return material;
};
#pragma endregion

#pragma region ImageMaterial
ImageMaterial::ImageMaterial(std::shared_ptr<ShaderProgram> program, std::shared_ptr<Texture> texture) : Material(program) {
 
    Color.onChange([this](Math3D::Vec4 oldValue,Math3D::Vec4 newValue) -> bool{
        set<Math3D::Vec4>("u_color",newValue);
        return true;
    });
    
    Tex.onChange([this]( std::shared_ptr<Texture> oldValue,std::shared_ptr<Texture> newValue) -> bool{
        set<std::shared_ptr<Texture>>("u_texture", newValue);
        return true;
    });

    UV.onChange([this](Math3D::Vec2 oldValue,Math3D::Vec2 newValue) -> bool{
        set<Math3D::Vec2>("u_uv", newValue);
        return true;
    });

    Color = Color::WHITE;
    Tex = texture;
    UV = Math3D::Vec2(0,0);
                
    // Ensure uniforms are set even if onChange doesn't fire
    set<Math3D::Vec4>("u_color", Color::WHITE);
    set<std::shared_ptr<Texture>>("u_texture", texture);
    set<Math3D::Vec2>("u_uv", Math3D::Vec2(0,0));
};

std::shared_ptr<ImageMaterial> ImageMaterial::Create(PTexture tex, Math3D::Vec4 color, Math3D::Vec2 uv){

    if(!tex){
        LogBot.Log(LOG_ERRO,"Cannot Create Material with Null Texture.");
        return nullptr;
    }

    auto program = compileMaterialProgram(
        "ImageShaderUnlit",
        "@assets/shader/Shader_Vert_Default.vert",
        "@assets/shader/Shader_Frag_ImageShader.frag"
    );
    if(!program){
        program = std::make_shared<ShaderProgram>();
    }
    if(program && program->getID() == 0){
        LogBot.Log(LOG_ERRO, "Failed to link ImageShaderUnlit: \n%s", program->getLog().c_str());
    }
    //program->setVertexShader(vertexShader->asString());
    //program->setFragmentShader(fragmentShader->asString());

    auto material = std::make_shared<ImageMaterial>(program, tex);
    material->Color = color;
    material->Tex = tex;
    material->UV = uv;

    return material;
};
#pragma endregion

#pragma region LitColorMaterial
LitColorMaterial::LitColorMaterial(std::shared_ptr<ShaderProgram> program) : Material(program) {

    Color.onChange([this](Math3D::Vec4 oldValue,Math3D::Vec4 newValue) -> bool{
        set<Math3D::Vec4>("u_color",newValue);
        return true;
    });

    ViewPos.onChange([this](Math3D::Vec3 oldValue,Math3D::Vec3 newValue) -> bool{
        set<Math3D::Vec3>("u_viewPos",newValue);
        return true;
    });

    Color = Color::WHITE;
    ViewPos = Math3D::Vec3(0, 0, 6);
    set<Math3D::Vec4>("u_color", Color::WHITE);
    set<Math3D::Vec3>("u_viewPos", Math3D::Vec3(0, 0, 0));
};

void LitColorMaterial::bind(){

    if(Screen::GetCurrentCamera()){
        ViewPos = Screen::GetCurrentCamera()->transform().position;
    }else{
        ViewPos = Math3D::Vec3(0,0,0);
    }

    Material::bind();
    // Upload lights from global light manager
    LightUniformUploader::UploadLights(this->getShader(), GetActiveLights());
    ShadowRenderer::BindShadowSamplers(this->getShader());
}

std::shared_ptr<LitColorMaterial> LitColorMaterial::Create(Math3D::Vec4 color){
    auto program = compileMaterialProgram(
        "ColorMaterialLit_UBO",
        "@assets/shader/Shader_Vert_Lit.vert",
        "@assets/shader/Shader_Frag_LitColor.frag"
    );
    if(!program){
        program = std::make_shared<ShaderProgram>();
    }
    if(program && program->getID() == 0){
        LogBot.Log(LOG_ERRO, "Failed to link ColorMaterialLit_UBO: \n%s", program->getLog().c_str());
    }
    //program->setVertexShader(vertexShader->asString());
    //program->setFragmentShader(fragmentShader->asString());
    auto material = std::make_shared<LitColorMaterial>(program);
    material->Color = color;

    return material;
};
#pragma endregion

#pragma region LitImageMaterial
LitImageMaterial::LitImageMaterial(std::shared_ptr<ShaderProgram> program, std::shared_ptr<Texture> texture) : Material(program) {

    Color.onChange([this](Math3D::Vec4 oldValue,Math3D::Vec4 newValue) -> bool{
        set<Math3D::Vec4>("u_color",newValue);
        return true;
    });
                
    Tex.onChange([this]( std::shared_ptr<Texture> oldValue,std::shared_ptr<Texture> newValue) -> bool{
        set<std::shared_ptr<Texture>>("u_texture", newValue);
        return true;
    });

    ViewPos.onChange([this](Math3D::Vec3 oldValue,Math3D::Vec3 newValue) -> bool{
        set<Math3D::Vec3>("u_viewPos",newValue);
        return true;
    });

    Color = Color::WHITE;
    Tex = texture;
    ViewPos = Math3D::Vec3(0, 0, 6);
                
    set<Math3D::Vec4>("u_color", Color::WHITE);
    set<std::shared_ptr<Texture>>("u_texture", texture);
    set<Math3D::Vec3>("u_viewPos", Math3D::Vec3(0, 0, 0));
};

void LitImageMaterial::bind(){

    if(Screen::GetCurrentCamera()){
        ViewPos = Screen::GetCurrentCamera()->transform().position;
    }else{
        ViewPos = Math3D::Vec3(0,0,0);
    }

    Material::bind();
    // Upload lights from global light manager
    LightUniformUploader::UploadLights(this->getShader(), GetActiveLights());
    ShadowRenderer::BindShadowSamplers(this->getShader());
}

std::shared_ptr<LitImageMaterial> LitImageMaterial::Create(PTexture tex, Math3D::Vec4 color){

    if(!tex){
        LogBot.Log(LOG_ERRO,"Cannot Create Material with Null Texture.");
        return nullptr;
    }

    auto program = compileMaterialProgram(
        "ImageShaderLit_UBO",
        "@assets/shader/Shader_Vert_Lit.vert",
        "@assets/shader/Shader_Frag_LitImage.frag"
    );
    if(!program){
        program = std::make_shared<ShaderProgram>();
    }
    if(program && program->getID() == 0){
        LogBot.Log(LOG_ERRO, "Failed to link ImageShaderLit_UBO: \n%s", program->getLog().c_str());
    }
    //program->setVertexShader(vertexShader->asString());
    //program->setFragmentShader(fragmentShader->asString());

    auto material = std::make_shared<LitImageMaterial>(program, tex);
    material->Color = color;
    material->Tex = tex;

    return material;
};
#pragma endregion

#pragma region FlatColorMaterial
FlatColorMaterial::FlatColorMaterial(std::shared_ptr<ShaderProgram> program) : Material(program) {
            
    Color.onChange([this](Math3D::Vec4 oldValue, Math3D::Vec4 newValue) -> bool {
        set<Math3D::Vec4>("u_color", newValue);
        return true;
    });

    ViewPos.onChange([this](Math3D::Vec3 oldValue, Math3D::Vec3 newValue) -> bool {
        set<Math3D::Vec3>("u_viewPos", newValue);
        return true;
    });

    Color = Color::WHITE;
    ViewPos = Math3D::Vec3(0, 0, 6);
            
    set<Math3D::Vec4>("u_color", Color::WHITE);
    set<Math3D::Vec3>("u_viewPos", Math3D::Vec3(0, 0, 0));
};

void FlatColorMaterial::bind(){

    if(Screen::GetCurrentCamera()){
        ViewPos = Screen::GetCurrentCamera()->transform().position;
    }else{
        ViewPos = Math3D::Vec3(0,0,0);
    }

    Material::bind();
    // Important: Use global lights
    LightUniformUploader::UploadLights(this->getShader(), GetActiveLights());
    ShadowRenderer::BindShadowSamplers(this->getShader());
}

std::shared_ptr<FlatColorMaterial> FlatColorMaterial::Create(Math3D::Vec4 color) {
    auto program = compileMaterialProgram(
        "ColorShaderLitFlat_UBO",
        "@assets/shader/Shader_Vert_Lit.vert",
        "@assets/shader/Shader_Frag_FlatColor.frag"
    );
    if(!program){
        program = std::make_shared<ShaderProgram>();
    }
    if(program && program->getID() == 0){
        LogBot.Log(LOG_ERRO, "Failed to link ColorShaderLitFlat_UBO: \n%s", program->getLog().c_str());
    }
    //program->setVertexShader(vertexShader->asString());
    //program->setFragmentShader(fragmentShader->asString());
            
    auto material = std::make_shared<FlatColorMaterial>(program);
    material->Color = color;

    return material;
};
#pragma endregion

#pragma region FlatImageMaterial
FlatImageMaterial::FlatImageMaterial(std::shared_ptr<ShaderProgram> program, std::shared_ptr<Texture> texture) : Material(program) {

    Color.onChange([this](Math3D::Vec4 oldValue, Math3D::Vec4 newValue) -> bool {
        set<Math3D::Vec4>("u_color", newValue);
        return true;
    });
                
    Tex.onChange([this](std::shared_ptr<Texture> oldValue, std::shared_ptr<Texture> newValue) -> bool {
        set<std::shared_ptr<Texture>>("u_texture", newValue);
        return true;
    });

    ViewPos.onChange([this](Math3D::Vec3 oldValue, Math3D::Vec3 newValue) -> bool {
        set<Math3D::Vec3>("u_viewPos", newValue);
        return true;
    });

    Color = Color::WHITE;
    Tex = texture;
    ViewPos = Math3D::Vec3(0, 0, 6);
                
    set<Math3D::Vec4>("u_color", Color::WHITE);
    set<std::shared_ptr<Texture>>("u_texture", texture);
    set<Math3D::Vec3>("u_viewPos", Math3D::Vec3(0, 0, 6));
};

void FlatImageMaterial::bind(){

    if(Screen::GetCurrentCamera()){
        ViewPos = Screen::GetCurrentCamera()->transform().position;
    }else{
        ViewPos = Math3D::Vec3(0,0,0);
    }

    Material::bind();
    LightUniformUploader::UploadLights(this->getShader(), GetActiveLights());
    ShadowRenderer::BindShadowSamplers(this->getShader());
}

std::shared_ptr<FlatImageMaterial> FlatImageMaterial::Create(std::shared_ptr<Texture> tex, Math3D::Vec4 color) {

    if(!tex){
        LogBot.Log(LOG_ERRO, "Cannot Create FlatImageMaterial with Null Texture.");
        return nullptr;
    }

    auto program = compileMaterialProgram(
        "ImageShaderLitFlat_UBO",
        "@assets/shader/Shader_Vert_Lit.vert",
        "@assets/shader/Shader_Frag_FlatImage.frag"
    );
    if(!program){
        program = std::make_shared<ShaderProgram>();
    }
    if(program && program->getID() == 0){
        LogBot.Log(LOG_ERRO, "Failed to link ImageShaderLitFlat_UBO: \n%s", program->getLog().c_str());
    }
    //program->setVertexShader(vertexShader->asString());
    //program->setFragmentShader(fragmentShader->asString());

    auto material = std::make_shared<FlatImageMaterial>(program, tex);
    material->Color = color;
    material->Tex = tex;

    return material;
};

#pragma endregion
