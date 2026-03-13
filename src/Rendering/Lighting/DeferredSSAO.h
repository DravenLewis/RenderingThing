#ifndef DEFERREDSSAO_H
#define DEFERREDSSAO_H

#include <array>
#include <memory>
#include <random>
#include <string>

#include "Foundation/Logging/Logbot.h"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Core/FrameBuffer.h"
#include "Rendering/Core/Graphics.h"
#include "Rendering/Geometry/ModelPart.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Rendering/Textures/Texture.h"

struct DeferredSSAOSettings {
    float radiusPx = 3.0f;
    float depthRadius = 0.025f;
    float bias = 0.001f;
    float intensity = 1.0f;
    float giBoost = 0.12f;
    float blurRadiusPx = 2.0f;
    float blurSharpness = 2.0f;
    int sampleCount = 16;
    int debugView = 0; // 0=composited, 1=combined AO, 2=SSAO raw, 3=material AO, 4=GI
};

class DeferredSSAO {
    private:
        static constexpr int MAX_KERNEL_SAMPLES = 64;

        std::shared_ptr<ShaderProgram> rawShader;
        std::shared_ptr<ShaderProgram> blurShader;
        bool compileAttempted = false;
        bool kernelInitialized = false;
        std::array<Math3D::Vec3, MAX_KERNEL_SAMPLES> kernelSamples{};
        PFrameBuffer rawAoFbo = nullptr;
        PFrameBuffer blurAoFbo = nullptr;

        const std::string SSAO_RAW_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D normalTexture;
            uniform sampler2D positionTexture;
            uniform mat4 u_viewMatrix;
            uniform mat4 u_projMatrix;
            uniform vec3 u_samples[64];
            uniform int u_kernelSize;
            uniform vec2 u_texelSize;
            uniform float u_radiusPx;
            uniform float u_depthRadius;
            uniform float u_bias;

            vec3 safeNormalize(vec3 v){
                float lenV = length(v);
                return (lenV > 1e-5) ? (v / lenV) : vec3(0.0, 0.0, 1.0);
            }

            vec3 worldToViewPosition(vec3 worldPos){
                return (u_viewMatrix * vec4(worldPos, 1.0)).xyz;
            }

            vec3 worldToViewNormal(vec3 worldNormal){
                return safeNormalize(mat3(u_viewMatrix) * worldNormal);
            }

            vec3 geometricNormalFromViewPos(vec3 posView){
                vec3 dx = dFdx(posView);
                vec3 dy = dFdy(posView);
                vec3 n = safeNormalize(cross(dx, dy));
                vec3 viewDir = safeNormalize(-posView);
                if(dot(n, viewDir) < 0.0){
                    n = -n;
                }
                return n;
            }

            float hash12(vec2 p){
                vec3 p3 = fract(vec3(p.xyx) * 0.1031);
                p3 += dot(p3, p3.yzx + 33.33);
                return fract((p3.x + p3.y) * p3.z);
            }

            vec3 hash32(vec2 p){
                return vec3(
                    hash12(p + vec2(17.0, 31.0)) * 2.0 - 1.0,
                    hash12(p + vec2(59.0, 7.0)) * 2.0 - 1.0,
                    hash12(p + vec2(83.0, 23.0)) * 2.0 - 1.0
                );
            }

