#include "LightUtils.h"

#include <glad/glad.h>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include "Logbot.h"
#include "ShadowRenderer.h"
#include "Math.h"

namespace {
    constexpr GLuint LIGHT_UBO_BINDING = 0;

    struct alignas(16) LightUBO {
        float meta[4] = {0.0f, 0.0f, -1.0f, 1.0f};     // x=type, y=shadowType, z=shadowMapIndex, w=shadowStrength
        float position[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float direction[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float params[4] = {1.0f, 10.0f, 1.0f, 45.0f};  // intensity, range, falloff, spotAngle
        float shadow[4] = {0.0025f, 0.005f, 0.0f, 0.0f}; // bias, normalBias, unused
        float lightMatrix[16] = {
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1
        };
    };

    struct alignas(16) LightBlockUBO {
        float header[4] = {0.0f, 0.0f, 0.0f, 0.0f};    // x=lightCount
        LightUBO lights[MAX_LIGHTS];
    };

    GLuint g_lightUbo = 0;
    std::unordered_set<GLuint> g_boundPrograms;
    std::unordered_set<GLuint> g_loggedPrograms;
    std::unordered_map<GLuint, int> g_lastLightCount;
    bool g_hasLastBlock = false;
    LightBlockUBO g_lastBlock;

    void ensureLightUboCreated() {
        if(g_lightUbo != 0){
            return;
        }

        glGenBuffers(1, &g_lightUbo);
        glBindBuffer(GL_UNIFORM_BUFFER, g_lightUbo);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(LightBlockUBO), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferRange(GL_UNIFORM_BUFFER, LIGHT_UBO_BINDING, g_lightUbo, 0, sizeof(LightBlockUBO));
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void ensureProgramBoundToLightBlock(GLuint programId) {
        if(programId == 0){
            return;
        }

        if(g_boundPrograms.find(programId) != g_boundPrograms.end()){
            return;
        }

        GLuint blockIndex = glGetUniformBlockIndex(programId, "LightBlock");
        if(blockIndex != GL_INVALID_INDEX){
            glUniformBlockBinding(programId, blockIndex, LIGHT_UBO_BINDING);
        }

        if(g_loggedPrograms.find(programId) == g_loggedPrograms.end()){
            if(blockIndex == GL_INVALID_INDEX){
                LogBot.Log(LOG_ERRO, "LightBlock uniform block not found for program %u", programId);
            }else{
                GLint blockSize = 0;
                GLint activeUniforms = 0;
                glGetActiveUniformBlockiv(programId, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
                glGetActiveUniformBlockiv(programId, blockIndex, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &activeUniforms);
                LogBot.Log(LOG_INFO, "LightBlock bound for program %u (size=%d bytes, uniforms=%d)", programId, blockSize, activeUniforms);
            }
            g_loggedPrograms.insert(programId);
        }

        g_boundPrograms.insert(programId);
    }
}

void LightUniformUploader::UploadLights(std::shared_ptr<ShaderProgram> program, const std::vector<Light>& lights) {
    if(!program || program->getID() == 0){
        return;
    }

    ensureLightUboCreated();
    ensureProgramBoundToLightBlock(program->getID());

    LightBlockUBO block;
    std::memset(&block, 0, sizeof(LightBlockUBO));
    int lightCount = static_cast<int>(lights.size());
    if(lightCount > MAX_LIGHTS) lightCount = MAX_LIGHTS;
    block.header[0] = static_cast<float>(lightCount);

    auto lastIt = g_lastLightCount.find(program->getID());
    if(lastIt == g_lastLightCount.end() || lastIt->second != lightCount){
        int firstType = (lightCount > 0) ? static_cast<int>(lights[0].type) : -1;
        LogBot.Log(LOG_INFO, "LightUBO upload: program %u count=%d firstType=%d", program->getID(), lightCount, firstType);
        g_lastLightCount[program->getID()] = lightCount;
    }

    for(int i = 0; i < lightCount; ++i){
        const Light& src = lights[i];
        LightUBO& dst = block.lights[i];

        ShadowLightData shadowData;
        ShadowRenderer::GetShadowDataForLight(i, src, shadowData);

        dst.meta[0] = static_cast<float>(static_cast<int>(src.type));
        dst.meta[1] = static_cast<float>(static_cast<int>(shadowData.shadowType));
        dst.meta[2] = static_cast<float>(shadowData.shadowMapIndex);
        dst.meta[3] = shadowData.shadowStrength;
        dst.position[0] = src.position.x;
        dst.position[1] = src.position.y;
        dst.position[2] = src.position.z;
        dst.position[3] = 0.0f;

        dst.direction[0] = src.direction.x;
        dst.direction[1] = src.direction.y;
        dst.direction[2] = src.direction.z;
        dst.direction[3] = 0.0f;

        dst.color[0] = src.color.x;
        dst.color[1] = src.color.y;
        dst.color[2] = src.color.z;
        dst.color[3] = src.color.w;

        dst.params[0] = src.intensity;
        dst.params[1] = src.range;
        dst.params[2] = src.falloff;
        dst.params[3] = src.spotAngle;

        dst.shadow[0] = shadowData.shadowBias;
        dst.shadow[1] = shadowData.shadowNormalBias;

        const float* m = glm::value_ptr(shadowData.lightMatrix.data);
        for(int k = 0; k < 16; ++k){
            dst.lightMatrix[k] = m[k];
        }
    }

    if(!g_hasLastBlock || std::memcmp(&block, &g_lastBlock, sizeof(LightBlockUBO)) != 0){
        glBindBuffer(GL_UNIFORM_BUFFER, g_lightUbo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightBlockUBO), &block);
        glBindBufferRange(GL_UNIFORM_BUFFER, LIGHT_UBO_BINDING, g_lightUbo, 0, sizeof(LightBlockUBO));
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        g_lastBlock = block;
        g_hasLastBlock = true;
    }
}
