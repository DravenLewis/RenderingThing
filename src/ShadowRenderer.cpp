#include "ShadowRenderer.h"

#include <algorithm>
#include <limits>
#include <string>

#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "ShaderProgram.h"
#include "Screen.h"
#include "Logbot.h"
#include "ModelPart.h"
#include <cmath>

namespace {
    constexpr int MAX_SHADOW_MAPS_2D = 24;
    constexpr int MAX_SHADOW_MAPS_CUBE = 2;
    constexpr int SHADOW_TEX_UNIT_BASE_2D = 8;

    struct ShadowSlot2D {
        ShadowMap2D map;
        Math3D::Mat4 matrix;
        int lightIndex = -1;
    };

    struct ShadowSlotCube {
        ShadowMapCube map;
        std::vector<Math3D::Mat4> matrices;
        Math3D::Vec3 lightPos;
        float farPlane = 25.0f;
        int lightIndex = -1;
    };

    bool g_enabled = false;
    bool g_inShadowPass = false;
    GLint g_savedFbo = 0;
    GLint g_savedViewport[4] = {0,0,0,0};

    std::vector<ShadowSlot2D> g_shadow2D;
    std::vector<ShadowSlotCube> g_shadowCube;
    std::vector<ShadowLightData> g_lightData;
    int g_active2D = 0;
    int g_activeCube = 0;

    std::shared_ptr<ShaderProgram> g_shadow2DProgram;
    std::shared_ptr<ShaderProgram> g_shadowCubeProgram;

    std::shared_ptr<Texture> g_debugDepthTexture;
    std::shared_ptr<ShaderProgram> g_shadowDebugProgram;
    std::shared_ptr<ModelPart> g_shadowDebugQuad;
    GLuint g_debugFbo = 0;
    GLuint g_debugColorTex = 0;
    int g_debugSize = 0;
    int g_debugShadowsMode = 0; // 0=off,1=visibility,2=cascade index,3=proj bounds
    bool g_debugShadowLogging = false;
    uint64_t g_shadowFrameId = 0;
    GLuint g_fallbackShadowTex2D = 0;
    GLuint g_fallbackShadowTexCube = 0;

    struct LightDebugState {
        int type = -1;
        bool castsShadows = false;
        Math3D::Vec3 position = Math3D::Vec3(0,0,0);
        Math3D::Vec3 direction = Math3D::Vec3(0,-1,0);
        float intensity = 0.0f;
        float range = 0.0f;
        float falloff = 0.0f;
        float spotAngle = 0.0f;
        float shadowRange = 0.0f;
    };
    std::vector<LightDebugState> g_lastLightDebug;

