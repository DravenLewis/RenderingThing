
#include "MaterialDefaults.h"
#include "Screen.h"

using namespace MaterialDefaults;

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
    std::shared_ptr<Asset> vertexShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Default.vert"));
    std::shared_ptr<Asset> fragmentShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_ColorShader.frag"));
 
    auto program = ShaderCacheManager::INSTANCE.getOrCompile("ColorShaderUnlit", vertexShader->asString(), fragmentShader->asString()); //std::make_shared<ShaderProgram>();
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

    std::shared_ptr<Asset> vertexShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Default.vert"));
    std::shared_ptr<Asset> fragmentShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_ImageShader.frag"));
 
    auto program = ShaderCacheManager::INSTANCE.getOrCompile("ImageShaderUnlit", vertexShader->asString(), fragmentShader->asString()); //std::make_shared<ShaderProgram>();
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
    LightUniformUploader::UploadLights(this->getShader(), LightManager::GlobalLightManager.getAllLights());
}

std::shared_ptr<LitColorMaterial> LitColorMaterial::Create(Math3D::Vec4 color){
    std::shared_ptr<Asset> vertexShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Lit.vert"));
    std::shared_ptr<Asset> fragmentShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_LitColor.frag"));
 
    auto program = ShaderCacheManager::INSTANCE.getOrCompile("ColorMaterialLit", vertexShader->asString(), fragmentShader->asString());//std::make_shared<ShaderProgram>();
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
    LightUniformUploader::UploadLights(this->getShader(), LightManager::GlobalLightManager.getAllLights());
}

std::shared_ptr<LitImageMaterial> LitImageMaterial::Create(PTexture tex, Math3D::Vec4 color){

    if(!tex){
        LogBot.Log(LOG_ERRO,"Cannot Create Material with Null Texture.");
        return nullptr;
    }

    std::shared_ptr<Asset> vertexShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Lit.vert"));
    std::shared_ptr<Asset> fragmentShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_LitImage.frag"));
 
    auto program = ShaderCacheManager::INSTANCE.getOrCompile("ImageShaderLit", vertexShader->asString(), fragmentShader->asString()); //std::make_shared<ShaderProgram>();
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
    LightUniformUploader::UploadLights(this->getShader(), LightManager::GlobalLightManager.getAllLights());
}

std::shared_ptr<FlatColorMaterial> FlatColorMaterial::Create(Math3D::Vec4 color) {
    // Use the standard Lit Vertex shader
    std::shared_ptr<Asset> vertexShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Lit.vert"));
    // Use our new Flat Fragment shader
    std::shared_ptr<Asset> fragmentShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_FlatColor.frag"));

    auto program = ShaderCacheManager::INSTANCE.getOrCompile("ColorShaderLitFlat", vertexShader->asString(), fragmentShader->asString());//std::make_shared<ShaderProgram>();
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
    LightUniformUploader::UploadLights(this->getShader(), LightManager::GlobalLightManager.getAllLights());
}

std::shared_ptr<FlatImageMaterial> FlatImageMaterial::Create(std::shared_ptr<Texture> tex, Math3D::Vec4 color) {

    if(!tex){
        LogBot.Log(LOG_ERRO, "Cannot Create FlatImageMaterial with Null Texture.");
        return nullptr;
    }

    // Re-use standard Lit Vertex shader
    std::shared_ptr<Asset> vertexShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Lit.vert"));
    // Use new Flat Image Fragment shader
    std::shared_ptr<Asset> fragmentShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_FlatImage.frag"));

    auto program = ShaderCacheManager::INSTANCE.getOrCompile("ImageShaderLitFlat", vertexShader->asString(), fragmentShader->asString()); //std::make_shared<ShaderProgram>();
    //program->setVertexShader(vertexShader->asString());
    //program->setFragmentShader(fragmentShader->asString());

    auto material = std::make_shared<FlatImageMaterial>(program, tex);
    material->Color = color;
    material->Tex = tex;

    return material;
};

#pragma endregion