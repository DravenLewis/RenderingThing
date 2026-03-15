#ifndef DEFERREDSCREENGI_H
#define DEFERREDSCREENGI_H

#include <array>
#include <memory>
#include <random>
#include <string>

#include "Foundation/Logging/Logbot.h"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Core/FrameBuffer.h"
#include "Rendering/Core/Graphics.h"
#include "Rendering/Geometry/ModelPart.h"
#include "Rendering/Lighting/DeferredSSAO.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Rendering/Textures/CubeMap.h"
#include "Rendering/Textures/Texture.h"

class DeferredScreenGI {
    private:
        static constexpr int MAX_KERNEL_SAMPLES = 64;
        static constexpr float GI_RESOLUTION_SCALE = 0.5f;
        static constexpr int GI_MAX_EFFECTIVE_SAMPLES = 8;

        std::shared_ptr<ShaderProgram> rawShader;
        std::shared_ptr<ShaderProgram> blurShader;
        bool compileAttempted = false;
        bool kernelInitialized = false;
        std::array<Math3D::Vec3, MAX_KERNEL_SAMPLES> kernelSamples{};
        PFrameBuffer rawGiFbo = nullptr;
        PFrameBuffer blurGiFbo = nullptr;
        GLuint rawKernelProgramId = 0;

        const std::string GI_RAW_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D normalTexture;
            uniform sampler2D depthTexture;
            uniform sampler2D directLightTexture;
            uniform samplerCube envTexture;
            uniform mat4 u_viewMatrix;
            uniform mat4 u_invViewMatrix;
            uniform mat4 u_projMatrix;
            uniform mat4 u_invProjMatrix;
            uniform vec3 u_samples[64];
            uniform int u_kernelSize;
            uniform int u_useEnvMap;
            uniform vec2 u_texelSize;
            uniform float u_radiusPx;
            uniform float u_depthRadius;
            uniform float u_bias;
            uniform float u_giBoost;

            vec3 safeNormalize(vec3 v){
                float lenV = length(v);
                return (lenV > 1e-5) ? (v / lenV) : vec3(0.0, 0.0, 1.0);
            }

            vec3 reconstructViewPosition(vec2 uv){
                float depth = texture(depthTexture, uv).r;
                vec4 clip = vec4((uv * 2.0) - 1.0, (depth * 2.0) - 1.0, 1.0);
                vec4 view = u_invProjMatrix * clip;
                return view.xyz / max(abs(view.w), 1e-5);
            }

            vec3 worldToViewNormal(vec3 worldNormal){
                return safeNormalize(mat3(u_viewMatrix) * worldNormal);
            }