    int getMaxShadowMapSize2D(){
        static bool cached = false;
        static int maxSize = 0;
        if(!cached){
            GLint maxTex = 0;
            GLint maxViewport[2] = {0,0};
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
            glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewport);
            maxSize = Math3D::Max(1, Math3D::Min(maxTex, Math3D::Min(maxViewport[0], maxViewport[1])));
            cached = true;
        }
        return maxSize;
    }

    int getMaxShadowMapSizeCube(){
        static bool cached = false;
        static int maxSize = 0;
        if(!cached){
            GLint maxTex = 0;
            GLint maxViewport[2] = {0,0};
            glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxTex);
            glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewport);
            maxSize = Math3D::Max(1, Math3D::Min(maxTex, Math3D::Min(maxViewport[0], maxViewport[1])));
            cached = true;
        }
        return maxSize;
    }

    int getShadowMapSize2D(const Light& light) {
        int target = (light.type == LightType::DIRECTIONAL) ? 4096 : 2048;
        return Math3D::Min(target, getMaxShadowMapSize2D());
    }

    int getShadowMapSizeCube() {
        return Math3D::Min(1024, getMaxShadowMapSizeCube());
    }

    const std::vector<Light>& getActiveLights(){
        auto env = Screen::GetCurrentEnvironment();
        if(env){
            return env->getLightsForUpload();
        }
        static const std::vector<Light> EMPTY;
        return EMPTY;
    }

    float safeFloat(float v, float fallback){
        return std::isfinite(v) ? v : fallback;
    }

    Math3D::Vec3 safeLightDir(const Math3D::Vec3& dir){
        if(!std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z)){
            return Math3D::Vec3(0,-1,0);
        }
        float len = dir.length();
        if(!std::isfinite(len) || len < Math3D::EPSILON){
            return Math3D::Vec3(0,-1,0);
        }
        return dir.normalize();
    }

    bool isValidLightDir(const Math3D::Vec3& dir){
        if(!std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z)){
            return false;
        }
        float len = dir.length();
        return std::isfinite(len) && len >= Math3D::EPSILON;
    }

    struct ShadowCandidate {
        size_t lightIndex = 0;
        LightType type = LightType::POINT;
        int slotCost = 1;
        float normalizedDistance = 0.0f;
        float distanceToCamera = 0.0f;
    };

    int getShadowTypePriority(LightType type){
        switch(type){
            case LightType::DIRECTIONAL: return 0;
            case LightType::SPOT: return 1;
            case LightType::POINT: return 2;
            default: return 3;
        }
    }

    float getDistanceToCamera(const Light& light, PCamera camera){
        if(!camera){
            return 0.0f;
        }
        return Math3D::Vec3::distance(camera->transform().position, light.position);
    }

    float getNormalizedDistance(const Light& light, float distanceToCamera){
        float safeRange = Math3D::Max(1.0f, safeFloat(light.range, 1.0f));
        return distanceToCamera / safeRange;
    }

    bool shadowCandidateLess(const ShadowCandidate& a, const ShadowCandidate& b){
        int typePriorityA = getShadowTypePriority(a.type);
        int typePriorityB = getShadowTypePriority(b.type);
        if(typePriorityA != typePriorityB){
            return typePriorityA < typePriorityB;
        }
        if(!Math3D::AreClose(a.normalizedDistance, b.normalizedDistance, 0.0001f)){
            return a.normalizedDistance < b.normalizedDistance;
        }
        if(!Math3D::AreClose(a.distanceToCamera, b.distanceToCamera, 0.01f)){
            return a.distanceToCamera < b.distanceToCamera;
        }
        return a.lightIndex < b.lightIndex;
    }

    void logLightState(const char* label, size_t index, const Light& light, const ShadowLightData& data){
        if(!g_debugShadowLogging){
            return;
        }
        LogBot.Log(LOG_INFO,
            "[Shadow] %s idx=%zu type=%d casts=%d pos=(%.3f,%.3f,%.3f) dir=(%.3f,%.3f,%.3f) intensity=%.3f range=%.3f falloff=%.3f spot=%.3f shadowRange=%.3f mapIndex=%d strength=%.3f bias=%.6f normalBias=%.6f cascades=%d",
            label,
            index,
            static_cast<int>(light.type),
            light.castsShadows ? 1 : 0,
            light.position.x, light.position.y, light.position.z,
            light.direction.x, light.direction.y, light.direction.z,
            light.intensity,
            light.range,
            light.falloff,
            light.spotAngle,
            light.shadowRange,
            data.shadowMapIndex,
            data.shadowStrength,
            data.shadowBias,
            data.shadowNormalBias,
            data.cascadeCount
        );
    }

    bool isFiniteMat(const Math3D::Mat4& m){
        const float* ptr = glm::value_ptr(m.data);
        for(int i = 0; i < 16; ++i){
            if(!std::isfinite(ptr[i])){
                return false;
            }
        }
        return true;
    }

    GLint getSamplerArrayLocation(GLuint programId, const char* baseName){
        std::string indexedName = std::string(baseName) + "[0]";
        GLint location = glGetUniformLocation(programId, indexedName.c_str());
        if(location == -1){
            location = glGetUniformLocation(programId, baseName);
        }
        return location;
    }

    int getMaxTextureUnits(){
        GLint maxUnits = 0;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxUnits);
        return Math3D::Max(0, static_cast<int>(maxUnits));
    }

    void ensureFallbackShadowTextures(){
        if(g_fallbackShadowTex2D == 0){
            glGenTextures(1, &g_fallbackShadowTex2D);
            glBindTexture(GL_TEXTURE_2D, g_fallbackShadowTex2D);
            float depthOne = 1.0f;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &depthOne);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        if(g_fallbackShadowTexCube == 0){
            glGenTextures(1, &g_fallbackShadowTexCube);
            glBindTexture(GL_TEXTURE_CUBE_MAP, g_fallbackShadowTexCube);
            float depthOne = 1.0f;
            for(int face = 0; face < 6; ++face){
                glTexImage2D(
                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                    0,
                    GL_DEPTH_COMPONENT32F,
                    1,
                    1,
                    0,
                    GL_DEPTH_COMPONENT,
                    GL_FLOAT,
                    &depthOne
                );
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }
    }
}

void ShadowRenderer::ensureShadowPrograms() {
    if(!g_shadow2DProgram){
        g_shadow2DProgram = std::make_shared<ShaderProgram>();
        g_shadow2DProgram->setVertexShader(R"(
            #version 410 core
            layout (location = 0) in vec3 aPos;
            uniform mat4 u_model;
            uniform mat4 u_lightMatrix;
            void main() {
                gl_Position = u_lightMatrix * u_model * vec4(aPos, 1.0);
            }
        )");
        g_shadow2DProgram->setFragmentShader(R"(
            #version 410 core
            void main() { }
        )");
        g_shadow2DProgram->compile();
        if(g_shadow2DProgram->getID() == 0){
            LogBot.Log(LOG_ERRO, "Shadow 2D shader failed to compile/link: \n%s", g_shadow2DProgram->getLog().c_str());
        }
    }

    if(!g_shadowCubeProgram){
        g_shadowCubeProgram = std::make_shared<ShaderProgram>();
        g_shadowCubeProgram->setVertexShader(R"(
            #version 410 core
            layout (location = 0) in vec3 aPos;
            uniform mat4 u_model;
            uniform mat4 u_lightMatrix;
            out vec3 v_worldPos;
            void main() {
                vec4 world = u_model * vec4(aPos, 1.0);
                v_worldPos = world.xyz;
                gl_Position = u_lightMatrix * world;
            }
        )");
        g_shadowCubeProgram->setFragmentShader(R"(
            #version 410 core
            in vec3 v_worldPos;
            uniform vec3 u_lightPos;
            uniform float u_farPlane;
            void main() {
                float dist = length(v_worldPos - u_lightPos);
                gl_FragDepth = dist / u_farPlane;
            }
        )");
        g_shadowCubeProgram->compile();
        if(g_shadowCubeProgram->getID() == 0){
            LogBot.Log(LOG_ERRO, "Shadow cube shader failed to compile/link: \n%s", g_shadowCubeProgram->getLog().c_str());
        }
    }

}