            vec3 fallbackTangent(vec3 normalView){
                vec3 axis = (abs(normalView.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
                return safeNormalize(cross(axis, normalView));
            }

            void main() {
                vec3 normalWorld = texture(normalTexture, TexCoords).xyz;
                if(length(normalWorld) < 1e-4){
                    FragColor = vec4(1.0);
                    return;
                }

                vec3 fragPosView = worldToViewPosition(texture(positionTexture, TexCoords).xyz);
                vec3 normalFromGBufferView = worldToViewNormal(normalWorld);
                vec3 normalView = safeNormalize(normalFromGBufferView);

                vec2 noiseSeed = floor(gl_FragCoord.xy);
                vec3 randomVec = safeNormalize(hash32(noiseSeed));
                if(length(randomVec) <= 1e-5){
                    randomVec = fallbackTangent(normalView);
                }
                vec3 tangent = randomVec - normalView * dot(randomVec, normalView);
                tangent = (length(tangent) > 1e-5) ? normalize(tangent) : fallbackTangent(normalView);
                vec3 bitangent = safeNormalize(cross(normalView, tangent));
                tangent = safeNormalize(cross(bitangent, normalView));
                mat3 TBN = mat3(tangent, bitangent, normalView);

                float projScaleY = max(abs(u_projMatrix[1][1]), 0.0001);
                float viewDepth = max(abs(fragPosView.z), 0.001);
                float worldUnitsPerPixel = (2.0 * viewDepth * u_texelSize.y / projScaleY);
                float pixelWorldRadius = worldUnitsPerPixel * max(u_radiusPx, 0.25);
                float radius = max(u_depthRadius * 24.0, pixelWorldRadius * 6.0);
                radius = clamp(radius, 0.02, 2.5);
                float bias = max(u_bias, radius * 0.02);
                float thickness = max(u_depthRadius * 4.0, worldUnitsPerPixel * 4.0);
                thickness = clamp(thickness, 0.01, max(radius * 0.35, 0.01));

                int kernelSize = clamp(u_kernelSize, 1, 64);
                float occlusion = 0.0;
                float weightSum = 0.0;
                for(int i = 0; i < kernelSize; ++i){
                    vec3 samplePosView = fragPosView + (TBN * u_samples[i]) * radius;
                    vec4 offset = u_projMatrix * vec4(samplePosView, 1.0);
                    if(abs(offset.w) <= 1e-5){
                        continue;
                    }

                    offset.xyz /= offset.w;
                    vec2 sampleUv = offset.xy * 0.5 + 0.5;
                    if(sampleUv.x <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y <= 0.0 || sampleUv.y >= 1.0){
                        continue;
                    }

                    vec3 sampleNormalWorld = texture(normalTexture, sampleUv).xyz;
                    if(length(sampleNormalWorld) < 1e-4){
                        continue;
                    }

                    vec3 sampleScenePosView = worldToViewPosition(texture(positionTexture, sampleUv).xyz);
                    vec3 sampleDeltaView = sampleScenePosView - fragPosView;
                    float sampleDistance = length(sampleDeltaView);
                    if(sampleDistance <= 1e-4 || sampleDistance > radius){
                        continue;
                    }

                    float depthDelta = abs(sampleScenePosView.z - samplePosView.z);
                    float rangeWeight = 1.0 - smoothstep(thickness, thickness * 2.5, depthDelta);
                    if(rangeWeight <= 0.001){
                        continue;
                    }

                    float distanceWeight = 1.0 - smoothstep(0.0, radius, sampleDistance);
                    float normalDistance = dot(normalView, sampleDeltaView);
                    float occluded = clamp((normalDistance - bias) / sampleDistance, 0.0, 1.0);
                    float weight = distanceWeight * rangeWeight;
                    occlusion += occluded * weight;
                    weightSum += weight;
                }

                float ao = 1.0 - ((weightSum > 0.0) ? (occlusion / weightSum) : 0.0);
                ao = clamp(pow(ao, 2.0), 0.0, 1.0);
                FragColor = vec4(vec3(ao), 1.0);
            }
        )";

        const std::string SSAO_BLUR_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D rawAoTexture;
            uniform sampler2D normalTexture;
            uniform sampler2D positionTexture;
            uniform mat4 u_viewMatrix;
            uniform vec2 u_texelSize;
            uniform float u_blurRadiusPx;
            uniform float u_blurSharpness;

            vec3 safeNormalize(vec3 v){
                float lenV = length(v);
                return (lenV > 1e-5) ? (v / lenV) : vec3(0.0, 0.0, 1.0);
            }

            vec3 worldToViewPosition(vec3 worldPos){
                return (u_viewMatrix * vec4(worldPos, 1.0)).xyz;
            }

            vec3 worldToViewNormal(vec3 worldNormal){
                return safeNormalize(mat3(u_viewMatrix) * worldNormal);
            }

            void main() {
                float centerAo = clamp(texture(rawAoTexture, TexCoords).r, 0.0, 1.0);
                vec3 centerNormalWorld = texture(normalTexture, TexCoords).xyz;
                if(length(centerNormalWorld) < 1e-4){
                    FragColor = vec4(centerAo, centerAo, centerAo, 1.0);
                    return;
                }

                vec3 centerNormalView = worldToViewNormal(centerNormalWorld);
                vec3 centerPosView = worldToViewPosition(texture(positionTexture, TexCoords).xyz);

                float blurRadius = max(u_blurRadiusPx, 0.5);
                float sharpness = clamp(u_blurSharpness, 0.25, 8.0);
                float sharpnessNorm = smoothstep(0.25, 8.0, sharpness);
                float spatialSigma = max(blurRadius * 0.85, 0.9);
                float depthSigma = max(abs(centerPosView.z) * mix(0.025, 0.010, sharpnessNorm), 0.006);

                float aoSum = 0.0;
                float weightSum = 0.0;
                for(int x = -2; x <= 2; ++x){
                    for(int y = -2; y <= 2; ++y){
                        vec2 kernelOffset = vec2(float(x), float(y));
                        vec2 sampleUv = TexCoords + (kernelOffset * u_texelSize * blurRadius);
                        if(sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0){
                            continue;
                        }

                        vec3 sampleNormalWorld = texture(normalTexture, sampleUv).xyz;
                        if(length(sampleNormalWorld) < 1e-4){
                            continue;
                        }

                        float sampleAo = clamp(texture(rawAoTexture, sampleUv).r, 0.0, 1.0);
                        vec3 sampleNormalView = worldToViewNormal(sampleNormalWorld);
                        vec3 samplePosView = worldToViewPosition(texture(positionTexture, sampleUv).xyz);

                        float kernelLen = length(kernelOffset);
                        float spatialWeight = exp(-(kernelLen * kernelLen) / max(2.0 * spatialSigma * spatialSigma, 0.0001));
                        float normalWeight = pow(max(dot(centerNormalView, sampleNormalView), 0.0), mix(1.0, 3.0, sharpnessNorm));
                        normalWeight = mix(0.25, 1.0, normalWeight);
                        float viewDepthDiff = abs(samplePosView.z - centerPosView.z);
                        float planeDiff = abs(dot(centerNormalView, samplePosView - centerPosView));
                        float depthMetric = max(planeDiff, viewDepthDiff * 0.5);
                        float depthWeight = exp(-depthMetric / max(depthSigma, 0.0001));
                        float edgeReject = 1.0 - smoothstep(depthSigma * 1.5, depthSigma * 4.0, depthMetric);
                        if(edgeReject <= 0.001){
                            continue;
                        }
                        float weight = spatialWeight * normalWeight * depthWeight * edgeReject;

                        aoSum += sampleAo * weight;
                        weightSum += weight;
                    }
                }

                float blurredAo = (weightSum > 0.0) ? (aoSum / weightSum) : centerAo;
                blurredAo = min(centerAo, blurredAo);
                FragColor = vec4(vec3(clamp(blurredAo, 0.0, 1.0)), 1.0);
            }
        )";