            vec3 viewToWorldDirection(vec3 viewDir){
                return safeNormalize((u_invViewMatrix * vec4(viewDir, 0.0)).xyz);
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

            vec3 sampleEnvironment(vec3 sampleDirView){
                if(u_useEnvMap == 0){
                    return vec3(0.0);
                }
                vec3 sampleDirWorld = viewToWorldDirection(sampleDirView);
                return texture(envTexture, sampleDirWorld).rgb;
            }

            void main() {
                vec3 normalWorld = texture(normalTexture, TexCoords).xyz;
                if(length(normalWorld) < 1e-4){
                    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
                    return;
                }

                vec3 fragPosView = reconstructViewPosition(TexCoords);
                vec3 normalView = worldToViewNormal(normalWorld);

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
                float radius = max(u_depthRadius * 32.0, pixelWorldRadius * 8.0);
                radius = clamp(radius, 0.03, 4.0);
                float bias = max(u_bias, radius * 0.015);
                float minDistance = max(radius * 0.08, 0.01);

                int kernelSize = clamp(u_kernelSize, 1, 64);
                vec3 giAccum = vec3(0.0);
                float weightSum = 0.0;
                vec3 envAccum = vec3(0.0);
                float envWeightSum = 0.0;
                for(int i = 0; i < kernelSize; ++i){
                    vec3 sampleVectorView = (TBN * u_samples[i]) * radius;
                    float probeDistance = length(sampleVectorView);
                    if(probeDistance <= minDistance){
                        continue;
                    }

                    vec3 sampleRayView = sampleVectorView / probeDistance;
                    float receiverFacing = dot(normalView, sampleRayView);
                    if(receiverFacing <= 0.0){
                        continue;
                    }

                    float hemiWeight = receiverFacing * max(u_samples[i].z, 0.05);
                    if(u_useEnvMap != 0){
                        vec3 envRadiance = sampleEnvironment(sampleRayView);
                        envAccum += envRadiance * hemiWeight;
                        envWeightSum += hemiWeight;
                    }

                    vec3 sampleProbeView = fragPosView + sampleVectorView;
                    vec4 offset = u_projMatrix * vec4(sampleProbeView, 1.0);
                    if(abs(offset.w) <= 1e-5){
                        continue;
                    }

                    offset.xyz /= offset.w;
                    vec2 sampleUv = offset.xy * 0.5 + 0.5;
                    if(sampleUv.x <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y <= 0.0 || sampleUv.y >= 1.0){
                        continue;
                    }

                    vec3 sampleDirect = texture(directLightTexture, sampleUv).rgb;
                    float sourceEnergy = dot(sampleDirect, vec3(0.2126, 0.7152, 0.0722));
                    if(sourceEnergy <= 1e-4){
                        continue;
                    }

                    vec3 sampleNormalWorld = texture(normalTexture, sampleUv).xyz;
                    if(length(sampleNormalWorld) < 1e-4){
                        continue;
                    }

                    vec3 samplePosView = reconstructViewPosition(sampleUv);
                    vec3 sampleDeltaView = samplePosView - fragPosView;
                    float sampleDistance = length(sampleDeltaView);
                    if(sampleDistance <= minDistance || sampleDistance > radius){
                        continue;
                    }

                    vec3 sampleDir = sampleDeltaView / sampleDistance;
                    receiverFacing = dot(normalView, sampleDir);
                    if(receiverFacing <= 0.0){
                        continue;
                    }

                    vec3 sampleNormalView = worldToViewNormal(sampleNormalWorld);
                    float sampleFacing = dot(sampleNormalView, -sampleDir);
                    if(sampleFacing <= 0.0){
                        continue;
                    }

                    float planeDistance = abs(dot(normalView, sampleDeltaView));
                    float similarNormal = dot(normalView, sampleNormalView);
                    if(similarNormal > 0.96 && planeDistance < (radius * 0.10)){
                        continue;
                    }

                    float distanceWeight = 1.0 - smoothstep(minDistance, radius, sampleDistance);
                    distanceWeight *= distanceWeight;
                    float horizon = clamp((receiverFacing - (bias / sampleDistance)), 0.0, 1.0);
                    float emitter = clamp(sampleFacing, 0.0, 1.0);
                    float weight = horizon * emitter * distanceWeight;

                    giAccum += sampleDirect * weight;
                    weightSum += weight;
                }

                vec3 gi = vec3(0.0);
                float screenCoverage = clamp((weightSum / float(kernelSize)) * 2.5, 0.0, 1.0);
                if(weightSum > 0.0){
                    vec3 meanRadiance = giAccum / weightSum;
                    gi += meanRadiance * screenCoverage * max(u_giBoost, 0.0) * 2.0;
                }
                if(envWeightSum > 0.0){
                    vec3 envRadiance = envAccum / envWeightSum;
                    float offscreenBlend = mix(0.18, 0.70, 1.0 - screenCoverage);
                    gi += envRadiance * max(u_giBoost, 0.0) * offscreenBlend;
                }

                FragColor = vec4(max(gi, vec3(0.0)), 1.0);
            }
        )";

        const std::string GI_BLUR_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D rawGiTexture;
            uniform sampler2D normalTexture;
            uniform sampler2D depthTexture;
            uniform mat4 u_viewMatrix;
            uniform mat4 u_invProjMatrix;
            uniform vec2 u_texelSize;
            uniform float u_blurRadiusPx;
            uniform float u_blurSharpness;

            vec3 safeNormalize(vec3 v){
                float lenV = length(v);
                return (lenV > 1e-5) ? (v / lenV) : vec3(0.0, 0.0, 1.0);
            }

            vec3 reconstructViewPosition(vec2 uv){
                float depth = texture(depthTexture, uv).r;
                vec4 clip = vec4((uv * 2.0) - 1.0, (depth * 2.0) - 1.0, 1.0);
                vec4 view = u_invProjMatrix * clip;
                return view.xyz / max(abs(view.w), 1e-5);
            }

            vec3 worldToViewNormal(vec3 worldNormal){
                return safeNormalize(mat3(u_viewMatrix) * worldNormal);
            }