Math3D::Mat4 ShadowRenderer::computeDirectionalMatrix(const Light& light, PCamera camera) {
    Math3D::Vec3 camPos = camera->transform().position;
    float defaultRange = Math3D::Min(camera->getSettings().farPlane, 200.0f);
    float shadowRange = safeFloat(light.shadowRange, light.range);
    float range = Math3D::Max(10.0f, (shadowRange > 0.0f) ? shadowRange : defaultRange);
        Math3D::Vec3 lightDir = safeLightDir(light.direction);
        Math3D::Vec3 lightPos = camPos - lightDir * range;

    glm::vec3 up(0,1,0);
    if(std::abs(glm::dot(glm::vec3(lightDir.x, lightDir.y, lightDir.z), up)) > 0.99f){
        up = glm::vec3(0,0,1);
    }

    glm::mat4 view = glm::lookAt(
        glm::vec3(lightPos.x, lightPos.y, lightPos.z),
        glm::vec3(camPos.x, camPos.y, camPos.z),
        up
    );

    float orthoSize = range;
    glm::mat4 proj = glm::ortho(
        -orthoSize, orthoSize,
        -orthoSize, orthoSize,
        0.1f, range * 2.0f
    );

    Math3D::Mat4 out(proj * view);
    if(!isFiniteMat(out)){
        LogBot.Log(LOG_ERRO, "[Shadow] computeDirectionalMatrix produced NaN/INF. dir=(%.3f,%.3f,%.3f) range=%.3f shadowRange=%.3f",
            light.direction.x, light.direction.y, light.direction.z, light.range, light.shadowRange);
        return Math3D::Mat4(1.0f);
    }
    return out;
}

static void computeDirectionalCascades(
    const Light& light,
    PCamera camera,
    int cascadeCount,
    std::vector<float>& outSplits,
    std::vector<Math3D::Mat4>& outMatrices
){
    outSplits.clear();
    outMatrices.clear();
    if(!camera || cascadeCount <= 0) return;

    float nearPlane = camera->getSettings().nearPlane;
    float shadowRange = safeFloat(light.shadowRange, light.range);
    float farPlane = Math3D::Min(camera->getSettings().farPlane, (shadowRange > 0.0f) ? shadowRange : camera->getSettings().farPlane);
    float lambda = 0.5f;

    std::vector<float> splits;
    splits.reserve(cascadeCount);
    for(int i = 1; i <= cascadeCount; ++i){
        float p = static_cast<float>(i) / static_cast<float>(cascadeCount);
        float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
        float linearSplit = nearPlane + (farPlane - nearPlane) * p;
        float split = lambda * logSplit + (1.0f - lambda) * linearSplit;
        splits.push_back(split);
    }

    glm::mat4 view = (glm::mat4)camera->getViewMatrix();
    glm::mat4 invView = glm::inverse(view);

    Math3D::Vec3 lightDir = safeLightDir(light.direction);
    glm::vec3 up(0,1,0);
    if(std::abs(glm::dot(glm::vec3(lightDir.x, lightDir.y, lightDir.z), up)) > 0.99f){
        up = glm::vec3(0,0,1);
    }

    float prevSplit = nearPlane;
    for(int i = 0; i < cascadeCount; ++i){
        float splitDist = splits[i];
        float tanHalfFov = std::tan(glm::radians(camera->getSettings().fov * 0.5f));
        float aspect = camera->getSettings().aspect;

        float nearH = tanHalfFov * prevSplit;
        float nearW = nearH * aspect;
        float farH = tanHalfFov * splitDist;
        float farW = farH * aspect;

        std::vector<glm::vec3> corners;
        corners.reserve(8);
        corners.push_back(glm::vec3( nearW,  nearH, -prevSplit));
        corners.push_back(glm::vec3(-nearW,  nearH, -prevSplit));
        corners.push_back(glm::vec3( nearW, -nearH, -prevSplit));
        corners.push_back(glm::vec3(-nearW, -nearH, -prevSplit));
        corners.push_back(glm::vec3( farW,  farH, -splitDist));
        corners.push_back(glm::vec3(-farW,  farH, -splitDist));
        corners.push_back(glm::vec3( farW, -farH, -splitDist));
        corners.push_back(glm::vec3(-farW, -farH, -splitDist));

        glm::vec3 center(0);
        for(auto& c : corners){
            glm::vec4 world = invView * glm::vec4(c, 1.0f);
            c = glm::vec3(world) / world.w;
            center += c;
        }
        center /= static_cast<float>(corners.size());

        glm::vec3 lightPos = center - glm::vec3(lightDir.x, lightDir.y, lightDir.z) * 100.0f;
        glm::mat4 lightView = glm::lookAt(lightPos, center, up);

        glm::vec3 minV(FLT_MAX), maxV(-FLT_MAX);
        for(auto& c : corners){
            glm::vec4 ls = lightView * glm::vec4(c, 1.0f);
            minV = glm::min(minV, glm::vec3(ls));
            maxV = glm::max(maxV, glm::vec3(ls));
        }

        float zMult = 10.0f;
        float nearZ = -maxV.z - zMult;
        float farZ = -minV.z + zMult;
        if(nearZ < 0.1f) nearZ = 0.1f;
        if(farZ < nearZ + 0.1f) farZ = nearZ + 0.1f;

        glm::mat4 lightProj = glm::ortho(minV.x, maxV.x, minV.y, maxV.y, nearZ, farZ);

        Math3D::Mat4 out(lightProj * lightView);
        if(!isFiniteMat(out)){
            LogBot.Log(LOG_ERRO, "[Shadow] computeDirectionalCascades produced NaN/INF (cascade %d). dir=(%.3f,%.3f,%.3f)",
                i, light.direction.x, light.direction.y, light.direction.z);
            out = Math3D::Mat4(1.0f);
        }
        outMatrices.push_back(out);
        outSplits.push_back(splitDist);
        prevSplit = splitDist;
    }
}