        bool ensureCompiled(){
            if(!rawShader || !blurShader){
                return false;
            }
            if(rawShader->getID() != 0 && blurShader->getID() != 0){
                return true;
            }
            if(compileAttempted){
                return false;
            }

            compileAttempted = true;
            bool ok = true;
            if(rawShader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile deferred SSAO raw shader:\n%s", rawShader->getLog().c_str());
                ok = false;
            }
            if(blurShader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile deferred SSAO blur shader:\n%s", blurShader->getLog().c_str());
                ok = false;
            }
            return ok;
        }

        bool ensureTarget(PFrameBuffer& buffer, int width, int height){
            if(buffer &&
               buffer->getWidth() == width &&
               buffer->getHeight() == height &&
               buffer->getTexture()){
                return true;
            }

            buffer = FrameBuffer::Create(width, height);
            if(!buffer){
                return false;
            }
            buffer->attachTexture(Texture::CreateRenderTarget(width, height, GL_R16F, GL_RED, GL_FLOAT));
            return buffer->getTexture() && buffer->validate();
        }

        bool ensureTargets(int width, int height){
            if(width <= 0 || height <= 0){
                return false;
            }
            if(!ensureTarget(rawAoFbo, width, height)){
                return false;
            }
            return ensureTarget(blurAoFbo, width, height);
        }

        void initializeKernel(){
            if(kernelInitialized){
                return;
            }

            std::mt19937 rng(1337u);
            std::uniform_real_distribution<float> random01(0.0f, 1.0f);
            std::uniform_real_distribution<float> random11(-1.0f, 1.0f);

            for(int i = 0; i < MAX_KERNEL_SAMPLES; ++i){
                Math3D::Vec3 sample(random11(rng), random11(rng), random01(rng));
                float len = sample.length();
                if(len > 1e-5f){
                    sample = sample * (1.0f / len);
                }else{
                    sample = Math3D::Vec3(0.0f, 0.0f, 1.0f);
                }

                sample = sample * random01(rng);
                const float t = static_cast<float>(i) / static_cast<float>(MAX_KERNEL_SAMPLES);
                const float scale = Math3D::Lerp(0.1f, 1.0f, t * t);
                kernelSamples[i] = sample * scale;
            }
            kernelInitialized = true;
        }

