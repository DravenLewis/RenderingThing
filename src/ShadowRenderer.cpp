#include "ShadowRenderer.h"

#include <unordered_map>

#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "ShaderProgram.h"
#include "Screen.h"
#include "Logbot.h"
#include "ModelPart.h"

namespace {
    constexpr int MAX_SHADOW_MAPS_2D = 4;
    constexpr int MAX_SHADOW_MAPS_CUBE = 2;
    constexpr int SHADOW_TEX_UNIT_BASE_2D = 8;
    constexpr int SHADOW_TEX_UNIT_BASE_CUBE = 12;

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

    std::unordered_map<GLuint, bool> g_shadowSamplerBound;
    std::shared_ptr<Texture> g_debugDepthTexture;
    std::shared_ptr<ShaderProgram> g_shadowDebugProgram;
    std::shared_ptr<ModelPart> g_shadowDebugQuad;
    GLuint g_debugFbo = 0;
    GLuint g_debugColorTex = 0;
    int g_debugSize = 0;

    int getShadowMapSize2D(const Light& light) {
        return (light.type == LightType::DIRECTIONAL) ? 4096 : 2048;
    }

    int getShadowMapSizeCube() {
        return 1024;
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
    float range = Math3D::Max(10.0f, (light.range > 0.0f) ? light.range : defaultRange);
    Math3D::Vec3 lightDir = Math3D::Vec3(glm::normalize(glm::vec3(light.direction.x, light.direction.y, light.direction.z)));
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

    return Math3D::Mat4(proj * view);
}

Math3D::Mat4 ShadowRenderer::computeSpotMatrix(const Light& light) {
    float fov = Math3D::Clamp(light.spotAngle * 2.0f, 10.0f, 170.0f);
    glm::mat4 proj = glm::perspective(glm::radians(fov), 1.0f, 0.1f, Math3D::Max(1.0f, light.range));
    glm::vec3 pos(light.position.x, light.position.y, light.position.z);
    glm::vec3 dir = glm::normalize(glm::vec3(light.direction.x, light.direction.y, light.direction.z));
    glm::vec3 up(0,1,0);
    if(std::abs(glm::dot(dir, up)) > 0.99f){
        up = glm::vec3(0,0,1);
    }
    glm::mat4 view = glm::lookAt(pos, pos + dir, up);
    return Math3D::Mat4(proj * view);
}

void ShadowRenderer::computePointMatrices(const Light& light, std::vector<Math3D::Mat4>& outMatrices) {
    outMatrices.clear();
    outMatrices.reserve(6);
    glm::vec3 pos(light.position.x, light.position.y, light.position.z);
    float nearPlane = 0.1f;
    float farPlane = Math3D::Max(1.0f, light.range);
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0))));
    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0))));
    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1))));
    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1))));
    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0))));
    outMatrices.push_back(Math3D::Mat4(proj * glm::lookAt(pos, pos + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0))));
}

void ShadowRenderer::BeginFrame(PCamera camera) {
    g_enabled = (camera != nullptr && !camera->getSettings().isOrtho);
    g_lightData.clear();
    g_lightData.resize(MAX_LIGHTS);

    if(!g_enabled){
        return;
    }

    ensureShadowPrograms();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &g_savedFbo);
    glGetIntegerv(GL_VIEWPORT, g_savedViewport);

    g_active2D = 0;
    g_activeCube = 0;

    const auto& lights = LightManager::GlobalLightManager.getAllLights();
    for(size_t i = 0; i < lights.size() && i < MAX_LIGHTS; ++i){
        const Light& light = lights[i];
        ShadowLightData data;
        data.shadowType = light.shadowType;
        data.shadowStrength = light.shadowStrength;
        data.shadowBias = light.shadowBias;
        data.shadowNormalBias = light.shadowNormalBias;

        if(light.castsShadows){
            if(light.type == LightType::DIRECTIONAL || light.type == LightType::SPOT){
                if(g_active2D < MAX_SHADOW_MAPS_2D){
                    int mapSize = getShadowMapSize2D(light);
                    if(static_cast<int>(g_shadow2D.size()) <= g_active2D){
                        g_shadow2D.push_back(ShadowSlot2D{ShadowMap2D(mapSize)});
                    }else if(g_shadow2D[g_active2D].map.getSize() != mapSize){
                        g_shadow2D[g_active2D].map.resize(mapSize);
                    }

                    ShadowSlot2D& slot = g_shadow2D[g_active2D];
                    slot.lightIndex = static_cast<int>(i);
                    slot.matrix = (light.type == LightType::DIRECTIONAL)
                        ? computeDirectionalMatrix(light, camera)
                        : computeSpotMatrix(light);
                    data.shadowMapIndex = g_active2D;
                    data.lightMatrix = slot.matrix;
                    g_active2D++;
                }
            }else if(light.type == LightType::POINT){
                if(g_activeCube < MAX_SHADOW_MAPS_CUBE){
                    int mapSize = getShadowMapSizeCube();
                    if(static_cast<int>(g_shadowCube.size()) <= g_activeCube){
                        g_shadowCube.push_back(ShadowSlotCube{ShadowMapCube(mapSize)});
                    }else if(g_shadowCube[g_activeCube].map.getSize() != mapSize){
                        g_shadowCube[g_activeCube].map.resize(mapSize);
                    }

                    ShadowSlotCube& slot = g_shadowCube[g_activeCube];
                    slot.lightIndex = static_cast<int>(i);
                    slot.lightPos = light.position;
                    slot.farPlane = Math3D::Max(1.0f, light.range);
                    computePointMatrices(light, slot.matrices);
                    data.shadowMapIndex = g_activeCube;
                    g_activeCube++;
                }
            }
        }

        g_lightData[i] = data;
    }

    for(int i = 0; i < g_active2D; ++i){
        auto& slot = g_shadow2D[i];
        slot.map.bind();
        glViewport(0, 0, slot.map.getSize(), slot.map.getSize());
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    for(int i = 0; i < g_activeCube; ++i){
        auto& slot = g_shadowCube[i];
        for(int face = 0; face < 6; ++face){
            slot.map.bindFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face);
            glViewport(0, 0, slot.map.getSize(), slot.map.getSize());
            glClear(GL_DEPTH_BUFFER_BIT);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, g_savedFbo);
    glViewport(g_savedViewport[0], g_savedViewport[1], g_savedViewport[2], g_savedViewport[3]);
}