Math3D::Mat4 ShadowRenderer::computeSpotMatrix(const Light& light) {
    float spotAngle = safeFloat(light.spotAngle, 45.0f);
    float fov = Math3D::Clamp(spotAngle * 2.0f, 10.0f, 170.0f);
    float shadowRange = safeFloat(light.shadowRange, light.range);
    float range = safeFloat(light.range, 10.0f);
    float farPlane = (shadowRange > 0.0f) ? shadowRange : range;
    farPlane = Math3D::Max(1.0f, farPlane);
    glm::mat4 proj = glm::perspective(glm::radians(fov), 1.0f, 0.1f, farPlane);
    glm::vec3 pos(light.position.x, light.position.y, light.position.z);
    Math3D::Vec3 safeDir = safeLightDir(light.direction);
    glm::vec3 dir(safeDir.x, safeDir.y, safeDir.z);
    glm::vec3 up(0,1,0);
    if(std::abs(glm::dot(dir, up)) > 0.99f){
        up = glm::vec3(0,0,1);
    }
    glm::mat4 view = glm::lookAt(pos, pos + dir, up);
    Math3D::Mat4 out(proj * view);
    if(!isFiniteMat(out)){
        LogBot.Log(LOG_ERRO, "[Shadow] computeSpotMatrix produced NaN/INF. dir=(%.3f,%.3f,%.3f) spot=%.3f range=%.3f shadowRange=%.3f",
            light.direction.x, light.direction.y, light.direction.z, light.spotAngle, light.range, light.shadowRange);
        return Math3D::Mat4(1.0f);
    }
    return out;
}

void ShadowRenderer::computePointMatrices(const Light& light, std::vector<Math3D::Mat4>& outMatrices) {
    outMatrices.clear();
    outMatrices.reserve(6);
    glm::vec3 pos(light.position.x, light.position.y, light.position.z);
    float nearPlane = 0.1f;
    float farPlane = (light.shadowRange > 0.0f) ? light.shadowRange : light.range;
    farPlane = Math3D::Max(1.0f, farPlane);
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0))));
    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0))));
    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1))));
    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1))));
    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0))));
    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0))));
}

