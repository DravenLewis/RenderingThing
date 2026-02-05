#include "SkyboxMaterial.h"

#include "Asset.h"
#include "Logbot.h"
#include <glad/glad.h>

SkyboxMaterial::SkyboxMaterial(std::shared_ptr<ShaderProgram> program) : Material(program) {
    EnvMap.onChange([this](std::shared_ptr<CubeMap>, std::shared_ptr<CubeMap> newValue) -> bool{
        set<GLUniformUpload::CubeMapSlot>("u_envMap", GLUniformUpload::CubeMapSlot(newValue, 0));
        return true;
    });
}

void SkyboxMaterial::bind(){
    auto program = getShader();
    if(!program) return;

    program->bind();

    auto map = EnvMap.get();
    if(map){
        map->bind(0);
        GLint loc = glGetUniformLocation(program->getID(), "u_envMap");
        if(loc != -1){
            glUniform1i(loc, 0);
        }
    }

    Material::bind();
}

std::shared_ptr<SkyboxMaterial> SkyboxMaterial::Create(std::shared_ptr<CubeMap> envMap){
    std::shared_ptr<Asset> vertexShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Vert_Skybox.vert"));
    std::shared_ptr<Asset> fragmentShader = (AssetManager::Instance.getOrLoad("@assets/shader/Shader_Frag_Skybox.frag"));

    auto program = ShaderCacheManager::INSTANCE.getOrCompile("SkyboxShader_v2", vertexShader->asString(), fragmentShader->asString());
    if(program && program->getID() == 0){
        LogBot.Log(LOG_ERRO, "Failed to link SkyboxShader: \n%s", program->getLog().c_str());
    }

    auto material = std::make_shared<SkyboxMaterial>(program);
    if(!envMap){
        LogBot.Log(LOG_WARN, "SkyboxMaterial created with null cubemap. Skybox will render black.");
    }
    material->EnvMap = envMap;
    return material;
}