bool ShadowRenderer::IsEnabled() {
    return g_enabled;
}

void ShadowRenderer::RenderShadows(const std::shared_ptr<Mesh>& mesh, const Math3D::Mat4& model, const std::shared_ptr<Material>& material) {
    if(!g_enabled || g_inShadowPass || !mesh || !material){
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
            slot.map.bind();
            glViewport(0, 0, slot.map.getSize(), slot.map.getSize());
            g_shadow2DProgram->setUniformFast("u_model", u_model);
            g_shadow2DProgram->setUniformFast("u_lightMatrix", Uniform<Math3D::Mat4>(slot.matrix));
            mesh->draw();
        }
    }

    if(g_shadowCubeProgram && g_shadowCubeProgram->getID() != 0){
        g_shadowCubeProgram->bind();
        Uniform<Math3D::Mat4> u_model(model);
        for(int i = 0; i < g_activeCube; ++i){
            const auto& slot = g_shadowCube[i];
            glUniform3f(glGetUniformLocation(g_shadowCubeProgram->getID(), "u_lightPos"), slot.lightPos.x, slot.lightPos.y, slot.lightPos.z);
            glUniform1f(glGetUniformLocation(g_shadowCubeProgram->getID(), "u_farPlane"), slot.farPlane);
            for(size_t face = 0; face < slot.matrices.size(); ++face){
                slot.map.bindFace(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<int>(face));
                glViewport(0, 0, slot.map.getSize(), slot.map.getSize());
                g_shadowCubeProgram->setUniformFast("u_model", u_model);
                g_shadowCubeProgram->setUniformFast("u_lightMatrix", Uniform<Math3D::Mat4>(slot.matrices[face]));
                mesh->draw();
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

    const GLuint programId = program->getID();
    if(g_shadowSamplerBound.find(programId) == g_shadowSamplerBound.end()){
        std::vector<int> units2D(MAX_SHADOW_MAPS_2D);
        for(int i = 0; i < MAX_SHADOW_MAPS_2D; ++i){
            units2D[i] = SHADOW_TEX_UNIT_BASE_2D + i;
        }
        GLint loc2D = glGetUniformLocation(programId, "u_shadowMaps2D");
        if(loc2D != -1){
            glUniform1iv(loc2D, MAX_SHADOW_MAPS_2D, units2D.data());
        }

        std::vector<int> unitsCube(MAX_SHADOW_MAPS_CUBE);
        for(int i = 0; i < MAX_SHADOW_MAPS_CUBE; ++i){
            unitsCube[i] = SHADOW_TEX_UNIT_BASE_CUBE + i;
        }
        GLint locCube = glGetUniformLocation(programId, "u_shadowMapsCube");
        if(locCube != -1){
            glUniform1iv(locCube, MAX_SHADOW_MAPS_CUBE, unitsCube.data());
        }

        g_shadowSamplerBound[programId] = true;
    }

    for(int i = 0; i < g_active2D; ++i){
        glActiveTexture(GL_TEXTURE0 + SHADOW_TEX_UNIT_BASE_2D + static_cast<int>(i));
        glBindTexture(GL_TEXTURE_2D, g_shadow2D[i].map.getDepthTexture());
    }

    for(int i = 0; i < g_activeCube; ++i){
        glActiveTexture(GL_TEXTURE0 + SHADOW_TEX_UNIT_BASE_CUBE + static_cast<int>(i));
        glBindTexture(GL_TEXTURE_CUBE_MAP, g_shadowCube[i].map.getDepthTexture());
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
    outData.lightMatrix = Math3D::Mat4(1.0f);
}