void ShadowRenderer::BeginFrame(PCamera camera) {
    g_shadowFrameId++;
    g_enabled = (camera != nullptr && !camera->getSettings().isOrtho);
    if(!g_enabled){
        return;
    }

    g_lightData.clear();
    g_lightData.resize(MAX_LIGHTS);

    ensureShadowPrograms();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_savedFbo);
    glGetIntegerv(GL_VIEWPORT, g_savedViewport);

    g_active2D = 0;
    g_activeCube = 0;

    const auto& lights = getActiveLights();
    if(lights.empty()){
        g_enabled = false;
        return;
    }

    if(g_lastLightDebug.size() != lights.size()){
        g_lastLightDebug.clear();
        g_lastLightDebug.resize(lights.size());
        if(g_debugShadowLogging){
            LogBot.Log(LOG_INFO, "[Shadow] Frame %llu lights=%zu (reset debug snapshot)", (unsigned long long)g_shadowFrameId, lights.size());
        }
    }

    std::vector<bool> allow2D(lights.size(), false);
    std::vector<bool> allowCube(lights.size(), false);
    std::vector<ShadowCandidate> candidates2D;
    std::vector<ShadowCandidate> candidatesCube;

    for(size_t i = 0; i < lights.size() && i < MAX_LIGHTS; ++i){
        const Light& light = lights[i];
        if(!light.castsShadows){
            continue;
        }

        const float distanceToCamera = getDistanceToCamera(light, camera);
        const float normalizedDistance = getNormalizedDistance(light, distanceToCamera);

        if(light.type == LightType::DIRECTIONAL){
            ShadowCandidate candidate;
            candidate.lightIndex = i;
            candidate.type = light.type;
            candidate.slotCost = 4; // 4 cascades
            candidate.normalizedDistance = 0.0f;
            candidate.distanceToCamera = 0.0f;
            candidates2D.push_back(candidate);
        }else if(light.type == LightType::SPOT){
            if(!isValidLightDir(light.direction)){
                continue;
            }
            ShadowCandidate candidate;
            candidate.lightIndex = i;
            candidate.type = light.type;
            candidate.slotCost = 1;
            candidate.normalizedDistance = normalizedDistance;
            candidate.distanceToCamera = distanceToCamera;
            candidates2D.push_back(candidate);
        }else if(light.type == LightType::POINT){
            ShadowCandidate candidate;
            candidate.lightIndex = i;
            candidate.type = light.type;
            candidate.slotCost = 1;
            candidate.normalizedDistance = normalizedDistance;
            candidate.distanceToCamera = distanceToCamera;
            candidatesCube.push_back(candidate);
        }
    }

    std::sort(candidates2D.begin(), candidates2D.end(), shadowCandidateLess);
    std::sort(candidatesCube.begin(), candidatesCube.end(), shadowCandidateLess);

    int remaining2DSlots = MAX_SHADOW_MAPS_2D;
    for(const ShadowCandidate& candidate : candidates2D){
        if(candidate.slotCost <= remaining2DSlots){
            allow2D[candidate.lightIndex] = true;
            remaining2DSlots -= candidate.slotCost;
        }
    }

    int remainingCubeSlots = MAX_SHADOW_MAPS_CUBE;
    for(const ShadowCandidate& candidate : candidatesCube){
        if(candidate.slotCost <= remainingCubeSlots){
            allowCube[candidate.lightIndex] = true;
            remainingCubeSlots -= candidate.slotCost;
        }
    }

    for(size_t i = 0; i < lights.size() && i < MAX_LIGHTS; ++i){
        const Light& light = lights[i];
        ShadowLightData data;
        data.shadowType = light.shadowType;
        data.shadowStrength = light.shadowStrength;
        data.shadowBias = light.shadowBias;
        data.shadowNormalBias = light.shadowNormalBias;
        data.shadowMapIndex = -1;
        data.cascadeCount = 0;

        if(light.castsShadows){
            if(light.type == LightType::DIRECTIONAL){
                if(i < allow2D.size() && allow2D[i]){
                    int cascadeCount = 4;
                    std::vector<float> splits;
                    std::vector<Math3D::Mat4> matrices;
                    computeDirectionalCascades(light, camera, cascadeCount, splits, matrices);

                    if(g_active2D + cascadeCount <= MAX_SHADOW_MAPS_2D){
                        if(static_cast<int>(g_shadow2D.size()) < g_active2D + cascadeCount){
                            while(static_cast<int>(g_shadow2D.size()) < g_active2D + cascadeCount){
                                g_shadow2D.push_back(ShadowSlot2D{ShadowMap2D(getShadowMapSize2D(light))});
                            }
                        }

                        int mapSize = getShadowMapSize2D(light);
                        bool hasValidMap = true;
                        for(int c = 0; c < cascadeCount; ++c){
                            ShadowSlot2D& slot = g_shadow2D[g_active2D];
                            if(slot.map.getSize() != mapSize){
                                slot.map.resize(mapSize);
                            }
                            if(slot.map.getDepthTexture() == 0 || slot.map.getSize() <= 0){
                                hasValidMap = false;
                                break;
                            }
                            slot.lightIndex = static_cast<int>(i);
                            slot.matrix = (c < static_cast<int>(matrices.size())) ? matrices[c] : Math3D::Mat4(1.0f);
                            if(c == 0){
                                data.shadowMapIndex = g_active2D;
                            }
                            g_active2D++;
                        }

                        data.cascadeCount = cascadeCount;
                        data.cascadeSplits = Math3D::Vec4(
                            splits.size() > 0 ? splits[0] : 0.0f,
                            splits.size() > 1 ? splits[1] : 0.0f,
                            splits.size() > 2 ? splits[2] : 0.0f,
                            splits.size() > 3 ? splits[3] : 0.0f
                        );

                        for(int c = 0; c < 4; ++c){
                            data.lightMatrices[c] = (c < static_cast<int>(matrices.size())) ? matrices[c] : Math3D::Mat4(1.0f);
                        }

                        if(!hasValidMap){
                            data.shadowMapIndex = -1;
                            data.cascadeCount = 0;
                        }
                    }
                }else if(g_debugShadowLogging){
                    LogBot.Log(LOG_INFO, "[Shadow] Directional skipped by budget idx=%zu", i);
                }
            }else if(light.type == LightType::SPOT){
                if(!(i < allow2D.size() && allow2D[i])){
                    if(g_debugShadowLogging){
                        LogBot.Log(LOG_INFO, "[Shadow] Spot skipped by budget idx=%zu", i);
                    }
                }else if(g_active2D < MAX_SHADOW_MAPS_2D && isValidLightDir(light.direction)){
                    int mapSize = getShadowMapSize2D(light);
                    if(static_cast<int>(g_shadow2D.size()) <= g_active2D){
                        g_shadow2D.push_back(ShadowSlot2D{ShadowMap2D(mapSize)});
                    }else if(g_shadow2D[g_active2D].map.getSize() != mapSize){
                        g_shadow2D[g_active2D].map.resize(mapSize);
                    }

                    ShadowSlot2D& slot = g_shadow2D[g_active2D];
                    slot.lightIndex = static_cast<int>(i);
                    slot.matrix = computeSpotMatrix(light);
                    if(slot.map.getDepthTexture() != 0 && slot.map.getSize() > 0){
                        data.shadowMapIndex = g_active2D;
                        data.cascadeCount = 1;
                        float shadowRange = (light.shadowRange > 0.0f) ? light.shadowRange : light.range;
                        data.cascadeSplits = Math3D::Vec4(shadowRange, shadowRange, shadowRange, shadowRange);
                        data.lightMatrices[0] = slot.matrix;
                        if(g_debugShadowLogging){
                            LogBot.Log(LOG_INFO, "[Shadow] Spot alloc idx=%zu mapIndex=%d mapSize=%d", i, g_active2D, mapSize);
                        }
                        g_active2D++;
                    }
                }else if(!isValidLightDir(light.direction)){
                    if(g_debugShadowLogging){
                        LogBot.Log(LOG_WARN, "[Shadow] Spot skipped (invalid dir) idx=%zu dir=(%.3f,%.3f,%.3f)", i,
                            light.direction.x, light.direction.y, light.direction.z);
                    }
                }
            }else if(light.type == LightType::POINT){
                if(!(i < allowCube.size() && allowCube[i])){
                    if(g_debugShadowLogging){
                        LogBot.Log(LOG_INFO, "[Shadow] Point skipped by budget idx=%zu", i);
                    }
                }else if(g_activeCube < MAX_SHADOW_MAPS_CUBE){
                    int mapSize = getShadowMapSizeCube();
                    if(static_cast<int>(g_shadowCube.size()) <= g_activeCube){
                        g_shadowCube.push_back(ShadowSlotCube{ShadowMapCube(mapSize)});
                    }else if(g_shadowCube[g_activeCube].map.getSize() != mapSize){
                        g_shadowCube[g_activeCube].map.resize(mapSize);
                    }

                    ShadowSlotCube& slot = g_shadowCube[g_activeCube];
                    slot.lightIndex = static_cast<int>(i);
                    slot.lightPos = light.position;
                    slot.farPlane = (light.shadowRange > 0.0f) ? light.shadowRange : light.range;
                    slot.farPlane = Math3D::Max(1.0f, slot.farPlane);
                    computePointMatrices(light, slot.matrices);
                    if(slot.map.getDepthTexture() != 0 && slot.map.getSize() > 0){
                        data.shadowMapIndex = g_activeCube;
                        data.cascadeCount = 1;
                        data.cascadeSplits = Math3D::Vec4(slot.farPlane, slot.farPlane, slot.farPlane, slot.farPlane);
                        g_activeCube++;
                        if(g_debugShadowLogging){
                            LogBot.Log(LOG_INFO, "[Shadow] Point alloc idx=%zu mapIndex=%d mapSize=%d", i, data.shadowMapIndex, mapSize);
                        }
                    }
                }
            }
        }

        g_lightData[i] = data;

        // Debug logging for state changes or invalid data.
        LightDebugState snapshot;
        snapshot.type = static_cast<int>(light.type);
        snapshot.castsShadows = light.castsShadows;
        snapshot.position = light.position;
        snapshot.direction = light.direction;
        snapshot.intensity = light.intensity;
        snapshot.range = light.range;
        snapshot.falloff = light.falloff;
        snapshot.spotAngle = light.spotAngle;
        snapshot.shadowRange = light.shadowRange;

        bool changed = false;
        if(i < g_lastLightDebug.size()){
            const auto& prev = g_lastLightDebug[i];
            changed = (prev.type != snapshot.type) ||
                      (prev.castsShadows != snapshot.castsShadows) ||
                      (Math3D::Vec3::distance(prev.position, snapshot.position) > 0.001f) ||
                      (Math3D::Vec3::distance(prev.direction, snapshot.direction) > 0.001f) ||
                      !Math3D::AreClose(prev.intensity, snapshot.intensity, 0.001f) ||
                      !Math3D::AreClose(prev.range, snapshot.range, 0.001f) ||
                      !Math3D::AreClose(prev.falloff, snapshot.falloff, 0.001f) ||
                      !Math3D::AreClose(prev.spotAngle, snapshot.spotAngle, 0.001f) ||
                      !Math3D::AreClose(prev.shadowRange, snapshot.shadowRange, 0.001f);
        }

        bool invalidDir = !isValidLightDir(light.direction) && light.type != LightType::POINT;
        bool invalidRange = !std::isfinite(light.range) || light.range <= 0.0f;
        bool invalidSpot = (light.type == LightType::SPOT) && (!std::isfinite(light.spotAngle) || light.spotAngle <= 0.0f);
        if(changed || invalidDir || invalidRange || invalidSpot){
            logLightState(changed ? "Change" : "Invalid", i, light, data);
        }

        if(i < g_lastLightDebug.size()){
            g_lastLightDebug[i] = snapshot;
        }
    }

    for(int i = 0; i < g_active2D; ++i){
        auto& slot = g_shadow2D[i];
        if(slot.map.getDepthTexture() == 0 || slot.map.getSize() <= 0){
            continue;
        }
        slot.map.bind();
        glViewport(0, 0, slot.map.getSize(), slot.map.getSize());
        glClear(GL_DEPTH_BUFFER_BIT);
        GLenum err = glGetError();
        if(err != GL_NO_ERROR){
            LogBot.Log(LOG_ERRO, "[Shadow] GL error after clearing 2D shadow map %d: 0x%X", i, err);
        }
    }

    for(int i = 0; i < g_activeCube; ++i){
        auto& slot = g_shadowCube[i];
        if(slot.map.getDepthTexture() == 0 || slot.map.getSize() <= 0){
            continue;
        }
        for(int face = 0; face < 6; ++face){
            slot.map.bindFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face);
            glViewport(0, 0, slot.map.getSize(), slot.map.getSize());
            glClear(GL_DEPTH_BUFFER_BIT);
            GLenum err = glGetError();
            if(err != GL_NO_ERROR){
                LogBot.Log(LOG_ERRO, "[Shadow] GL error after clearing cube shadow map %d face %d: 0x%X", i, face, err);
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, g_savedFbo);
    glViewport(g_savedViewport[0], g_savedViewport[1], g_savedViewport[2], g_savedViewport[3]);
    {
        GLenum err = glGetError();
        if(err != GL_NO_ERROR){
            LogBot.Log(LOG_ERRO, "[Shadow] GL error after BeginFrame restore: 0x%X", err);
        }
    }
}

