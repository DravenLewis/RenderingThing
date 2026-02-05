#include "LightUtils.h"

#include <glad/glad.h>
#include <unordered_map>

void LightUniformUploader::UploadLights(std::shared_ptr<ShaderProgram> program, const std::vector<Light>& lights) {
    if(!program || program->getID() == 0){
        return;
    }

    int lightCount = static_cast<int>(lights.size());
    if (lightCount > MAX_LIGHTS) lightCount = MAX_LIGHTS;

    struct LightUniforms {
        GLint typeLoc = -1;
        GLint posLoc = -1;
        GLint dirLoc = -1;
        GLint colorLoc = -1;
        GLint intensityLoc = -1;
        GLint rangeLoc = -1;
        GLint falloffLoc = -1;
        GLint angleLoc = -1;
    };

    struct CachedLightUniforms {
        GLint countLoc = -1;
        std::vector<LightUniforms> lights;
    };

    static std::unordered_map<GLuint, CachedLightUniforms> cache;

    const GLuint programId = program->getID();
    auto it = cache.find(programId);
    if(it == cache.end()){
        CachedLightUniforms cached;
        cached.countLoc = glGetUniformLocation(programId, "u_lightCount");
        cached.lights.resize(MAX_LIGHTS);

        for(int i = 0; i < MAX_LIGHTS; ++i){
            std::string uniformBase = "u_lights[" + std::to_string(i) + "]";
            cached.lights[i].typeLoc = glGetUniformLocation(programId, (uniformBase + ".type").c_str());
            cached.lights[i].posLoc = glGetUniformLocation(programId, (uniformBase + ".position").c_str());
            cached.lights[i].dirLoc = glGetUniformLocation(programId, (uniformBase + ".direction").c_str());
            cached.lights[i].colorLoc = glGetUniformLocation(programId, (uniformBase + ".color").c_str());
            cached.lights[i].intensityLoc = glGetUniformLocation(programId, (uniformBase + ".intensity").c_str());
            cached.lights[i].rangeLoc = glGetUniformLocation(programId, (uniformBase + ".range").c_str());
            cached.lights[i].falloffLoc = glGetUniformLocation(programId, (uniformBase + ".falloff").c_str());
            cached.lights[i].angleLoc = glGetUniformLocation(programId, (uniformBase + ".spotAngle").c_str());
        }

        it = cache.emplace(programId, std::move(cached)).first;
    }

    if (it->second.countLoc != -1) {
        glUniform1i(it->second.countLoc, lightCount);
    }

    for (int i = 0; i < lightCount; ++i) {
        const Light& light = lights[i];
        const LightUniforms& u = it->second.lights[i];

        if (u.typeLoc != -1) glUniform1i(u.typeLoc, static_cast<int>(light.type));
        if (u.posLoc != -1) glUniform3f(u.posLoc, light.position.x, light.position.y, light.position.z);
        if (u.dirLoc != -1) glUniform3f(u.dirLoc, light.direction.x, light.direction.y, light.direction.z);
        if (u.colorLoc != -1) glUniform4f(u.colorLoc, light.color.x, light.color.y, light.color.z, light.color.w);
        if (u.intensityLoc != -1) glUniform1f(u.intensityLoc, light.intensity);
        if (u.rangeLoc != -1) glUniform1f(u.rangeLoc, light.range);
        if (u.falloffLoc != -1) glUniform1f(u.falloffLoc, light.falloff);
        if (u.angleLoc != -1) glUniform1f(u.angleLoc, light.spotAngle);
    }
}
