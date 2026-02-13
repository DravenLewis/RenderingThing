#include "LightUtils.h"

#include <glad/glad.h>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include "Logbot.h"
#include "ShadowRenderer.h"
#include "Math.h"
#include <cmath>

namespace {
    constexpr GLuint LIGHT_UBO_BINDING = 0;

    struct alignas(16) LightUBO {
        float meta[4] = {0.0f, 0.0f, -1.0f, 1.0f};     // x=type, y=shadowType, z=shadowMapIndex, w=shadowStrength
        float position[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float direction[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float params[4] = {1.0f, 10.0f, 1.0f, 45.0f};  // intensity, range, falloff, spotAngle
        float shadow[4] = {0.0025f, 0.005f, 1.0f, 0.0f}; // bias, normalBias, cascadeCount, unused
        float cascadeSplits[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float lightMatrices[16 * 4] = {
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1,
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1,
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1,
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
    uint64_t g_lightUploadFrame = 0;

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

    g_lightUploadFrame++;

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

        auto safeFloat = [](float v, float fallback){
            return std::isfinite(v) ? v : fallback;
        };
        auto safeVec3 = [&](const Math3D::Vec3& v, const Math3D::Vec3& fallback){
            if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)){
                return fallback;
            }
            return v;
        };
        auto safeVec4 = [&](const Math3D::Vec4& v, const Math3D::Vec4& fallback){
            if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.w)){
                return fallback;
            }
            return v;
        };

        Math3D::Vec3 safePos = safeVec3(src.position, Math3D::Vec3(0,0,0));
        Math3D::Vec3 safeDir = safeVec3(src.direction, Math3D::Vec3(0,-1,0));
        if(safeDir.length() < Math3D::EPSILON){
            safeDir = Math3D::Vec3(0,-1,0);
        }else{
            safeDir = safeDir.normalize();
        }
        Math3D::Vec4 safeColor = safeVec4(src.color, Math3D::Vec4(1,1,1,1));

        float safeIntensity = Math3D::Max(0.0f, safeFloat(src.intensity, 0.0f));
        float safeRange = Math3D::Max(0.1f, safeFloat(src.range, 0.1f));
        float safeFalloff = Math3D::Clamp(safeFloat(src.falloff, 2.0f), 0.1f, 3.0f);
        float safeSpotAngle = Math3D::Clamp(safeFloat(src.spotAngle, 45.0f), 1.0f, 170.0f);
        float safeShadowStrength = Math3D::Clamp(safeFloat(src.shadowStrength, 1.0f), 0.0f, 1.0f);

        ShadowLightData shadowData;
        ShadowRenderer::GetShadowDataForLight(i, src, shadowData);
        bool shadowDataValid = std::isfinite(shadowData.shadowBias) &&
                               std::isfinite(shadowData.shadowNormalBias);
        for(int m = 0; m < 4 && shadowDataValid; ++m){
            const float* matPtr = glm::value_ptr(shadowData.lightMatrices[m].data);
            for(int k = 0; k < 16; ++k){
                if(!std::isfinite(matPtr[k])){
                    shadowDataValid = false;
                    break;
                }
            }
        }
        if(!shadowDataValid){
            shadowData.shadowMapIndex = -1;
            shadowData.shadowStrength = 0.0f;
        }
        bool sanitized = (!std::isfinite(src.position.x) || !std::isfinite(src.position.y) || !std::isfinite(src.position.z)) ||
                         (!std::isfinite(src.direction.x) || !std::isfinite(src.direction.y) || !std::isfinite(src.direction.z)) ||
                         (!std::isfinite(src.color.x) || !std::isfinite(src.color.y) || !std::isfinite(src.color.z) || !std::isfinite(src.color.w)) ||
                         !std::isfinite(src.intensity) || !std::isfinite(src.range) || !std::isfinite(src.falloff) || !std::isfinite(src.spotAngle) ||
                         (safeIntensity != src.intensity) || (safeRange != src.range) || (safeFalloff != src.falloff) || (safeSpotAngle != src.spotAngle);
        if(sanitized || !shadowDataValid){
            LogBot.Log(LOG_WARN,
                "[LightUBO] frame=%llu program=%u idx=%d type=%d casts=%d sanitized=%d shadowValid=%d "
                "pos=(%.3f,%.3f,%.3f) dir=(%.3f,%.3f,%.3f) intensity=%.3f range=%.3f falloff=%.3f spot=%.3f shadowRange=%.3f mapIndex=%d strength=%.3f",
                (unsigned long long)g_lightUploadFrame,
                program->getID(),
                i,
                static_cast<int>(src.type),
                src.castsShadows ? 1 : 0,
                sanitized ? 1 : 0,
                shadowDataValid ? 1 : 0,
                src.position.x, src.position.y, src.position.z,
                src.direction.x, src.direction.y, src.direction.z,
                src.intensity, src.range, src.falloff, src.spotAngle, src.shadowRange,
                shadowData.shadowMapIndex, shadowData.shadowStrength
            );
        }

        dst.meta[0] = static_cast<float>(static_cast<int>(src.type));
        dst.meta[1] = static_cast<float>(static_cast<int>(shadowData.shadowType));
        dst.meta[2] = static_cast<float>(shadowData.shadowMapIndex);
        dst.meta[3] = shadowDataValid ? safeShadowStrength : 0.0f;
        dst.position[0] = safePos.x;
        dst.position[1] = safePos.y;
        dst.position[2] = safePos.z;
        dst.position[3] = 0.0f;

        dst.direction[0] = safeDir.x;
        dst.direction[1] = safeDir.y;
        dst.direction[2] = safeDir.z;
        dst.direction[3] = 0.0f;

        dst.color[0] = safeColor.x;
        dst.color[1] = safeColor.y;
        dst.color[2] = safeColor.z;
        dst.color[3] = safeColor.w;

        dst.params[0] = safeIntensity;
        dst.params[1] = safeRange;
        dst.params[2] = safeFalloff;
        dst.params[3] = safeSpotAngle;

        dst.shadow[0] = safeFloat(shadowData.shadowBias, 0.0025f);
        dst.shadow[1] = safeFloat(shadowData.shadowNormalBias, 0.005f);
        dst.shadow[2] = static_cast<float>(shadowData.cascadeCount);

        dst.cascadeSplits[0] = shadowData.cascadeSplits.x;
        dst.cascadeSplits[1] = shadowData.cascadeSplits.y;
        dst.cascadeSplits[2] = shadowData.cascadeSplits.z;
        dst.cascadeSplits[3] = shadowData.cascadeSplits.w;

        for(int m = 0; m < 4; ++m){
            const float* matPtr = glm::value_ptr(shadowData.lightMatrices[m].data);
            for(int k = 0; k < 16; ++k){
                dst.lightMatrices[m * 16 + k] = matPtr[k];
            }
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