bool ShadowRenderer::IsEnabled() {
    return g_enabled;
}

void ShadowRenderer::RenderShadows(const std::shared_ptr<Mesh>& mesh, const Math3D::Mat4& model, const std::shared_ptr<Material>& material) {
    if(!g_enabled || g_inShadowPass || !mesh || !material){
        return;
    }

    auto currentCam = Screen::GetCurrentCamera();
    if(currentCam && currentCam->getSettings().isOrtho){
        return;
    }

    if(!material->castsShadows()){
        return;
    }

    g_inShadowPass = true;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    if(g_shadow2DProgram && g_shadow2DProgram->getID() != 0){
        g_shadow2DProgram->bind();
        Uniform<Math3D::Mat4> u_model(model);
        for(int i = 0; i < g_active2D; ++i){
            const auto& slot = g_shadow2D[i];
            if(slot.map.getDepthTexture() == 0 || slot.map.getSize() <= 0){
                continue;
            }
            slot.map.bind();
            glViewport(0, 0, slot.map.getSize(), slot.map.getSize());
            g_shadow2DProgram->setUniformFast("u_model", u_model);
            g_shadow2DProgram->setUniformFast("u_lightMatrix", Uniform<Math3D::Mat4>(slot.matrix));
            mesh->draw();
            GLenum err = glGetError();
            if(err != GL_NO_ERROR){
                LogBot.Log(LOG_ERRO, "[Shadow] GL error during 2D shadow draw slot %d: 0x%X", i, err);
            }
        }
    }

    if(g_shadowCubeProgram && g_shadowCubeProgram->getID() != 0){
        g_shadowCubeProgram->bind();
        Uniform<Math3D::Mat4> u_model(model);
        for(int i = 0; i < g_activeCube; ++i){
            const auto& slot = g_shadowCube[i];
            if(slot.map.getDepthTexture() == 0 || slot.map.getSize() <= 0){
                continue;
            }
            glUniform3f(glGetUniformLocation(g_shadowCubeProgram->getID(), "u_lightPos"), slot.lightPos.x, slot.lightPos.y, slot.lightPos.z);
            glUniform1f(glGetUniformLocation(g_shadowCubeProgram->getID(), "u_farPlane"), slot.farPlane);
            for(size_t face = 0; face < slot.matrices.size(); ++face){
                slot.map.bindFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<int>(face));
                glViewport(0, 0, slot.map.getSize(), slot.map.getSize());
                g_shadowCubeProgram->setUniformFast("u_model", u_model);
                g_shadowCubeProgram->setUniformFast("u_lightMatrix", Uniform<Math3D::Mat4>(slot.matrices[face]));
                mesh->draw();
                GLenum err = glGetError();
                if(err != GL_NO_ERROR){
                    LogBot.Log(LOG_ERRO, "[Shadow] GL error during cube shadow draw slot %d face %zu: 0x%X", i, face, err);
                }
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, g_savedFbo);
    glViewport(g_savedViewport[0], g_savedViewport[1], g_savedViewport[2], g_savedViewport[3]);
    glEnable(GL_CULL_FACE);
    g_inShadowPass = false;
}

void ShadowRenderer::BindShadowSamplers(const std::shared_ptr<ShaderProgram>& program) {
    if(!g_enabled || !program || program->getID() == 0){
        return;
    }

    program->bind();
    const GLuint programId = program->getID();
    GLint locDebug = glGetUniformLocation(programId, "u_debugShadows");
    if(locDebug != -1){
        glUniform1i(locDebug, g_debugShadowsMode);
    }

    GLint loc2D = getSamplerArrayLocation(programId, "u_shadowMaps2D");
    GLint locCube = getSamplerArrayLocation(programId, "u_shadowMapsCube");
    const int size2D = (loc2D != -1) ? MAX_SHADOW_MAPS_2D : 0;
    const int sizeCube = (locCube != -1) ? MAX_SHADOW_MAPS_CUBE : 0;

    if(size2D == 0 && sizeCube == 0){
        glActiveTexture(GL_TEXTURE0);
        return;
    }

    const int maxUnits = getMaxTextureUnits();
    const int shadowUnitCount = Math3D::Max(0, maxUnits - SHADOW_TEX_UNIT_BASE_2D);
    if((size2D > 0 || sizeCube > 0) && shadowUnitCount < 2){
        glActiveTexture(GL_TEXTURE0);
        return;
    }

    ensureFallbackShadowTextures();

    const int fallback2DUnit = SHADOW_TEX_UNIT_BASE_2D;
    const int fallbackCubeUnit = SHADOW_TEX_UNIT_BASE_2D + shadowUnitCount - 1;
    const int realUnitsStart = SHADOW_TEX_UNIT_BASE_2D + 1;
    const int realUnitsCount = Math3D::Max(0, shadowUnitCount - 2);

    int want2D = Math3D::Min(g_active2D, size2D);
    int wantCube = Math3D::Min(g_activeCube, sizeCube);
    int real2D = Math3D::Min(want2D, realUnitsCount);
    int remaining = realUnitsCount - real2D;
    int realCube = Math3D::Min(wantCube, remaining);
    if(wantCube > 0 && realCube == 0 && real2D > 0){
        real2D--;
        realCube = 1;
    }

    std::vector<int> units2D(static_cast<size_t>(size2D), fallback2DUnit);
    for(int i = 0; i < real2D; ++i){
        units2D[i] = realUnitsStart + i;
    }

    std::vector<int> unitsCube(static_cast<size_t>(sizeCube), fallbackCubeUnit);
    const int cubeUnitsStart = realUnitsStart + real2D;
    for(int i = 0; i < realCube; ++i){
        unitsCube[i] = cubeUnitsStart + i;
    }

    if(loc2D != -1 && size2D > 0){
        glUniform1iv(loc2D, size2D, units2D.data());
    }
    if(locCube != -1 && sizeCube > 0){
        glUniform1iv(locCube, sizeCube, unitsCube.data());
    }

    if(size2D > 0){
        glActiveTexture(GL_TEXTURE0 + fallback2DUnit);
        glBindTexture(GL_TEXTURE_2D, g_fallbackShadowTex2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
    if(sizeCube > 0){
        glActiveTexture(GL_TEXTURE0 + fallbackCubeUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, g_fallbackShadowTexCube);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }

    for(int i = 0; i < real2D; ++i){
        GLuint texId = g_fallbackShadowTex2D;
        if(i < static_cast<int>(g_shadow2D.size())){
            GLuint mapTex = g_shadow2D[i].map.getDepthTexture();
            if(mapTex != 0){
                texId = mapTex;
            }
        }
        glActiveTexture(GL_TEXTURE0 + units2D[i]);
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }

    for(int i = 0; i < realCube; ++i){
        GLuint texId = g_fallbackShadowTexCube;
        if(i < static_cast<int>(g_shadowCube.size())){
            GLuint mapTex = g_shadowCube[i].map.getDepthTexture();
            if(mapTex != 0){
                texId = mapTex;
            }
        }
        glActiveTexture(GL_TEXTURE0 + unitsCube[i]);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texId);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }

    // Restore default active texture unit to avoid surprising later binds.
    glActiveTexture(GL_TEXTURE0);
    {
        GLenum err = glGetError();
        if(err != GL_NO_ERROR){
            LogBot.Log(LOG_ERRO, "[Shadow] GL error during BindShadowSamplers: 0x%X", err);
        }
    }

    if(!g_shadowDebugProgram){
        g_shadowDebugProgram = std::make_shared<ShaderProgram>();
        g_shadowDebugProgram->setVertexShader(R"(
            #version 410 core
            layout (location = 0) in vec3 aPos;
            layout (location = 3) in vec2 aTexCoord;
            out vec2 v_uv;
            void main() {
                v_uv = aTexCoord;
                gl_Position = vec4(aPos.xy, 0.0, 1.0);
            }
        )");
        g_shadowDebugProgram->setFragmentShader(R"(
            #version 410 core
            in vec2 v_uv;
            out vec4 FragColor;
            uniform sampler2D u_depthTex;
            void main() {
                float d = texture(u_depthTex, v_uv).r;
                FragColor = vec4(vec3(d), 1.0);
            }
        )");
        g_shadowDebugProgram->compile();
        if(g_shadowDebugProgram->getID() == 0){
            LogBot.Log(LOG_ERRO, "Shadow debug shader failed to compile/link: \n%s", g_shadowDebugProgram->getLog().c_str());
        }
    }

    if(!g_shadowDebugQuad){
        auto factory = ModelPartFactory::Create(nullptr);
        int v1, v2, v3, v4;
        factory
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f, -1.0f, 0.0f)).UV(0,0), &v1)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f, -1.0f, 0.0f)).UV(1,0), &v2)
            .addVertex(Vertex::Build(Math3D::Vec3( 1.0f,  1.0f, 0.0f)).UV(1,1), &v3)
            .addVertex(Vertex::Build(Math3D::Vec3(-1.0f,  1.0f, 0.0f)).UV(0,1), &v4)
            .defineFace(v1, v2, v3, v4);
        g_shadowDebugQuad = factory.assemble();
    }
}