        void uploadKernelUniforms(const std::shared_ptr<ShaderProgram>& shaderProgram){
            if(!shaderProgram){
                return;
            }
            for(int i = 0; i < MAX_KERNEL_SAMPLES; ++i){
                shaderProgram->setUniformFast(
                    "u_samples[" + std::to_string(i) + "]",
                    Uniform<Math3D::Vec3>(kernelSamples[i])
                );
            }
        }

        void drawFullscreenPass(const std::shared_ptr<ShaderProgram>& shaderProgram, const std::shared_ptr<ModelPart>& quad){
            static const Math3D::Mat4 IDENTITY;
            shaderProgram->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
            shaderProgram->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
            shaderProgram->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));
            quad->draw(IDENTITY, IDENTITY, IDENTITY);
        }

    public:
        DeferredSSAO(){
            rawShader = std::make_shared<ShaderProgram>();
            rawShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            rawShader->setFragmentShader(SSAO_RAW_FRAG_SHADER);

            blurShader = std::make_shared<ShaderProgram>();
            blurShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            blurShader->setFragmentShader(SSAO_BLUR_FRAG_SHADER);
        }

        bool renderAoMap(int width,
                         int height,
                         const std::shared_ptr<ModelPart>& quad,
                         PTexture normalTexture,
                         PTexture positionTexture,
                         const Math3D::Mat4& viewMatrix,
                         const Math3D::Mat4& projectionMatrix,
                         const DeferredSSAOSettings& settings){
            if(width <= 0 || height <= 0 || !quad || !normalTexture || !positionTexture){
                return false;
            }
            if(!ensureCompiled() || !ensureTargets(width, height)){
                return false;
            }

            initializeKernel();

            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);

            const Math3D::Vec2 texelSize(
                1.0f / static_cast<float>(width),
                1.0f / static_cast<float>(height)
            );
            const int effectiveSamples = Math3D::Clamp(settings.sampleCount, 4, MAX_KERNEL_SAMPLES);

            rawAoFbo->bind();
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            rawShader->bind();
            rawShader->setUniformFast("normalTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(normalTexture, 0)));
            rawShader->setUniformFast("positionTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(positionTexture, 1)));
            rawShader->setUniformFast("u_viewMatrix", Uniform<Math3D::Mat4>(viewMatrix));
            rawShader->setUniformFast("u_projMatrix", Uniform<Math3D::Mat4>(projectionMatrix));
            rawShader->setUniformFast("u_kernelSize", Uniform<int>(effectiveSamples));
            rawShader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(texelSize));
            rawShader->setUniformFast("u_radiusPx", Uniform<float>(Math3D::Clamp(settings.radiusPx, 0.25f, 8.0f)));
            rawShader->setUniformFast("u_depthRadius", Uniform<float>(Math3D::Clamp(settings.depthRadius, 0.0005f, 0.5f)));
            rawShader->setUniformFast("u_bias", Uniform<float>(Math3D::Clamp(settings.bias, 0.0f, 0.05f)));
            uploadKernelUniforms(rawShader);
            drawFullscreenPass(rawShader, quad);
            rawAoFbo->unbind();

            blurAoFbo->bind();
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            blurShader->bind();
            blurShader->setUniformFast("rawAoTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(rawAoFbo->getTexture(), 0)));
            blurShader->setUniformFast("normalTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(normalTexture, 1)));
            blurShader->setUniformFast("positionTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(positionTexture, 2)));
            blurShader->setUniformFast("u_viewMatrix", Uniform<Math3D::Mat4>(viewMatrix));
            blurShader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(texelSize));
            blurShader->setUniformFast("u_blurRadiusPx", Uniform<float>(Math3D::Clamp(settings.blurRadiusPx, 0.25f, 8.0f)));
            blurShader->setUniformFast("u_blurSharpness", Uniform<float>(Math3D::Clamp(settings.blurSharpness, 0.25f, 8.0f)));
            drawFullscreenPass(blurShader, quad);
            blurAoFbo->unbind();
            return true;
        }

        PTexture getRawAoTexture() const {
            return rawAoFbo ? rawAoFbo->getTexture() : nullptr;
        }

        PTexture getBlurAoTexture() const {
            return blurAoFbo ? blurAoFbo->getTexture() : nullptr;
        }
};

#endif // DEFERREDSSAO_H