            void main() {
                vec3 centerGi = max(texture(rawGiTexture, TexCoords).rgb, vec3(0.0));
                vec3 centerNormalWorld = texture(normalTexture, TexCoords).xyz;
                if(length(centerNormalWorld) < 1e-4){
                    FragColor = vec4(centerGi, 1.0);
                    return;
                }

                vec3 centerNormalView = worldToViewNormal(centerNormalWorld);
                vec3 centerPosView = reconstructViewPosition(TexCoords);

                float blurRadius = max(u_blurRadiusPx, 0.5);
                float sharpness = clamp(u_blurSharpness, 0.25, 8.0);
                float sharpnessNorm = smoothstep(0.25, 8.0, sharpness);
                float spatialSigma = max(blurRadius * 0.9, 1.0);
                float depthSigma = max(abs(centerPosView.z) * mix(0.035, 0.014, sharpnessNorm), 0.008);

                vec3 giSum = vec3(0.0);
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

                        vec3 sampleGi = max(texture(rawGiTexture, sampleUv).rgb, vec3(0.0));
                        vec3 sampleNormalView = worldToViewNormal(sampleNormalWorld);
                        vec3 samplePosView = reconstructViewPosition(sampleUv);

                        float kernelLen = length(kernelOffset);
                        float spatialWeight = exp(-(kernelLen * kernelLen) / max(2.0 * spatialSigma * spatialSigma, 0.0001));
                        float normalWeight = pow(max(dot(centerNormalView, sampleNormalView), 0.0), mix(1.0, 4.0, sharpnessNorm));
                        normalWeight = mix(0.15, 1.0, normalWeight);
                        float viewDepthDiff = abs(samplePosView.z - centerPosView.z);
                        float planeDiff = abs(dot(centerNormalView, samplePosView - centerPosView));
                        float depthMetric = max(planeDiff, viewDepthDiff * 0.5);
                        float depthWeight = exp(-depthMetric / max(depthSigma, 0.0001));
                        float weight = spatialWeight * normalWeight * depthWeight;

                        giSum += sampleGi * weight;
                        weightSum += weight;
                    }
                }

                vec3 blurredGi = (weightSum > 0.0) ? (giSum / weightSum) : centerGi;
                FragColor = vec4(max(blurredGi, vec3(0.0)), 1.0);
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
                LogBot.Log(LOG_ERRO, "Failed to compile deferred GI raw shader:\n%s", rawShader->getLog().c_str());
                ok = false;
            }
            if(blurShader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile deferred GI blur shader:\n%s", blurShader->getLog().c_str());
                ok = false;
            }
            return ok;
        }

        static int scaleDimension(int fullSize, float scale){
            if(fullSize <= 0){
                return 0;
            }
            const float clampedScale = Math3D::Clamp(scale, 0.25f, 1.0f);
            return Math3D::Max(1, static_cast<int>((static_cast<float>(fullSize) * clampedScale) + 0.5f));
        }

        static float computeResolutionScale(int fullWidth, int fullHeight, int passWidth, int passHeight){
            if(fullWidth <= 0 || fullHeight <= 0 || passWidth <= 0 || passHeight <= 0){
                return 1.0f;
            }
            const float scaleX = static_cast<float>(passWidth) / static_cast<float>(fullWidth);
            const float scaleY = static_cast<float>(passHeight) / static_cast<float>(fullHeight);
            return Math3D::Clamp(Math3D::Min(scaleX, scaleY), 0.25f, 1.0f);
        }

        static void configureTextureFilter(PTexture texture, GLint filter){
            if(!texture || texture->getID() == 0){
                return;
            }
            glBindTexture(GL_TEXTURE_2D, texture->getID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        bool ensureTarget(PFrameBuffer& buffer, int width, int height, GLint filter){
            if(buffer &&
               buffer->getWidth() == width &&
               buffer->getHeight() == height &&
               buffer->getTexture()){
                configureTextureFilter(buffer->getTexture(), filter);
                return true;
            }

            buffer = FrameBuffer::Create(width, height);
            if(!buffer){
                return false;
            }
            buffer->attachTexture(Texture::CreateRenderTarget(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT));
            configureTextureFilter(buffer->getTexture(), filter);
            return buffer->getTexture() && buffer->validate();
        }

        bool ensureTargets(int width, int height){
            if(width <= 0 || height <= 0){
                return false;
            }
            if(!ensureTarget(rawGiFbo, width, height, GL_NEAREST)){
                return false;
            }
            return ensureTarget(blurGiFbo, width, height, GL_LINEAR);
        }

        void initializeKernel(){
            if(kernelInitialized){
                return;
            }

            std::mt19937 rng(424242u);
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
                const float scale = Math3D::Lerp(0.08f, 1.0f, t * t);
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
        static int ComputeTargetWidth(int fullWidth){
            return scaleDimension(fullWidth, GI_RESOLUTION_SCALE);
        }

        static int ComputeTargetHeight(int fullHeight){
            return scaleDimension(fullHeight, GI_RESOLUTION_SCALE);
        }

        DeferredScreenGI(){
            rawShader = std::make_shared<ShaderProgram>();
            rawShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            rawShader->setFragmentShader(GI_RAW_FRAG_SHADER);

            blurShader = std::make_shared<ShaderProgram>();
            blurShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            blurShader->setFragmentShader(GI_BLUR_FRAG_SHADER);
        }

        bool renderGiMap(int width,
                         int height,
                         const std::shared_ptr<ModelPart>& quad,
                         PTexture normalTexture,
                         PTexture depthTexture,
                         PTexture directLightTexture,
                         PCubeMap envMap,
                         const Math3D::Mat4& viewMatrix,
                         const Math3D::Mat4& inverseViewMatrix,
                         const Math3D::Mat4& projectionMatrix,
                         const DeferredSSAOSettings& settings){
            if(width <= 0 || height <= 0 || !quad || !normalTexture || !depthTexture || !directLightTexture){
                return false;
            }
            const int giWidth = ComputeTargetWidth(width);
            const int giHeight = ComputeTargetHeight(height);
            if(!ensureCompiled() || !ensureTargets(giWidth, giHeight)){
                return false;
            }

            initializeKernel();

            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);

            const Math3D::Vec2 texelSize(
                1.0f / static_cast<float>(giWidth),
                1.0f / static_cast<float>(giHeight)
            );
            const float resolutionScale = computeResolutionScale(width, height, giWidth, giHeight);
            const int effectiveSamples = Math3D::Clamp(
                Math3D::Min(settings.sampleCount, GI_MAX_EFFECTIVE_SAMPLES),
                4,
                MAX_KERNEL_SAMPLES
            );
            const float scaledRadiusPx = Math3D::Clamp(settings.radiusPx, 0.25f, 8.0f) * resolutionScale;
            const float scaledBlurRadiusPx = Math3D::Clamp(settings.blurRadiusPx, 0.25f, 8.0f) * resolutionScale;

            rawGiFbo->bind();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            rawShader->bind();
            rawShader->setUniformFast("normalTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(normalTexture, 0)));
            rawShader->setUniformFast("depthTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(depthTexture, 1)));
            rawShader->setUniformFast("directLightTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(directLightTexture, 2)));
            rawShader->setUniformFast("envTexture", Uniform<GLUniformUpload::CubeMapSlot>(GLUniformUpload::CubeMapSlot(envMap, 3)));
            rawShader->setUniformFast("u_viewMatrix", Uniform<Math3D::Mat4>(viewMatrix));
            rawShader->setUniformFast("u_invViewMatrix", Uniform<Math3D::Mat4>(inverseViewMatrix));
            rawShader->setUniformFast("u_projMatrix", Uniform<Math3D::Mat4>(projectionMatrix));
            rawShader->setUniformFast("u_invProjMatrix", Uniform<Math3D::Mat4>(Math3D::Mat4(glm::inverse(glm::mat4(projectionMatrix)))));
            rawShader->setUniformFast("u_kernelSize", Uniform<int>(effectiveSamples));
            rawShader->setUniformFast("u_useEnvMap", Uniform<int>(envMap ? 1 : 0));
            rawShader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(texelSize));
            rawShader->setUniformFast("u_radiusPx", Uniform<float>(scaledRadiusPx));
            rawShader->setUniformFast("u_depthRadius", Uniform<float>(Math3D::Clamp(settings.depthRadius, 0.0005f, 0.5f)));
            rawShader->setUniformFast("u_bias", Uniform<float>(Math3D::Clamp(settings.bias, 0.0f, 0.05f)));
            rawShader->setUniformFast("u_giBoost", Uniform<float>(Math3D::Clamp(settings.giBoost, 0.0f, 1.0f)));
            if(rawKernelProgramId != rawShader->getID()){
                uploadKernelUniforms(rawShader);
                rawKernelProgramId = rawShader->getID();
            }
            drawFullscreenPass(rawShader, quad);
            rawGiFbo->unbind();

            blurGiFbo->bind();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            blurShader->bind();
            blurShader->setUniformFast("rawGiTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(rawGiFbo->getTexture(), 0)));
            blurShader->setUniformFast("normalTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(normalTexture, 1)));
            blurShader->setUniformFast("depthTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(depthTexture, 2)));
            blurShader->setUniformFast("u_viewMatrix", Uniform<Math3D::Mat4>(viewMatrix));
            blurShader->setUniformFast("u_invProjMatrix", Uniform<Math3D::Mat4>(Math3D::Mat4(glm::inverse(glm::mat4(projectionMatrix)))));
            blurShader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(texelSize));
            blurShader->setUniformFast("u_blurRadiusPx", Uniform<float>(scaledBlurRadiusPx));
            blurShader->setUniformFast("u_blurSharpness", Uniform<float>(Math3D::Clamp(settings.blurSharpness, 0.25f, 8.0f)));
            drawFullscreenPass(blurShader, quad);
            blurGiFbo->unbind();
            return true;
        }

        PTexture getRawGiTexture() const {
            return rawGiFbo ? rawGiFbo->getTexture() : nullptr;
        }

        PTexture getBlurGiTexture() const {
            return blurGiFbo ? blurGiFbo->getTexture() : nullptr;
        }
};

#endif // DEFERREDSCREENGI_H