std::shared_ptr<Texture> ShadowRenderer::GetDepthBuffer() {
    if(g_active2D <= 0){
        return nullptr;
    }

    ensureShadowPrograms();

    const auto& slot = g_shadow2D[0];
    GLuint id = slot.map.getDepthTexture();
    int size = slot.map.getSize();

    if(size <= 0){
        return nullptr;
    }

    if(g_debugSize != size || g_debugColorTex == 0 || g_debugFbo == 0){
        if(g_debugColorTex != 0){
            glDeleteTextures(1, &g_debugColorTex);
            g_debugColorTex = 0;
        }
        if(g_debugFbo != 0){
            glDeleteFramebuffers(1, &g_debugFbo);
            g_debugFbo = 0;
        }

        glGenTextures(1, &g_debugColorTex);
        glBindTexture(GL_TEXTURE_2D, g_debugColorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &g_debugFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_debugFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_debugColorTex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        g_debugSize = size;
        g_debugDepthTexture = Texture::CreateFromExisting(g_debugColorTex, size, size, false);
    }

    GLint prevFbo = 0;
    GLint prevViewport[4] = {0,0,0,0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, g_debugFbo);
    glViewport(0, 0, size, size);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    GLint prevCompareMode = 0;
    glBindTexture(GL_TEXTURE_2D, id);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, &prevCompareMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    g_shadowDebugProgram->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, id);
    glUniform1i(glGetUniformLocation(g_shadowDebugProgram->getID(), "u_depthTex"), 0);

    if(g_shadowDebugQuad){
        g_shadowDebugQuad->draw(Math3D::Mat4(), Math3D::Mat4(), Math3D::Mat4());
    }

    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, prevCompareMode);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    return g_debugDepthTexture;
}

void ShadowRenderer::GetShadowDataForLight(size_t index, const Light& light, ShadowLightData& outData) {
    if(index < g_lightData.size()){
        outData = g_lightData[index];
        return;
    }

    outData.shadowMapIndex = -1;
    outData.shadowType = light.shadowType;
    outData.shadowStrength = light.shadowStrength;
    outData.shadowBias = light.shadowBias;
    outData.shadowNormalBias = light.shadowNormalBias;
    outData.cascadeCount = 1;
    outData.cascadeSplits = Math3D::Vec4(0,0,0,0);
    outData.lightMatrices[0] = Math3D::Mat4(1.0f);
    outData.lightMatrices[1] = Math3D::Mat4(1.0f);
    outData.lightMatrices[2] = Math3D::Mat4(1.0f);
    outData.lightMatrices[3] = Math3D::Mat4(1.0f);
}

void ShadowRenderer::SetDebugShadows(bool enabled) {
    g_debugShadowsMode = enabled ? 1 : 0;
}

bool ShadowRenderer::GetDebugShadows() {
    return g_debugShadowsMode != 0;
}

void ShadowRenderer::CycleDebugShadows() {
    g_debugShadowsMode = (g_debugShadowsMode + 1) % 4;
}

int ShadowRenderer::GetDebugShadowsMode() {
    return g_debugShadowsMode;
}

void ShadowRenderer::SetDebugLogging(bool enabled) {
    g_debugShadowLogging = enabled;
}

bool ShadowRenderer::GetDebugLogging() {
    return g_debugShadowLogging;
}
