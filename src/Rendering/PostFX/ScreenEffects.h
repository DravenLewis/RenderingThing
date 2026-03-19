/**
 * @file src/Rendering/PostFX/ScreenEffects.h
 * @brief Declarations for ScreenEffects.
 */

#ifndef SCREENEFFECTS_H
#define SCREENEFFECTS_H

#include "Rendering/Core/Graphics.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Foundation/Math/Color.h"
#include "Foundation/Logging/Logbot.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

/// @brief Enumerates values for AntiAliasingPreset.
enum class AntiAliasingPreset {
    Off = 0,
    FXAA_Low,
    FXAA_Medium,
    FXAA_High
};

/// @brief Represents the GrayscaleEffect type.
class GrayscaleEffect : public Graphics::PostProcessing::PostProcessingEffect{
    private:
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;

        const std::string GRAYSCALE_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;
            uniform sampler2D screenTexture;
            
            /**
             * @brief Executes the main shader pass.
             */
            void main() {
                vec4 col = texture(screenTexture, TexCoords);
                float avg = 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
                FragColor = vec4(avg, avg, avg, col.a);
            }
        )";

        /**
         * @brief Checks whether ensure compiled.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ensureCompiled(){
            if(!shader){
                return false;
            }
            if(shader->getID() != 0){
                return true;
            }
            if(compileAttempted){
                return false;
            }
            compileAttempted = true;
            if(shader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile grayscale effect shader:\n%s", shader->getLog().c_str());
                return false;
            }
            return true;
        }

    public:

        /**
         * @brief Constructs a new GrayscaleEffect instance.
         */
        GrayscaleEffect(){
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(GRAYSCALE_SHADER);
        }

        /**
         * @brief Applies current settings.
         * @param tex Value for tex.
         * @param depthTex Value for depth tex.
         * @param outFbo Output value for fbo.
         * @param quad Value for quad.
         * @return True when the operation succeeds; otherwise false.
         */
        bool apply(PTexture tex, PTexture depthTex, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {
            (void)depthTex;
            if(!tex || !outFbo || !quad){
                return false;
            }
            if(!ensureCompiled()){
                return false;
            }

            outFbo->bind();
            glDisable(GL_DEPTH_TEST);

            shader->bind();
            Uniform<PTexture> u_tex(tex);
            shader->setUniform("screenTexture", u_tex);
            static const Math3D::Mat4 IDENTITY;
            shader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));

            quad->draw(IDENTITY, IDENTITY, IDENTITY);

            outFbo->unbind();
            return true;
        }

        /**
         * @brief Creates a new object.
         * @return Result of this operation.
         */
        static Graphics::PostProcessing::PPostProcessingEffect New(){
            return std::make_shared<GrayscaleEffect>();
        }
};

/// @brief Deprecated legacy screen-space SSAO post effect.
/// @deprecated Use DeferredSSAO or RobustSSAOEffect instead.
class SSAOEffect : public Graphics::PostProcessing::PostProcessingEffect {
    private:
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;
        const std::string SSAO_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D screenTexture;
            uniform sampler2D depthTexture;
            uniform sampler2D normalTexture;
            uniform sampler2D positionTexture;
            uniform vec2 u_texelSize;
            uniform float u_radiusPx;
            uniform float u_depthRadius;
            uniform float u_bias;
            uniform float u_intensity;
            uniform float u_giBoost;
            uniform int u_sampleCount;
            uniform int u_debugView;
            uniform int u_hasSceneNormalTexture;
            uniform int u_hasScenePositionTexture;
            uniform mat4 u_viewMatrix;
            uniform mat4 u_projMatrix;
            uniform vec2 u_projScale;
            uniform vec2 u_orthoViewSize;
            uniform int u_isOrtho;
            uniform float u_nearPlane;
            uniform float u_farPlane;

            const vec2 kPoisson[16] = vec2[](
                vec2(-0.94201624, -0.39906216),
                vec2( 0.94558609, -0.76890725),
                vec2(-0.09418410, -0.92938870),
                vec2( 0.34495938,  0.29387760),
                vec2(-0.91588581,  0.45771432),
                vec2(-0.81544232, -0.87912464),
                vec2(-0.38277543,  0.27676845),
                vec2( 0.97484398,  0.75648379),
                vec2( 0.44323325, -0.97511554),
                vec2( 0.53742981, -0.47373420),
                vec2(-0.26496911, -0.41893023),
                vec2( 0.79197514,  0.19090188),
                vec2(-0.24188840,  0.99706507),
                vec2(-0.81409955,  0.91437590),
                vec2( 0.19984126,  0.78641367),
                /**
                 * @brief Builds a 2D vector.
                 */
                vec2( 0.14383161, -0.14100790)
            );

            const vec2 kPoisson[16] = vec2[](
                vec2(-0.94201624, -0.39906216),
                vec2( 0.94558609, -0.76890725),
                vec2(-0.09418410, -0.92938870),
                vec2( 0.34495938,  0.29387760),
                vec2(-0.91588581,  0.45771432),
                vec2(-0.81544232, -0.87912464),
                vec2(-0.38277543,  0.27676845),
                vec2( 0.97484398,  0.75648379),
                vec2( 0.44323325, -0.97511554),
                vec2( 0.53742981, -0.47373420),
                vec2(-0.26496911, -0.41893023),
                vec2( 0.79197514,  0.19090188),
                vec2(-0.24188840,  0.99706507),
                vec2(-0.81409955,  0.91437590),
                vec2( 0.19984126,  0.78641367),
                vec2( 0.14383161, -0.14100790)
            );

            /**
             * @brief Converts device depth to linear depth.
             * @param depth Value for depth.
             * @return Computed numeric result.
             */
            float linearizeDepth(float depth){
                float z = depth * 2.0 - 1.0;
                return (2.0 * u_nearPlane * u_farPlane) / max((u_farPlane + u_nearPlane) - (z * (u_farPlane - u_nearPlane)), 0.0001);
            }

            /**
             * @brief Checks whether h12.
             * @param p Value for p.
             * @return Computed numeric result.
             */
            float hash12(vec2 p){
                vec3 p3 = fract(vec3(p.xyx) * 0.1031);
                p3 += dot(p3, p3.yzx + 33.33);
                return fract((p3.x + p3.y) * p3.z);
            }

            const float kSkyDepthCutoff = 0.9995;

            ivec2 snapUvToDepthCoord(vec2 uv){
                vec2 texSize = vec2(textureSize(depthTexture, 0));
                vec2 clampedUv = clamp(uv, vec2(0.0), vec2(0.999999));
                vec2 coord = floor(clampedUv * texSize);
                coord = clamp(coord, vec2(0.0), texSize - vec2(1.0));
                return ivec2(coord);
            }

            vec2 depthCoordToUv(ivec2 coord){
                vec2 texSize = vec2(textureSize(depthTexture, 0));
                return (vec2(coord) + 0.5) / texSize;
            }

            vec2 snapUvToDepthTexel(vec2 uv){
                return depthCoordToUv(snapUvToDepthCoord(uv));
            }

            float sampleDepthNearest(vec2 uv){
                return texelFetch(depthTexture, snapUvToDepthCoord(uv), 0).r;
            }

            vec3 sampleSceneNormal(vec2 uv){
                vec3 N = texelFetch(normalTexture, snapUvToDepthCoord(uv), 0).xyz;
                float lenN = length(N);
                return (lenN > 1e-5) ? (N / lenN) : vec3(0.0, 0.0, 1.0);
            }

            vec3 sampleScenePosition(vec2 uv){
                return texelFetch(positionTexture, snapUvToDepthCoord(uv), 0).xyz;
            }

            mat3 buildBasis(vec3 N, float angle){
                vec3 tangentSeed = (abs(N.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
                vec3 T = normalize(cross(tangentSeed, N));
                vec3 B = cross(N, T);
                float s = sin(angle);
                float c = cos(angle);
                vec3 rotatedT = (T * c) + (B * s);
                vec3 rotatedB = cross(N, rotatedT);
                return mat3(rotatedT, rotatedB, N);
            }

            vec3 reconstructViewPos(vec2 uv, float depthRaw){
                float linearDepth = linearizeDepth(depthRaw);
                vec2 ndc = uv * 2.0 - 1.0;
                if(u_isOrtho != 0){
                    vec2 halfSize = u_orthoViewSize * 0.5;
                    return vec3(ndc.x * halfSize.x, ndc.y * halfSize.y, -linearDepth);
                }
                return vec3(
                    ndc.x * linearDepth * u_projScale.x,
                    ndc.y * linearDepth * u_projScale.y,
                    -linearDepth
                );
            }

            vec3 reconstructViewNormal(vec2 uv, float centerDepthRaw, vec3 centerPos){
                float centerDepth = linearizeDepth(centerDepthRaw);
                vec2 leftUv = snapUvToDepthTexel(uv - vec2(u_texelSize.x, 0.0));
                vec2 rightUv = snapUvToDepthTexel(uv + vec2(u_texelSize.x, 0.0));
                vec2 downUv = snapUvToDepthTexel(uv - vec2(0.0, u_texelSize.y));
                vec2 upUv = snapUvToDepthTexel(uv + vec2(0.0, u_texelSize.y));

                float leftDepthRaw = sampleDepthNearest(leftUv);
                float rightDepthRaw = sampleDepthNearest(rightUv);
                float downDepthRaw = sampleDepthNearest(downUv);
                float upDepthRaw = sampleDepthNearest(upUv);

                float leftDepth = (leftDepthRaw >= kSkyDepthCutoff) ? centerDepth : linearizeDepth(leftDepthRaw);
                float rightDepth = (rightDepthRaw >= kSkyDepthCutoff) ? centerDepth : linearizeDepth(rightDepthRaw);
                float downDepth = (downDepthRaw >= kSkyDepthCutoff) ? centerDepth : linearizeDepth(downDepthRaw);
                float upDepth = (upDepthRaw >= kSkyDepthCutoff) ? centerDepth : linearizeDepth(upDepthRaw);

                vec3 leftPos = reconstructViewPos(leftUv, (leftDepthRaw >= kSkyDepthCutoff) ? centerDepthRaw : leftDepthRaw);
                vec3 rightPos = reconstructViewPos(rightUv, (rightDepthRaw >= kSkyDepthCutoff) ? centerDepthRaw : rightDepthRaw);
                vec3 downPos = reconstructViewPos(downUv, (downDepthRaw >= kSkyDepthCutoff) ? centerDepthRaw : downDepthRaw);
                vec3 upPos = reconstructViewPos(upUv, (upDepthRaw >= kSkyDepthCutoff) ? centerDepthRaw : upDepthRaw);

                vec3 dx = (abs(leftDepth - centerDepth) < abs(rightDepth - centerDepth))
                    ? (centerPos - leftPos)
                    : (rightPos - centerPos);
                vec3 dy = (abs(downDepth - centerDepth) < abs(upDepth - centerDepth))
                    ? (centerPos - downPos)
                    : (upPos - centerPos);
                vec3 rawN = cross(dx, dy);
                float rawLen = length(rawN);
                if(rawLen < 1e-4){
                    return vec3(0.0, 0.0, 1.0);
                }
                vec3 N = rawN / rawLen;
                if(N.z < 0.0){
                    N = -N;
                }
                return N;
            }

            /**
             * @brief Executes the main shader pass.
             */
            void main() {
                vec4 base = texture(screenTexture, TexCoords);
                float materialAo = clamp(base.a, 0.0, 1.0);
                vec2 centerUv = snapUvToDepthTexel(TexCoords);
                float centerDepthRaw = sampleDepthNearest(centerUv);
                // Treat near-far depth as background/sky and skip AO to avoid horizon halos.
                if(centerDepthRaw >= kSkyDepthCutoff){
                    if(u_debugView != 0){
                        FragColor = vec4(vec3(0.0), 1.0);
                    }else{
                        FragColor = base;
                    }
                    return;
                }
                float centerDepth = linearizeDepth(centerDepthRaw);
                bool useSceneGeometry = (u_hasSceneNormalTexture != 0) && (u_hasScenePositionTexture != 0);
                vec3 centerPos = useSceneGeometry
                    ? sampleScenePosition(centerUv)
                    : reconstructViewPos(centerUv, centerDepthRaw);
                vec3 centerNormal = useSceneGeometry
                    ? sampleSceneNormal(centerUv)
                    : reconstructViewNormal(centerUv, centerDepthRaw, centerPos);

                float radiusPx = max(u_radiusPx, 1.0);
                float depthRadius = max(u_depthRadius, 0.00005);
                int count = clamp(u_sampleCount, 1, 16);
                // Use pixel-stable noise to avoid UV-space stripe patterns on large flat surfaces.
                float angle = hash12(floor(gl_FragCoord.xy)) * 6.2831853;
                float sinAngle = sin(angle);
                float cosAngle = cos(angle);

                float pixelWorldScale = (u_isOrtho != 0)
                    ? max(u_orthoViewSize.y * u_texelSize.y, 0.0001)
                    : max((2.0 * centerDepth * u_projScale.y * u_texelSize.y), 0.0001);
                // Keep the AO footprint tied to the requested world radius and the true pixel footprint,
                // rather than scaling it again with camera depth.
                float adaptiveDepthRadius = max(depthRadius, pixelWorldScale * 2.0);
                float sampleRadiusView = max(
                    adaptiveDepthRadius * 1.75,
                    pixelWorldScale * radiusPx * 2.0
                );
                float baseBias = max(u_bias, pixelWorldScale * 0.75);
                float normalBias = clamp(max(u_bias * 2.0, pixelWorldScale * 1.25), 0.001, 0.08);

                float occ = 0.0;
                float weightSum = 0.0;
                if(useSceneGeometry){
                    vec3 centerPosVS = (u_viewMatrix * vec4(centerPos, 1.0)).xyz;
                    vec3 centerNormalVS = normalize(mat3(u_viewMatrix) * centerNormal);
                    mat3 sampleBasis = buildBasis(centerNormalVS, angle);
                    float sampleRadiusWorld = max(depthRadius, pixelWorldScale * radiusPx * 1.25);
                    float projectedRadius = max(sampleRadiusWorld, pixelWorldScale);
                    for(int i = 0; i < count; ++i){
                        float ringT = (float(i) + 0.5) / float(count);
                        vec3 kernel = normalize(kHemisphere[i]);
                        // Bias samples outward so most taps probe beyond the current surface footprint.
                        float sampleScale = mix(0.20, 1.0, ringT * ringT);
                        vec3 samplePosVS = centerPosVS + ((sampleBasis * kernel) * sampleRadiusWorld * sampleScale);
                        vec4 clip = u_projMatrix * vec4(samplePosVS, 1.0);
                        if(abs(clip.w) <= 1e-5){
                            continue;
                        }
                        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
                        if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0){
                            continue;
                        }

                        float sampleDepthRaw = sampleDepthNearest(uv);
                        if(sampleDepthRaw >= kSkyDepthCutoff){
                            continue;
                        }

                        vec3 actualPosVS = (u_viewMatrix * vec4(sampleScenePosition(uv), 1.0)).xyz;
                        vec3 deltaVecVS = actualPosVS - centerPosVS;
                        float sampleDistance = length(deltaVecVS);
                        if(sampleDistance <= 1e-4){
                            continue;
                        }
                        vec3 deltaDirVS = deltaVecVS / sampleDistance;
                        float expectedDepth = -samplePosVS.z;
                        float actualDepth = -actualPosVS.z;
                        float depthDelta = expectedDepth - actualDepth;
                        float planeDelta = max(dot(centerNormalVS, deltaVecVS) - baseBias, 0.0);
                        float depthOcc = smoothstep(baseBias, projectedRadius, depthDelta);
                        float planeOcc = smoothstep(0.0, projectedRadius, planeDelta);
                        float angular = max(dot(centerNormalVS, deltaDirVS) - normalBias, 0.0);
                        float sampleOcc = max(depthOcc, planeOcc) * smoothstep(0.05, 0.85, angular);
                        float rangeWeight = 1.0 - smoothstep(projectedRadius * 0.30, projectedRadius * 1.10, sampleDistance);
                        occ += sampleOcc * rangeWeight;
                        weightSum += rangeWeight;
                    }
                }else{
                    for(int i = 0; i < count; ++i){
                        float ringT = (float(i) + 0.5) / float(count);
                        vec2 poisson = kPoisson[i];
                        vec2 rotated = vec2(
                            (poisson.x * cosAngle) - (poisson.y * sinAngle),
                            (poisson.x * sinAngle) + (poisson.y * cosAngle)
                        );

                        for(int mirror = 0; mirror < 2; ++mirror){
                            vec2 direction = (mirror == 0) ? rotated : -rotated;
                            vec2 pixelOffset = direction * radiusPx * ringT;
                            vec2 uv = snapUvToDepthTexel(centerUv + (pixelOffset * u_texelSize));
                            if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0){
                                continue;
                            }
                            float sampleDepthRaw = sampleDepthNearest(uv);
                            if(sampleDepthRaw >= kSkyDepthCutoff){
                                continue;
                            }
                            vec3 samplePos = reconstructViewPos(uv, sampleDepthRaw);
                            vec3 deltaVec = samplePos - centerPos;
                            float sampleDistance = length(deltaVec);
                            if(sampleDistance <= 1e-4){
                                continue;
                            }
                            vec3 deltaDir = deltaVec / sampleDistance;

                            float planeDelta = max(dot(centerNormal, deltaVec) - baseBias, 0.0);
                            float depthOcc = smoothstep(0.0, adaptiveDepthRadius, planeDelta);
                            depthOcc *= 1.0 - smoothstep(sampleRadiusView * 0.25, sampleRadiusView * 1.05, sampleDistance);
                            float geomAngular = smoothstep(0.25, 0.90, dot(centerNormal, deltaDir) - normalBias);
                            float geomOcc = geomAngular * (1.0 - smoothstep(sampleRadiusView * 0.15, sampleRadiusView * 0.90, sampleDistance));
                            float sampleOcc = max(depthOcc, geomOcc * 0.35);
                            float radialBias = 1.0 - smoothstep(0.35, 1.0, ringT);
                            sampleOcc *= mix(1.0, 1.10, radialBias);
                            float rangeWeight = 1.0 - smoothstep(sampleRadiusView * 0.20, sampleRadiusView * 1.10, sampleDistance);
                            occ += sampleOcc * rangeWeight;
                            weightSum += rangeWeight;
                        }
                    }
                }

                occ = clamp(occ / max(weightSum, 0.0001), 0.0, 1.0);
                occ = pow(occ, 0.68);
                float effectiveIntensity = pow(max(u_intensity, 0.0), 0.6);
                float debugOcclusion = clamp(occ * max(u_intensity, 1.0), 0.0, 1.0);
                debugOcclusion = pow(debugOcclusion, 0.60);
                float ssaoFactorRaw = clamp(1.0 - (occ * effectiveIntensity), 0.0, 1.0);
                float combinedAoRaw = clamp(materialAo * ssaoFactorRaw, 0.0, 1.0);
                // Keep a floor so SSAO cannot collapse the whole frame into near-black.
                float ao = clamp(ssaoFactorRaw, 0.35, 1.0);

                float giFactor = (1.0 - ao);
                giFactor = giFactor * giFactor * u_giBoost;
                vec3 giBounce = base.rgb * giFactor;
                if(u_debugView == 1){
                    FragColor = vec4(vec3(combinedAoRaw), 1.0);
                    return;
                }
                if(u_debugView == 2){
                    FragColor = vec4(vec3(debugOcclusion), 1.0);
                    return;
                }
                if(u_debugView == 3){
                    FragColor = vec4(vec3(materialAo), 1.0);
                    return;
                }
                if(u_debugView == 4){
                    vec3 giDebug = clamp(giBounce * 4.0, vec3(0.0), vec3(1.0));
                    FragColor = vec4(giDebug, 1.0);
                    return;
                }
                vec3 finalColor = (base.rgb * ao) + giBounce;
                FragColor = vec4(finalColor, base.a);
            }
        )";

        /**
         * @brief Checks whether ensure compiled.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ensureCompiled(){
            if(!shader){
                return false;
            }
            if(shader->getID() != 0){
                return true;
            }
            if(compileAttempted){
                return false;
            }
            compileAttempted = true;
            if(shader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile SSAO effect shader:\n%s", shader->getLog().c_str());
                return false;
            }
            return true;
        }

    public:
        float radiusPx = 3.0f;
        float depthRadius = 0.025f;
        float bias = 0.001f;
        float intensity = 1.0f;
        float giBoost = 0.12f;
        int sampleCount = 8;
        int debugView = 0; // 0=composited, 1=combined AO, 2=SSAO raw, 3=material AO, 4=GI bounce
        PTexture sceneNormalTex = nullptr;
        PTexture scenePositionTex = nullptr;
        Math3D::Mat4 viewMatrix;
        Math3D::Mat4 projectionMatrix;
        float fovYDegrees = 45.0f;
        float aspect = 1.33f;
        int isOrtho = 0;
        Math3D::Vec2 orthoViewSize = Math3D::Vec2(1.0f, 1.0f);
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;

        /**
         * @brief Constructs a new SSAOEffect instance.
         */
        SSAOEffect() {
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(SSAO_FRAG_SHADER);
        }

        /**
         * @brief Applies current settings.
         * @param tex Value for tex.
         * @param depthTex Value for depth tex.
         * @param outFbo Output value for fbo.
         * @param quad Value for quad.
         * @return True when the operation succeeds; otherwise false.
         */
        bool apply(PTexture tex, PTexture depthTex, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {
            if(!outFbo || !quad || !tex || !depthTex){
                return false;
            }
            if(!ensureCompiled()){
                return false;
            }

            outFbo->bind();
            glDisable(GL_DEPTH_TEST);

            shader->bind();
            shader->setUniformFast("screenTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(tex, 0)));
            shader->setUniformFast("depthTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(depthTex, 1)));
            shader->setUniformFast("normalTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(sceneNormalTex, 2)));
            shader->setUniformFast("positionTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(scenePositionTex, 3)));
            shader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(Math3D::Vec2(1.0f / (float)outFbo->getWidth(), 1.0f / (float)outFbo->getHeight())));
            shader->setUniformFast("u_radiusPx", Uniform<float>(radiusPx));
            shader->setUniformFast("u_depthRadius", Uniform<float>(depthRadius));
            shader->setUniformFast("u_bias", Uniform<float>(bias));
            shader->setUniformFast("u_intensity", Uniform<float>(intensity));
            shader->setUniformFast("u_giBoost", Uniform<float>(giBoost));
            shader->setUniformFast("u_debugView", Uniform<int>(debugView));
            shader->setUniformFast("u_hasSceneNormalTexture", Uniform<int>(sceneNormalTex ? 1 : 0));
            shader->setUniformFast("u_hasScenePositionTexture", Uniform<int>(scenePositionTex ? 1 : 0));
            shader->setUniformFast("u_viewMatrix", Uniform<Math3D::Mat4>(viewMatrix));
            shader->setUniformFast("u_projMatrix", Uniform<Math3D::Mat4>(projectionMatrix));
            float tanHalfFov = std::tan((Math3D::Clamp(fovYDegrees, 1.0f, 179.0f) * 0.5f) * (Math3D::PI / 180.0f));
            shader->setUniformFast("u_projScale", Uniform<Math3D::Vec2>(Math3D::Vec2(tanHalfFov * Math3D::Max(0.0001f, aspect), tanHalfFov)));
            shader->setUniformFast("u_orthoViewSize", Uniform<Math3D::Vec2>(orthoViewSize));
            shader->setUniformFast("u_isOrtho", Uniform<int>(isOrtho));
            int effectiveSamples = sampleCount;
            const int pixelCount = outFbo->getWidth() * outFbo->getHeight();
            if(pixelCount >= (2560 * 1440) && effectiveSamples > 5){
                effectiveSamples = 5;
            }else if(pixelCount >= (1920 * 1080) && effectiveSamples > 6){
                effectiveSamples = 6;
            }
            if(effectiveSamples < 1){
                effectiveSamples = 1;
            }
            shader->setUniformFast("u_sampleCount", Uniform<int>(effectiveSamples));
            shader->setUniformFast("u_nearPlane", Uniform<float>(nearPlane));
            shader->setUniformFast("u_farPlane", Uniform<float>(farPlane));

            static const Math3D::Mat4 IDENTITY;
            shader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));
            quad->draw(IDENTITY, IDENTITY, IDENTITY);
            outFbo->unbind();
            return true;
        }

        /**
         * @brief Creates a new object.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<SSAOEffect> New() {
            return std::make_shared<SSAOEffect>();
        }
};

/// @brief Represents the RobustSSAOEffect type.
class RobustSSAOEffect : public Graphics::PostProcessing::PostProcessingEffect {
    private:
        static constexpr int MAX_KERNEL_SAMPLES = 64;

        std::shared_ptr<ShaderProgram> rawShader;
        std::shared_ptr<ShaderProgram> blurShader;
        std::shared_ptr<ShaderProgram> compositeShader;
        bool compileAttempted = false;
        bool kernelInitialized = false;
        std::array<Math3D::Vec3, MAX_KERNEL_SAMPLES> kernelSamples{};
        PTexture noiseTexture = nullptr;
        PFrameBuffer rawAoFbo = nullptr;
        PFrameBuffer blurAoFbo = nullptr;

        const std::string SSAO_RAW_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D normalTexture;
            uniform sampler2D positionTexture;
            uniform sampler2D noiseTexture;
            uniform mat4 u_viewMatrix;
            uniform mat4 u_projMatrix;
            uniform vec3 u_samples[64];
            uniform int u_kernelSize;
            uniform vec2 u_noiseScale;
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

            void main() {
                vec3 normalWorld = texture(normalTexture, TexCoords).xyz;
                if(length(normalWorld) < 1e-4){
                    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
                    return;
                }

                vec3 fragPosView = worldToViewPosition(texture(positionTexture, TexCoords).xyz);
                vec3 geomNormalView = geometricNormalFromViewPos(fragPosView);
                vec3 normalFromGBufferView = worldToViewNormal(normalWorld);
                vec3 normalView = safeNormalize(mix(geomNormalView, normalFromGBufferView, 0.35));

                vec3 randomVec = safeNormalize(texture(noiseTexture, TexCoords * u_noiseScale).xyz);
                vec3 tangent = randomVec - normalView * dot(randomVec, normalView);
                tangent = (length(tangent) > 1e-5) ? normalize(tangent) : vec3(1.0, 0.0, 0.0);
                vec3 bitangent = safeNormalize(cross(normalView, tangent));
                mat3 TBN = mat3(tangent, bitangent, normalView);

                float projScaleY = max(abs(u_projMatrix[1][1]), 0.0001);
                float viewDepth = max(abs(fragPosView.z), 0.001);
                float pixelWorldRadius = (2.0 * viewDepth * u_texelSize.y / projScaleY) * max(u_radiusPx, 0.25);
                float radius = max(u_depthRadius * 4.0, pixelWorldRadius * 2.0);
                radius = clamp(radius, 0.01, 0.75);
                // Keep the projected hemisphere broad enough to find occluders,
                // but only normalize over taps that stay in the fragment's local depth neighborhood.
                float nearbyDepthRadius = clamp(max(u_depthRadius, pixelWorldRadius), 0.005, radius);
                float bias = max(u_bias, radius * 0.02);

                int kernelSize = clamp(u_kernelSize, 1, 64);
                float occlusion = 0.0;
                float nearbyValidSamples = 0.0;
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
                    float rangeDelta = abs(fragPosView.z - sampleScenePosView.z);
                    if(rangeDelta > nearbyDepthRadius){
                        continue;
                    }
                    float occluded = (sampleScenePosView.z >= (samplePosView.z + bias)) ? 1.0 : 0.0;
                    occlusion += occluded;
                    nearbyValidSamples += 1.0;
                }

                float ao = 1.0 - ((nearbyValidSamples > 0.0) ? (occlusion / nearbyValidSamples) : 0.0);
                ao = pow(clamp(ao, 0.0, 1.0), 2.0);
                FragColor = vec4(vec3(clamp(ao, 0.0, 1.0)), 1.0);
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
                        float spatialSigma = max(blurRadius * 0.90, 0.9);
                        float spatialWeight = exp(-(kernelLen * kernelLen) / max(2.0 * spatialSigma * spatialSigma, 0.0001));
                        float normalWeight = pow(max(dot(centerNormalView, sampleNormalView), 0.0), mix(1.0, 3.0, sharpnessNorm));
                        normalWeight = mix(0.25, 1.0, normalWeight);
                        float viewDepthDiff = abs(samplePosView.z - centerPosView.z);
                        float planeDiff = abs(dot(centerNormalView, samplePosView - centerPosView));
                        float depthMetric = max(planeDiff, viewDepthDiff * 0.5);
                        float depthWeight = exp(-depthMetric / max(depthSigma, 0.0001));
                        float weight = spatialWeight * normalWeight * depthWeight;

                        aoSum += sampleAo * weight;
                        weightSum += weight;
                    }
                }

                float blurredAo = (weightSum > 0.0) ? (aoSum / weightSum) : centerAo;
                FragColor = vec4(vec3(clamp(blurredAo, 0.0, 1.0)), 1.0);
            }
        )";

        const std::string SSAO_COMPOSITE_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D screenTexture;
            uniform sampler2D aoRawTexture;
            uniform sampler2D aoBlurTexture;
            uniform sampler2D normalTexture;
            uniform sampler2D positionTexture;
            uniform float u_intensity;
            uniform float u_giBoost;
            uniform int u_debugView;

            void main(){
                vec4 base = texture(screenTexture, TexCoords);
                vec3 normalWorld = texture(normalTexture, TexCoords).xyz;
                if(length(normalWorld) < 1e-4){
                    if(u_debugView == 4){
                        FragColor = vec4(vec3(0.0), 1.0);
                    }else if(u_debugView != 0){
                        FragColor = vec4(vec3(1.0), 1.0);
                    }else{
                        FragColor = base;
                    }
                    return;
                }

                float rawOcc = clamp(texture(aoRawTexture, TexCoords).r, 0.0, 1.0);
                float blurredOcc = clamp(texture(aoBlurTexture, TexCoords).r, 0.0, 1.0);
                float materialAo = clamp(texture(positionTexture, TexCoords).a, 0.0, 1.0);

                float ssaoFactor = pow(max(blurredOcc, 0.0001), max(u_intensity, 0.0));
                ssaoFactor = clamp(ssaoFactor, 0.0, 1.0);
                float combinedAo = clamp(materialAo * ssaoFactor, 0.0, 1.0);
                float bounce = 1.0 - ssaoFactor;
                vec3 giBounce = base.rgb * (bounce * bounce * max(u_giBoost, 0.0));

                if(u_debugView == 1){
                    FragColor = vec4(vec3(combinedAo), 1.0);
                    return;
                }
                if(u_debugView == 2){
                    FragColor = vec4(vec3(rawOcc), 1.0);
                    return;
                }
                if(u_debugView == 3){
                    FragColor = vec4(vec3(materialAo), 1.0);
                    return;
                }
                if(u_debugView == 4){
                    FragColor = vec4(clamp(giBounce * 4.0, vec3(0.0), vec3(1.0)), 1.0);
                    return;
                }

                vec3 finalColor = (base.rgb * combinedAo) + giBounce;
                FragColor = vec4(finalColor, base.a);
            }
        )";

        bool ensureCompiled(){
            if(!rawShader || !blurShader || !compositeShader){
                return false;
            }
            if(rawShader->getID() != 0 && blurShader->getID() != 0 && compositeShader->getID() != 0){
                return true;
            }
            if(compileAttempted){
                return false;
            }

            compileAttempted = true;
            bool ok = true;
            if(rawShader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile robust SSAO raw shader:\n%s", rawShader->getLog().c_str());
                ok = false;
            }
            if(blurShader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile robust SSAO blur shader:\n%s", blurShader->getLog().c_str());
                ok = false;
            }
            if(compositeShader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile robust SSAO composite shader:\n%s", compositeShader->getLog().c_str());
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

        void initializeKernelAndNoise(){
            if(kernelInitialized && noiseTexture){
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

            std::array<float, 4 * 4 * 3> noiseData{};
            for(int i = 0; i < 16; ++i){
                noiseData[i * 3 + 0] = random11(rng);
                noiseData[i * 3 + 1] = random11(rng);
                noiseData[i * 3 + 2] = 0.0f;
            }

            GLuint noiseTexId = 0;
            glGenTextures(1, &noiseTexId);
            glBindTexture(GL_TEXTURE_2D, noiseTexId);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noiseData.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glBindTexture(GL_TEXTURE_2D, 0);

            noiseTexture = Texture::CreateFromExisting(noiseTexId, 4, 4, true);
            kernelInitialized = (noiseTexture != nullptr);
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

        bool renderAoPasses(int width, int height, const std::shared_ptr<ModelPart>& quad){
            if(width <= 0 || height <= 0 || !quad || !sceneNormalTex || !scenePositionTex){
                return false;
            }
            if(!ensureCompiled() || !ensureTargets(width, height)){
                return false;
            }

            initializeKernelAndNoise();
            if(!noiseTexture){
                return false;
            }

            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);

            const Math3D::Vec2 texelSize(
                1.0f / static_cast<float>(width),
                1.0f / static_cast<float>(height)
            );
            const Math3D::Vec2 noiseScale(
                static_cast<float>(width) / 4.0f,
                static_cast<float>(height) / 4.0f
            );
            int effectiveSamples = Math3D::Clamp(sampleCount, 4, MAX_KERNEL_SAMPLES);

            rawAoFbo->bind();
            rawShader->bind();
            rawShader->setUniformFast("normalTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(sceneNormalTex, 0)));
            rawShader->setUniformFast("positionTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(scenePositionTex, 1)));
            rawShader->setUniformFast("noiseTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(noiseTexture, 2)));
            rawShader->setUniformFast("u_viewMatrix", Uniform<Math3D::Mat4>(viewMatrix));
            rawShader->setUniformFast("u_projMatrix", Uniform<Math3D::Mat4>(projectionMatrix));
            rawShader->setUniformFast("u_kernelSize", Uniform<int>(effectiveSamples));
            rawShader->setUniformFast("u_noiseScale", Uniform<Math3D::Vec2>(noiseScale));
            rawShader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(texelSize));
            rawShader->setUniformFast("u_radiusPx", Uniform<float>(Math3D::Clamp(radiusPx, 0.25f, 8.0f)));
            rawShader->setUniformFast("u_depthRadius", Uniform<float>(Math3D::Clamp(depthRadius, 0.0005f, 0.5f)));
            rawShader->setUniformFast("u_bias", Uniform<float>(Math3D::Clamp(bias, 0.0f, 0.05f)));
            uploadKernelUniforms(rawShader);
            drawFullscreenPass(rawShader, quad);
            rawAoFbo->unbind();

            blurAoFbo->bind();
            blurShader->bind();
            blurShader->setUniformFast("rawAoTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(rawAoFbo->getTexture(), 0)));
            blurShader->setUniformFast("normalTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(sceneNormalTex, 1)));
            blurShader->setUniformFast("positionTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(scenePositionTex, 2)));
            blurShader->setUniformFast("u_viewMatrix", Uniform<Math3D::Mat4>(viewMatrix));
            blurShader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(texelSize));
            blurShader->setUniformFast("u_blurRadiusPx", Uniform<float>(Math3D::Clamp(blurRadiusPx, 0.25f, 8.0f)));
            blurShader->setUniformFast("u_blurSharpness", Uniform<float>(Math3D::Clamp(blurSharpness, 0.25f, 8.0f)));
            drawFullscreenPass(blurShader, quad);
            blurAoFbo->unbind();
            return true;
        }

    public:
        float radiusPx = 3.0f;
        float depthRadius = 0.025f;
        float bias = 0.001f;
        float intensity = 1.0f;
        float giBoost = 0.12f;
        float blurRadiusPx = 2.0f;
        float blurSharpness = 2.0f;
        int sampleCount = 16;
        int debugView = 0; // 0=composited, 1=combined AO, 2=SSAO raw, 3=material AO, 4=GI bounce
        PTexture sceneNormalTex = nullptr;
        PTexture scenePositionTex = nullptr;
        Math3D::Mat4 viewMatrix;
        Math3D::Mat4 projectionMatrix;
        float fovYDegrees = 45.0f;
        float aspect = 1.33f;
        int isOrtho = 0;
        Math3D::Vec2 orthoViewSize = Math3D::Vec2(1.0f, 1.0f);
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;

        RobustSSAOEffect() {
            rawShader = std::make_shared<ShaderProgram>();
            rawShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            rawShader->setFragmentShader(SSAO_RAW_FRAG_SHADER);

            blurShader = std::make_shared<ShaderProgram>();
            blurShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            blurShader->setFragmentShader(SSAO_BLUR_FRAG_SHADER);

            compositeShader = std::make_shared<ShaderProgram>();
            compositeShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            compositeShader->setFragmentShader(SSAO_COMPOSITE_FRAG_SHADER);
        }

        bool renderAoMap(int width, int height, const std::shared_ptr<ModelPart>& quad){
            return renderAoPasses(width, height, quad);
        }

        PTexture getRawAoTexture() const {
            return rawAoFbo ? rawAoFbo->getTexture() : nullptr;
        }

        PTexture getBlurAoTexture() const {
            return blurAoFbo ? blurAoFbo->getTexture() : nullptr;
        }

        bool apply(PTexture tex, PTexture depthTex, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {
            (void)depthTex;
            if(!outFbo || !quad || !tex || !sceneNormalTex || !scenePositionTex){
                return false;
            }
            if(!renderAoPasses(outFbo->getWidth(), outFbo->getHeight(), quad)){
                return false;
            }

            outFbo->bind();
            compositeShader->bind();
            compositeShader->setUniformFast("screenTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(tex, 0)));
            compositeShader->setUniformFast("aoRawTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(rawAoFbo->getTexture(), 1)));
            compositeShader->setUniformFast("aoBlurTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(blurAoFbo->getTexture(), 2)));
            compositeShader->setUniformFast("normalTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(sceneNormalTex, 3)));
            compositeShader->setUniformFast("positionTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(scenePositionTex, 4)));
            compositeShader->setUniformFast("u_intensity", Uniform<float>(Math3D::Clamp(intensity, 0.0f, 10.0f)));
            compositeShader->setUniformFast("u_giBoost", Uniform<float>(Math3D::Clamp(giBoost, 0.0f, 1.0f)));
            compositeShader->setUniformFast("u_debugView", Uniform<int>(Math3D::Clamp(debugView, 0, 4)));
            drawFullscreenPass(compositeShader, quad);
            outFbo->unbind();
            return true;
        }

        static std::shared_ptr<RobustSSAOEffect> New() {
            return std::make_shared<RobustSSAOEffect>();
        }
};

/// @brief Represents the DepthOfFieldEffect type.
class DepthOfFieldEffect : public Graphics::PostProcessing::PostProcessingEffect {
    private:
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;
        GLuint adaptiveDepthReadFbo = 0;
        float resolvedFocusDistance = 8.0f;
        float resolvedFocusRange = 4.0f;
        bool focusAdaptationInitialized = false;
        float smoothedFocusDistance = 8.0f;
        float smoothedFocusRange = 4.0f;
        float focusPullBackRate = 10.0f;
        float focusPushOutRate = 3.0f;
        float focusFallbackRate = 2.0f;
        bool debugAdaptiveCenterValid = false;
        float debugAdaptiveCenterDistance = 0.0f;
        bool debugAdaptiveRayValid = false;
        float debugAdaptiveRayDistance = 0.0f;
        bool debugAdaptiveUsedFallback = false;
        float debugAdaptiveTargetDistance = 0.0f;
        float debugAdaptiveFallbackDistance = 0.0f;
        std::chrono::steady_clock::time_point lastFocusAdaptationTime = std::chrono::steady_clock::now();
        const std::string DOF_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D screenTexture;
            uniform sampler2D depthTexture;
            uniform vec2 u_texelSize;
            uniform float u_focusDistance;
            uniform float u_focusRange;
            uniform float u_focusBandWidth;
            uniform float u_blurRamp;
            uniform float u_blurDistanceLerp;
            uniform float u_blurStrength;
            uniform float u_maxBlurPx;
            uniform int u_sampleCount;
            uniform int u_debugCocView;
            uniform int u_adaptiveFocusEnabled;
            uniform vec2 u_focusUv;
            uniform float u_nearPlane;
            uniform float u_farPlane;

            /**
             * @brief Converts device depth to linear depth.
             * @param depth Value for depth.
             * @return Computed numeric result.
             */
            float linearizeDepth(float depth){
                float z = depth * 2.0 - 1.0;
                return (2.0 * u_nearPlane * u_farPlane) / max((u_farPlane + u_nearPlane) - (z * (u_farPlane - u_nearPlane)), 0.0001);
            }

            /**
             * @brief Resolves focus distance from depth data.
             * @return Computed numeric result.
             */
            float resolveFocusDistance(){
                if(u_adaptiveFocusEnabled == 0){
                    return u_focusDistance;
                }

                const float kSkyDepthCutoff = 0.9995;
                vec2 clampedFocusUv = clamp(u_focusUv, vec2(0.0), vec2(1.0));
                float centerDepth = texture(depthTexture, clampedFocusUv).r;
                if(centerDepth < kSkyDepthCutoff){
                    return linearizeDepth(centerDepth);
                }

                vec2 fallbackTaps[8] = vec2[](
                    vec2( 1.0,  0.0),
                    vec2(-1.0,  0.0),
                    vec2( 0.0,  1.0),
                    vec2( 0.0, -1.0),
                    vec2( 2.0,  0.0),
                    vec2(-2.0,  0.0),
                    vec2( 0.0,  2.0),
                    vec2( 0.0, -2.0)
                );

                float accum = 0.0;
                float weight = 0.0;
                for(int i = 0; i < 8; ++i){
                    vec2 sampleUv = clamp(clampedFocusUv + (fallbackTaps[i] * u_texelSize), vec2(0.0), vec2(1.0));
                    float sampleDepth = texture(depthTexture, sampleUv).r;
                    if(sampleDepth >= kSkyDepthCutoff){
                        continue;
                    }
                    accum += linearizeDepth(sampleDepth);
                    weight += 1.0;
                }

                if(weight <= 0.0001){
                    return u_focusDistance;
                }
                return accum / weight;
            }

            /**
             * @brief Accumulates blur samples.
             * @param uv Value for uv.
             * @param radiusPx Value for radius px.
             * @param sampleCount Number of elements or bytes.
             * @param centerLinearDepth Value for center linear depth.
             * @param blurAmount Value for blur amount.
             * @return Result of this operation.
             */
            vec3 gatherBlur(vec2 uv, float radiusPx, int sampleCount, float centerLinearDepth, float blurAmount){
                vec2 dirs[8] = vec2[](
                    vec2( 1.0,  0.0),
                    vec2(-1.0,  0.0),
                    vec2( 0.0,  1.0),
                    vec2( 0.0, -1.0),
                    vec2( 0.707,  0.707),
                    vec2(-0.707,  0.707),
                    vec2( 0.707, -0.707),
                    vec2(-0.707, -0.707)
                );

                vec3 accum = texture(screenTexture, uv).rgb;
                float weight = 1.0;
                int loops = clamp(sampleCount, 1, 8);
                float baseDepthTolerance = max(u_focusRange * 0.35, 0.25);
                // Keep blur relaxation continuous from the first blur contribution so large flats
                // do not show a distance "knee" where blur behavior abruptly changes.
                float blurRelax = smoothstep(0.0, 1.0, clamp(blurAmount, 0.0, 1.0));
                float relaxedDepthTolerance = mix(baseDepthTolerance, max(baseDepthTolerance * 4.0, 1.5), blurRelax);
                for(int i = 0; i < loops; ++i){
                    vec2 offset = dirs[i] * radiusPx * u_texelSize;
                    vec2 sampleUv = clamp(uv + offset, vec2(0.0), vec2(1.0));
                    vec3 sampleColor = texture(screenTexture, sampleUv).rgb;
                    float sampleLinearDepth = linearizeDepth(texture(depthTexture, sampleUv).r);
                    float depthDelta = abs(sampleLinearDepth - centerLinearDepth);
                    float depthWeight = 1.0 - smoothstep(0.0, relaxedDepthTolerance, depthDelta);
                    depthWeight = mix(depthWeight, 1.0, blurRelax * 0.55);
                    accum += sampleColor * depthWeight;
                    weight += depthWeight;
                }
                return accum / max(weight, 0.0001);
            }

            /**
             * @brief Executes the main shader pass.
             */
            void main() {
                vec4 base = texture(screenTexture, TexCoords);
                float depth = texture(depthTexture, TexCoords).r;
                float linearDepth = linearizeDepth(depth);
                float focusDistance = resolveFocusDistance();

                float focusRange = max(u_focusRange, 0.001);
                float depthDelta = linearDepth - focusDistance;
                float nearBoost = (depthDelta < 0.0) ? 1.15 : 1.0;
                // Smooth CoC falloff prevents a harsh in-focus strip while keeping
                // a clear focal region around the resolved distance.
                float absDepthDelta = abs(depthDelta);
                float focusBandWidth = max(u_focusBandWidth, 0.05);
                float focusSigma = max(focusRange * focusBandWidth, 0.02);
                float coc = 1.0 - exp(-(absDepthDelta * absDepthDelta) / (2.0 * focusSigma * focusSigma));
                coc = clamp(coc * nearBoost, 0.0, 1.0);
                float blurRamp = max(u_blurRamp, 0.05);
                float cocCurve = pow(coc, blurRamp);
                float blurDistanceLerp = clamp(u_blurDistanceLerp, 0.0, 1.0);
                float cocBlend = mix(cocCurve, coc, blurDistanceLerp);
                float strength = max(u_blurStrength, 0.0);
                float blurRadius = cocBlend * max(u_maxBlurPx, 0.0) * strength;
                // Use stronger blend than radius scaling so clearly out-of-focus regions
                // don't remain overly crisp at moderate blurStrength values.
                float blurAmount = clamp(cocBlend * clamp(strength * 1.8, 0.0, 1.0), 0.0, 1.0);
                if(u_debugCocView != 0){
                    vec3 focusColor = vec3(0.10, 0.95, 0.20);
                    vec3 nearColor = vec3(0.18, 0.55, 1.00);
                    vec3 farColor = vec3(1.00, 0.42, 0.12);
                    vec3 sideColor = (depthDelta < 0.0) ? nearColor : farColor;
                    vec3 cocColor = mix(focusColor, sideColor, coc);
                    cocColor *= mix(0.45, 1.0, blurAmount);
                    FragColor = vec4(cocColor, 1.0);
                    return;
                }
                vec3 blurred = gatherBlur(TexCoords, blurRadius, u_sampleCount, linearDepth, blurAmount);
                vec3 color = mix(base.rgb, blurred, blurAmount);
                FragColor = vec4(color, base.a);
            }
        )";

        /**
         * @brief Checks whether ensure compiled.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ensureCompiled(){
            if(!shader){
                return false;
            }
            if(shader->getID() != 0){
                return true;
            }
            if(compileAttempted){
                return false;
            }
            compileAttempted = true;
            if(shader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile depth-of-field effect shader:\n%s", shader->getLog().c_str());
                return false;
            }
            return true;
        }

        /**
         * @brief Converts device depth to linear depth on CPU.
         * @param depth Value for depth.
         * @return Computed numeric result.
         */
        float linearizeDepthCpu(float depth) const{
            float z = (depth * 2.0f) - 1.0f;
            return (2.0f * nearPlane * farPlane) /
                   Math3D::Max((farPlane + nearPlane) - (z * (farPlane - nearPlane)), 0.0001f);
        }

        /**
         * @brief Checks whether sample adaptive focus distance.
         * @param depthTex Value for depth tex.
         * @param outDistance Output value for distance.
         * @return True when the operation succeeds; otherwise false.
         */
        bool sampleAdaptiveFocusDistance(PTexture depthTex, float& outDistance){
            if(!depthTex || depthTex->getID() == 0){
                return false;
            }
            const int width = depthTex->getWidth();
            const int height = depthTex->getHeight();
            if(width <= 0 || height <= 0){
                return false;
            }
            if(adaptiveDepthReadFbo == 0){
                glGenFramebuffers(1, &adaptiveDepthReadFbo);
            }
            if(adaptiveDepthReadFbo == 0){
                return false;
            }

            GLint prevReadFbo = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, adaptiveDepthReadFbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex->getID(), 0);
            glReadBuffer(GL_NONE);
            const bool complete = (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
            if(!complete){
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
                return false;
            }

            const float kSkyDepthCutoff = 0.9995f;
            auto readDepthAt = [&](int x, int y, float& outDepth) -> bool {
                float depth = 1.0f;
                glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
                if(!std::isfinite(depth) || depth >= kSkyDepthCutoff){
                    return false;
                }
                outDepth = depth;
                return true;
            };

            const int centerX = Math3D::Clamp(
                static_cast<int>(std::round(focusUv.x * static_cast<float>(width - 1))),
                0,
                width - 1
            );
            const int centerY = Math3D::Clamp(
                static_cast<int>(std::round(focusUv.y * static_cast<float>(height - 1))),
                0,
                height - 1
            );

            float resolvedDepth = 1.0f;
            bool gotDepth = readDepthAt(centerX, centerY, resolvedDepth);
            if(!gotDepth){
                static const int OFFSETS[8][2] = {
                    { 1,  0}, {-1,  0},
                    { 0,  1}, { 0, -1},
                    { 2,  0}, {-2,  0},
                    { 0,  2}, { 0, -2}
                };
                float accum = 0.0f;
                int count = 0;
                for(const auto& offset : OFFSETS){
                    const int sx = Math3D::Clamp(centerX + offset[0], 0, width - 1);
                    const int sy = Math3D::Clamp(centerY + offset[1], 0, height - 1);
                    float sampleDepth = 1.0f;
                    if(readDepthAt(sx, sy, sampleDepth)){
                        accum += sampleDepth;
                        count++;
                    }
                }
                if(count > 0){
                    resolvedDepth = accum / static_cast<float>(count);
                    gotDepth = true;
                }
            }

            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
            if(!gotDepth){
                return false;
            }

            outDistance = linearizeDepthCpu(resolvedDepth);
            return std::isfinite(outDistance) && outDistance > 0.0f;
        }

    public:
        float focusDistance = 8.0f;
        float focusRange = 4.0f;
        float focusBandWidth = 0.85f;
        float blurRamp = 2.0f;
        float blurDistanceLerp = 0.35f;
        float fallbackFocusRange = 6.0f;
        float blurStrength = 0.65f;
        float maxBlurPx = 7.0f;
        int sampleCount = 6;
        bool debugCocView = false;
        bool adaptiveFocus = false;
        Math3D::Vec2 focusUv = Math3D::Vec2(0.5f, 0.5f);
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        bool externalAdaptiveFocusValid = false;
        float externalAdaptiveFocusDistance = 0.0f;

        /**
         * @brief Constructs a new DepthOfFieldEffect instance.
         */
        DepthOfFieldEffect() {
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(DOF_FRAG_SHADER);
        }

        /**
         * @brief Destroys this DepthOfFieldEffect instance.
         */
        ~DepthOfFieldEffect() override {
            if(adaptiveDepthReadFbo != 0){
                glDeleteFramebuffers(1, &adaptiveDepthReadFbo);
                adaptiveDepthReadFbo = 0;
            }
        }

        /**
         * @brief Returns the resolved focus distance.
         * @return Computed numeric result.
         */
        float getResolvedFocusDistance() const{
            return resolvedFocusDistance;
        }

        /**
         * @brief Returns the resolved focus range.
         * @return Computed numeric result.
         */
        float getResolvedFocusRange() const{
            return resolvedFocusRange;
        }

        /**
         * @brief Returns the debug adaptive center valid.
         * @return True when the operation succeeds; otherwise false.
         */
        bool getDebugAdaptiveCenterValid() const{
            return debugAdaptiveCenterValid;
        }

        /**
         * @brief Returns the debug adaptive center distance.
         * @return Computed numeric result.
         */
        float getDebugAdaptiveCenterDistance() const{
            return debugAdaptiveCenterDistance;
        }

        /**
         * @brief Returns the debug adaptive ray valid.
         * @return True when the operation succeeds; otherwise false.
         */
        bool getDebugAdaptiveRayValid() const{
            return debugAdaptiveRayValid;
        }

        /**
         * @brief Returns the debug adaptive ray distance.
         * @return Computed numeric result.
         */
        float getDebugAdaptiveRayDistance() const{
            return debugAdaptiveRayDistance;
        }

        /**
         * @brief Returns the debug adaptive used fallback.
         * @return True when the operation succeeds; otherwise false.
         */
        bool getDebugAdaptiveUsedFallback() const{
            return debugAdaptiveUsedFallback;
        }

        /**
         * @brief Returns the debug adaptive target distance.
         * @return Computed numeric result.
         */
        float getDebugAdaptiveTargetDistance() const{
            return debugAdaptiveTargetDistance;
        }

        /**
         * @brief Returns the debug adaptive fallback distance.
         * @return Computed numeric result.
         */
        float getDebugAdaptiveFallbackDistance() const{
            return debugAdaptiveFallbackDistance;
        }

        /**
         * @brief Applies current settings.
         * @param tex Value for tex.
         * @param depthTex Value for depth tex.
         * @param outFbo Output value for fbo.
         * @param quad Value for quad.
         * @return True when the operation succeeds; otherwise false.
         */
        bool apply(PTexture tex, PTexture depthTex, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {
            if(!outFbo || !quad || !tex || !depthTex){
                return false;
            }
            if(!ensureCompiled()){
                return false;
            }

            outFbo->bind();
            glDisable(GL_DEPTH_TEST);

            float targetFocusRange = Math3D::Max(0.001f, focusRange);
            bool useExternalAdaptiveFocus = externalAdaptiveFocusValid;
            float externalAdaptiveDistance = externalAdaptiveFocusDistance;
            externalAdaptiveFocusValid = false;
            debugAdaptiveCenterValid = false;
            debugAdaptiveCenterDistance = 0.0f;
            debugAdaptiveRayValid = false;
            debugAdaptiveRayDistance = 0.0f;
            debugAdaptiveUsedFallback = false;
            debugAdaptiveTargetDistance = 0.0f;
            debugAdaptiveFallbackDistance = Math3D::Max(0.01f, focusDistance);
            if(adaptiveFocus){
                const float fallbackFocusDistance = Math3D::Max(0.01f, focusDistance);
                bool hasRayFocusTarget = false;
                float targetFocusPlaneDistance = fallbackFocusDistance;
                float sampledFocusDistance = 0.0f;
                if(useExternalAdaptiveFocus && std::isfinite(externalAdaptiveDistance) && externalAdaptiveDistance > 0.0f){
                    targetFocusPlaneDistance = Math3D::Max(0.01f, externalAdaptiveDistance);
                    hasRayFocusTarget = true;
                    debugAdaptiveRayValid = true;
                    debugAdaptiveRayDistance = targetFocusPlaneDistance;
                }else if(sampleAdaptiveFocusDistance(depthTex, sampledFocusDistance)){
                    targetFocusPlaneDistance = Math3D::Max(0.01f, sampledFocusDistance);
                    hasRayFocusTarget = true;
                    debugAdaptiveCenterValid = true;
                    debugAdaptiveCenterDistance = targetFocusPlaneDistance;
                }else{
                    targetFocusRange = Math3D::Max(0.001f, fallbackFocusRange);
                }
                debugAdaptiveTargetDistance = targetFocusPlaneDistance;
                debugAdaptiveUsedFallback = !hasRayFocusTarget;
                debugAdaptiveFallbackDistance = fallbackFocusDistance;

                auto now = std::chrono::steady_clock::now();
                if(!focusAdaptationInitialized){
                    smoothedFocusDistance = targetFocusPlaneDistance;
                    smoothedFocusRange = targetFocusRange;
                    focusAdaptationInitialized = true;
                    lastFocusAdaptationTime = now;
                }else{
                    float dt = std::chrono::duration<float>(now - lastFocusAdaptationTime).count();
                    dt = Math3D::Clamp(dt, 0.001f, 0.25f);
                    lastFocusAdaptationTime = now;

                    auto moveTowardNoOvershoot = [&](float current, float target, float rate) -> float {
                        float alpha = 1.0f - std::exp(-Math3D::Max(rate, 0.0001f) * dt);
                        float next = current + (target - current) * alpha;
                        if(target >= current){
                            return Math3D::Min(next, target);
                        }
                        return Math3D::Max(next, target);
                    };

                    if(hasRayFocusTarget){
                        if(smoothedFocusDistance > targetFocusPlaneDistance){
                            // FocusDistance is beyond target plane: pull back quickly toward it.
                            smoothedFocusDistance = moveTowardNoOvershoot(smoothedFocusDistance, targetFocusPlaneDistance, focusPullBackRate);
                        }else if(smoothedFocusDistance < targetFocusPlaneDistance){
                            // FocusDistance is nearer than target plane: push out slowly toward it.
                            smoothedFocusDistance = moveTowardNoOvershoot(smoothedFocusDistance, targetFocusPlaneDistance, focusPushOutRate);
                        }
                    }else{
                        // No ray hit: drift back to fallback focus distance.
                        smoothedFocusDistance = moveTowardNoOvershoot(smoothedFocusDistance, fallbackFocusDistance, focusFallbackRate);
                    }

                    const float rangeRate = hasRayFocusTarget ? 5.0f : 2.5f;
                    const float rangeAlpha = 1.0f - std::exp(-rangeRate * dt);
                    smoothedFocusRange += (targetFocusRange - smoothedFocusRange) * rangeAlpha;
                }

                resolvedFocusDistance = Math3D::Max(0.01f, smoothedFocusDistance);
                resolvedFocusRange = Math3D::Max(0.001f, smoothedFocusRange);
            }else{
                float targetFocusDistance = Math3D::Max(0.01f, focusDistance);
                focusAdaptationInitialized = false;
                smoothedFocusDistance = targetFocusDistance;
                smoothedFocusRange = targetFocusRange;
                resolvedFocusDistance = targetFocusDistance;
                resolvedFocusRange = targetFocusRange;
            }

            shader->bind();
            shader->setUniformFast("screenTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(tex, 0)));
            shader->setUniformFast("depthTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(depthTex, 1)));
            shader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(Math3D::Vec2(1.0f / (float)outFbo->getWidth(), 1.0f / (float)outFbo->getHeight())));
            shader->setUniformFast("u_focusDistance", Uniform<float>(resolvedFocusDistance));
            shader->setUniformFast("u_focusRange", Uniform<float>(resolvedFocusRange));
            shader->setUniformFast("u_focusBandWidth", Uniform<float>(Math3D::Clamp(focusBandWidth, 0.05f, 4.0f)));
            shader->setUniformFast("u_blurRamp", Uniform<float>(Math3D::Clamp(blurRamp, 0.05f, 6.0f)));
            shader->setUniformFast("u_blurDistanceLerp", Uniform<float>(Math3D::Clamp(blurDistanceLerp, 0.0f, 1.0f)));
            shader->setUniformFast("u_blurStrength", Uniform<float>(blurStrength));
            shader->setUniformFast("u_maxBlurPx", Uniform<float>(maxBlurPx));
            shader->setUniformFast("u_debugCocView", Uniform<int>(debugCocView ? 1 : 0));
            // CPU resolves and smooths adaptive focus distance from scene raycast each frame.
            // Keep shader-side adaptive depth sampling disabled to avoid competing focus targets.
            shader->setUniformFast("u_adaptiveFocusEnabled", Uniform<int>(0));
            shader->setUniformFast("u_focusUv", Uniform<Math3D::Vec2>(focusUv));
            int effectiveSamples = sampleCount;
            const int pixelCount = outFbo->getWidth() * outFbo->getHeight();
            if(pixelCount >= (2560 * 1440) && effectiveSamples > 4){
                effectiveSamples = 4;
            }else if(pixelCount >= (1920 * 1080) && effectiveSamples > 5){
                effectiveSamples = 5;
            }
            if(effectiveSamples < 1){
                effectiveSamples = 1;
            }
            shader->setUniformFast("u_sampleCount", Uniform<int>(effectiveSamples));
            shader->setUniformFast("u_nearPlane", Uniform<float>(nearPlane));
            shader->setUniformFast("u_farPlane", Uniform<float>(farPlane));

            static const Math3D::Mat4 IDENTITY;
            shader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));
            quad->draw(IDENTITY, IDENTITY, IDENTITY);
            outFbo->unbind();
            return true;
        }

        /**
         * @brief Creates a new object.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<DepthOfFieldEffect> New() {
            return std::make_shared<DepthOfFieldEffect>();
        }
};

/// @brief Represents the BloomEffect type.
class BloomEffect : public Graphics::PostProcessing::PostProcessingEffect {
    private:
        static constexpr int METER_INTERVAL_FRAMES = 6;
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;
        const std::string BLOOM_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D screenTexture;
            uniform vec2 u_texelSize;
            uniform float u_threshold;
            uniform float u_softKnee;
            uniform float u_intensity;
            uniform float u_radiusPx;
            uniform int u_sampleCount;
            uniform vec3 u_tint;
            uniform float u_exposureScale;

            const vec2 kPoisson[12] = vec2[](
                vec2(-0.326, -0.406),
                vec2(-0.840, -0.074),
                vec2(-0.696,  0.457),
                vec2(-0.203,  0.621),
                vec2( 0.962, -0.195),
                vec2( 0.473, -0.480),
                vec2( 0.519,  0.767),
                vec2( 0.185, -0.893),
                vec2( 0.507,  0.064),
                vec2( 0.896,  0.412),
                vec2(-0.322,  0.951),
                /**
                 * @brief Builds a 2D vector.
                 */
                vec2(-0.720,  0.631)
            );

            /**
             * @brief Filters bright pixels for bloom.
             * @param color Color value.
             * @return Result of this operation.
             */
            vec3 prefilterBright(vec3 color){
                float brightness = max(color.r, max(color.g, color.b));
                float threshold = max(u_threshold, 0.0);
                float knee = max(threshold * max(u_softKnee, 0.0001), 0.00001);
                float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
                soft = (0.25 / knee) * soft * soft;
                float hard = max(brightness - threshold, 0.0);
                float contribution = max(hard, soft) / max(brightness, 0.0001);
                return color * contribution;
            }

            /**
             * @brief Executes the main shader pass.
             */
            void main() {
                float exposureScale = max(u_exposureScale, 0.0);
                vec3 base = texture(screenTexture, TexCoords).rgb;

                int count = clamp(u_sampleCount, 1, 12);
                float radiusPx = max(u_radiusPx, 0.0);
                vec3 bloomAccum = prefilterBright(base);
                float weightSum = 1.0;

                for(int i = 0; i < 12; ++i){
                    if(i >= count){
                        break;
                    }
                    float ringT = (float(i) + 0.5) / float(count);
                    float radialScale = mix(0.35, 1.0, ringT);
                    float weight = mix(1.0, 0.35, ringT);
                    vec2 offset = kPoisson[i] * radiusPx * radialScale * u_texelSize;
                    vec2 uvA = clamp(TexCoords + offset, vec2(0.0), vec2(1.0));
                    vec2 uvB = clamp(TexCoords - offset, vec2(0.0), vec2(1.0));
                    vec3 sampleA = prefilterBright(texture(screenTexture, uvA).rgb * exposureScale);
                    vec3 sampleB = prefilterBright(texture(screenTexture, uvB).rgb * exposureScale);
                    bloomAccum += (sampleA + sampleB) * weight;
                    weightSum += 2.0 * weight;
                }

                vec3 bloom = (bloomAccum / max(weightSum, 0.0001));
                bloom *= max(u_intensity, 0.0) * u_tint;

                vec3 color = base + bloom;
                FragColor = vec4(max(color, vec3(0.0)), 1.0);
            }
        )";
        GLuint adaptationReadFbo = 0;
        bool adaptationInitialized = false;
        float adaptedLuminance = 0.35f;
        float filteredLuminance = 0.35f;
        float smoothedThresholdScale = 1.0f;
        float smoothedIntensityScale = 1.0f;
        float smoothedExposureScale = 1.0f;
        int meteringFrameCounter = 0;
        std::chrono::steady_clock::time_point lastAdaptationTime = std::chrono::steady_clock::now();

        /**
         * @brief Checks whether sample scene luminance.
         * @param tex Value for tex.
         * @param outLuminance Output value for luminance.
         * @return True when the operation succeeds; otherwise false.
         */
        bool sampleSceneLuminance(PTexture tex, float& outLuminance){
            if(!tex || tex->getID() == 0 || tex->getWidth() <= 0 || tex->getHeight() <= 0){
                return false;
            }
            if(adaptationReadFbo == 0){
                glGenFramebuffers(1, &adaptationReadFbo);
            }
            if(adaptationReadFbo == 0){
                return false;
            }

            GLint prevReadFbo = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, adaptationReadFbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->getID(), 0);
            if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
                return false;
            }

            glReadBuffer(GL_COLOR_ATTACHMENT0);
            auto readLumaAt = [](int x, int y) -> float {
                float pixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pixel);
                return (pixel[0] * 0.2126f) + (pixel[1] * 0.7152f) + (pixel[2] * 0.0722f);
            };

            const int width = tex->getWidth();
            const int height = tex->getHeight();
            const int centerX = width / 2;
            const int centerY = height / 2;

            float luma = 0.0f;
            luma += readLumaAt(centerX, centerY) * 0.50f;
            luma += readLumaAt(width / 4, height / 4) * 0.125f;
            luma += readLumaAt((width * 3) / 4, height / 4) * 0.125f;
            luma += readLumaAt(width / 4, (height * 3) / 4) * 0.125f;
            luma += readLumaAt((width * 3) / 4, (height * 3) / 4) * 0.125f;

            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
            if(!std::isfinite(luma)){
                return false;
            }

            outLuminance = Math3D::Clamp(luma, 0.0001f, 64.0f);
            return true;
        }

        /**
         * @brief Computes adaptive scales.
         * @param tex Value for tex.
         * @param outThresholdScale Spatial value used by this operation.
         * @param outIntensityScale Spatial value used by this operation.
         * @param outExposureScale Spatial value used by this operation.
         */
        void computeAdaptiveScales(PTexture tex, float& outThresholdScale, float& outIntensityScale, float& outExposureScale){
            outThresholdScale = smoothedThresholdScale;
            outIntensityScale = smoothedIntensityScale;
            outExposureScale = smoothedExposureScale;
            if(!adaptiveBloom){
                adaptationInitialized = false;
                adaptedLuminance = 0.35f;
                filteredLuminance = 0.35f;
                smoothedThresholdScale = 1.0f;
                smoothedIntensityScale = 1.0f;
                smoothedExposureScale = 1.0f;
                meteringFrameCounter = 0;
                outThresholdScale = smoothedThresholdScale;
                outIntensityScale = smoothedIntensityScale;
                outExposureScale = smoothedExposureScale;
                return;
            }

            if(adaptationInitialized){
                meteringFrameCounter = (meteringFrameCounter + 1) % METER_INTERVAL_FRAMES;
                if(meteringFrameCounter != 0){
                    return;
                }
            }else{
                meteringFrameCounter = 0;
            }

            float observedLuminance = adaptedLuminance;
            if(!sampleSceneLuminance(tex, observedLuminance)){
                return;
            }

            auto now = std::chrono::steady_clock::now();
            if(!adaptationInitialized){
                adaptedLuminance = observedLuminance;
                filteredLuminance = observedLuminance;
                smoothedThresholdScale = 1.0f;
                smoothedIntensityScale = 1.0f;
                smoothedExposureScale = 1.0f;
                adaptationInitialized = true;
                lastAdaptationTime = now;
            }else{
                float dt = std::chrono::duration<float>(now - lastAdaptationTime).count();
                dt = Math3D::Clamp(dt, 0.001f, 0.25f);
                lastAdaptationTime = now;

                // First, filter noisy per-frame luminance shifts (camera motion over high-contrast edges).
                const float observedFilterRate = 5.0f;
                const float observedAlpha = 1.0f - std::exp(-observedFilterRate * dt);
                filteredLuminance += (observedLuminance - filteredLuminance) * observedAlpha;

                // Then move the "eye adaptation" state toward filtered luminance.
                const float adaptationRateUp = 1.35f;
                const float adaptationRateDown = 0.95f;
                const float adaptationRate = (filteredLuminance > adaptedLuminance) ? adaptationRateUp : adaptationRateDown;
                const float adaptationAlpha = 1.0f - std::exp(-adaptationRate * dt);
                adaptedLuminance += (filteredLuminance - adaptedLuminance) * adaptationAlpha;

                float ratio = Math3D::Clamp(filteredLuminance / Math3D::Max(adaptedLuminance, 0.0001f), 0.5f, 2.5f);
                if(std::abs(std::log2(ratio)) < 0.06f){
                    ratio = 1.0f;
                }

                const float targetExposureScale = Math3D::Clamp(std::pow(ratio, 0.16f), 0.92f, 1.16f);
                const float targetIntensityScale = Math3D::Clamp(std::pow(ratio, 0.30f), 0.72f, 1.55f);
                const float targetThresholdScale = Math3D::Clamp(std::pow(ratio, -0.18f), 0.78f, 1.22f);

                const float outputSmoothingRate = 4.2f;
                const float outputAlpha = 1.0f - std::exp(-outputSmoothingRate * dt);
                smoothedExposureScale += (targetExposureScale - smoothedExposureScale) * outputAlpha;
                smoothedIntensityScale += (targetIntensityScale - smoothedIntensityScale) * outputAlpha;
                smoothedThresholdScale += (targetThresholdScale - smoothedThresholdScale) * outputAlpha;
            }

            outThresholdScale = smoothedThresholdScale;
            outIntensityScale = smoothedIntensityScale;
            outExposureScale = smoothedExposureScale;
        }

        /**
         * @brief Checks whether ensure compiled.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ensureCompiled(){
            if(!shader){
                return false;
            }
            if(shader->getID() != 0){
                return true;
            }
            if(compileAttempted){
                return false;
            }
            compileAttempted = true;
            if(shader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile Bloom shader:\n%s", shader->getLog().c_str());
                return false;
            }
            return true;
        }

    public:
        bool adaptiveBloom = false;
        float threshold = 0.75f;
        float softKnee = 0.5f;
        float intensity = 0.65f;
        float radiusPx = 6.0f;
        int sampleCount = 8;
        Math3D::Vec3 tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
        float autoExposureIntensityScale = 1.0f;
        float autoExposureThresholdScale = 1.0f;

        /**
         * @brief Constructs a new BloomEffect instance.
         */
        BloomEffect() {
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(BLOOM_FRAG_SHADER);
        }

        /**
         * @brief Destroys this BloomEffect instance.
         */
        ~BloomEffect() override {
            if(adaptationReadFbo != 0){
                glDeleteFramebuffers(1, &adaptationReadFbo);
                adaptationReadFbo = 0;
            }
        }

        /**
         * @brief Applies current settings.
         * @param tex Value for tex.
         * @param PTexture Value for p texture.
         * @param outFbo Output value for fbo.
         * @param quad Value for quad.
         * @return True when the operation succeeds; otherwise false.
         */
        bool apply(PTexture tex, PTexture, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {
            if(!outFbo || !quad || !tex){
                return false;
            }
            if(!ensureCompiled()){
                return false;
            }

            outFbo->bind();
            glDisable(GL_DEPTH_TEST);

            shader->bind();
            shader->setUniformFast("screenTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(tex, 0)));
            shader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(Math3D::Vec2(1.0f / (float)outFbo->getWidth(), 1.0f / (float)outFbo->getHeight())));
            float thresholdScale = Math3D::Clamp(autoExposureThresholdScale, 0.25f, 4.0f);
            float intensityScale = Math3D::Clamp(autoExposureIntensityScale, 0.25f, 4.0f);
            float exposureScale = 1.0f;
            float adaptiveThresholdScale = 1.0f;
            float adaptiveIntensityScale = 1.0f;
            const bool hasExternalExposureCoupling =
                (std::abs(autoExposureThresholdScale - 1.0f) > 0.001f) ||
                (std::abs(autoExposureIntensityScale - 1.0f) > 0.001f);
            if(adaptiveBloom && !hasExternalExposureCoupling){
                computeAdaptiveScales(tex, adaptiveThresholdScale, adaptiveIntensityScale, exposureScale);
            }else{
                adaptationInitialized = false;
                meteringFrameCounter = 0;
                adaptedLuminance = 0.35f;
                filteredLuminance = 0.35f;
                smoothedThresholdScale = 1.0f;
                smoothedIntensityScale = 1.0f;
                smoothedExposureScale = 1.0f;
            }
            thresholdScale *= adaptiveThresholdScale;
            intensityScale *= adaptiveIntensityScale;

            shader->setUniformFast("u_threshold", Uniform<float>(Math3D::Clamp(threshold * thresholdScale, 0.0f, 4.0f)));
            shader->setUniformFast("u_softKnee", Uniform<float>(softKnee));
            shader->setUniformFast("u_intensity", Uniform<float>(Math3D::Clamp(intensity * intensityScale, 0.0f, 6.0f)));
            shader->setUniformFast("u_radiusPx", Uniform<float>(radiusPx));
            shader->setUniformFast("u_exposureScale", Uniform<float>(exposureScale));

            int effectiveSamples = sampleCount;
            const int pixelCount = outFbo->getWidth() * outFbo->getHeight();
            if(pixelCount >= (2560 * 1440) && effectiveSamples > 8){
                effectiveSamples = 8;
            }else if(pixelCount >= (1920 * 1080) && effectiveSamples > 10){
                effectiveSamples = 10;
            }
            if(effectiveSamples < 1){
                effectiveSamples = 1;
            }
            shader->setUniformFast("u_sampleCount", Uniform<int>(effectiveSamples));
            shader->setUniformFast("u_tint", Uniform<Math3D::Vec3>(tint));

            static const Math3D::Mat4 IDENTITY;
            shader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));
            quad->draw(IDENTITY, IDENTITY, IDENTITY);
            outFbo->unbind();
            return true;
        }

        /**
         * @brief Creates a new object.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<BloomEffect> New() {
            return std::make_shared<BloomEffect>();
        }
};

/// @brief Represents the AutoExposureEffect type.
class AutoExposureEffect : public Graphics::PostProcessing::PostProcessingEffect {
    private:
        static constexpr int METER_INTERVAL_FRAMES = 6;
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;
        GLuint meteringReadFbo = 0;
        bool adaptationInitialized = false;
        float adaptedExposure = 1.0f;
        float filteredLogLuminance = std::log2(0.18f);
        int meteringFrameCounter = 0;
        std::chrono::steady_clock::time_point lastAdaptationTime = std::chrono::steady_clock::now();

        const std::string AUTO_EXPOSURE_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D screenTexture;
            uniform float u_exposure;

            /**
             * @brief Converts to nemap aces.
             * @param color Color value.
             * @return Result of this operation.
             */
            vec3 tonemapAces(vec3 color){
                vec3 a = color * (color * 2.51 + 0.03);
                vec3 b = color * (color * 2.43 + 0.59) + 0.14;
                return clamp(a / max(b, vec3(0.0001)), 0.0, 1.0);
            }

            /**
             * @brief Executes the main shader pass.
             */
            void main() {
                vec4 base = texture(screenTexture, TexCoords);
                vec3 exposed = max(base.rgb, vec3(0.0)) * max(u_exposure, 0.0001);
                vec3 mapped = tonemapAces(exposed);
                FragColor = vec4(mapped, base.a);
            }
        )";

        /**
         * @brief Checks whether sample scene luminance.
         * @param tex Value for tex.
         * @param outLuminance Output value for luminance.
         * @return True when the operation succeeds; otherwise false.
         */
        bool sampleSceneLuminance(PTexture tex, float& outLuminance){
            if(!tex || tex->getID() == 0 || tex->getWidth() <= 0 || tex->getHeight() <= 0){
                return false;
            }
            if(meteringReadFbo == 0){
                glGenFramebuffers(1, &meteringReadFbo);
            }
            if(meteringReadFbo == 0){
                return false;
            }

            GLint prevReadFbo = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, meteringReadFbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->getID(), 0);
            if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
                return false;
            }

            glReadBuffer(GL_COLOR_ATTACHMENT0);

            /// @brief Holds data for MeterTap.
            struct MeterTap {
                float u;
                float v;
                float weight;
            };

            static const std::array<MeterTap, 9> kMeterTaps = {{
                {0.50f, 0.50f, 0.30f},
                {0.35f, 0.35f, 0.14f},
                {0.65f, 0.35f, 0.14f},
                {0.35f, 0.65f, 0.14f},
                {0.65f, 0.65f, 0.14f},
                {0.12f, 0.12f, 0.035f},
                {0.88f, 0.12f, 0.035f},
                {0.12f, 0.88f, 0.035f},
                {0.88f, 0.88f, 0.035f}
            }};

            std::array<float, kMeterTaps.size()> logLuminance{};
            std::array<float, kMeterTaps.size()> weights{};
            size_t sampleCount = 0;

            const int width = tex->getWidth();
            const int height = tex->getHeight();
            for(const MeterTap& tap : kMeterTaps){
                int x = Math3D::Clamp(static_cast<int>(std::round(tap.u * static_cast<float>(width - 1))), 0, width - 1);
                int y = Math3D::Clamp(static_cast<int>(std::round(tap.v * static_cast<float>(height - 1))), 0, height - 1);
                float pixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pixel);
                float luminance = (pixel[0] * 0.2126f) + (pixel[1] * 0.7152f) + (pixel[2] * 0.0722f);
                luminance = Math3D::Clamp(luminance, 0.0001f, 64.0f);
                logLuminance[sampleCount] = std::log2(luminance);
                weights[sampleCount] = tap.weight;
                sampleCount++;
            }

            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));

            if(sampleCount == 0){
                return false;
            }

            std::array<float, kMeterTaps.size()> sortedLog = logLuminance;
            std::sort(sortedLog.begin(), sortedLog.begin() + static_cast<int>(sampleCount));
            const size_t lowIndex = (sampleCount > 4) ? (sampleCount / 6) : 0;
            const size_t highIndex = (sampleCount > 4) ? (sampleCount - 1 - (sampleCount / 6)) : (sampleCount - 1);
            const float trimmedLow = sortedLog[lowIndex];
            const float trimmedHigh = sortedLog[highIndex];

            float weightedLog = 0.0f;
            float totalWeight = 0.0f;
            for(size_t i = 0; i < sampleCount; ++i){
                float clampedLog = Math3D::Clamp(logLuminance[i], trimmedLow, trimmedHigh);
                weightedLog += clampedLog * weights[i];
                totalWeight += weights[i];
            }
            if(totalWeight <= 0.0001f || !std::isfinite(weightedLog)){
                return false;
            }

            float meteredLuminance = std::exp2(weightedLog / totalWeight);
            if(!std::isfinite(meteredLuminance)){
                return false;
            }
            outLuminance = Math3D::Clamp(meteredLuminance, 0.0001f, 64.0f);
            return true;
        }

        /**
         * @brief Updates adaptation.
         * @param tex Value for tex.
         */
        void updateAdaptation(PTexture tex){
            if(adaptationInitialized){
                meteringFrameCounter = (meteringFrameCounter + 1) % METER_INTERVAL_FRAMES;
                if(meteringFrameCounter != 0){
                    return;
                }
            }else{
                meteringFrameCounter = 0;
            }

            float observedLuminance = 0.18f;
            if(!sampleSceneLuminance(tex, observedLuminance)){
                return;
            }

            const float minExp = Math3D::Clamp(minExposure, 0.01f, 64.0f);
            const float maxExp = Math3D::Clamp(maxExposure, minExp, 64.0f);
            const float compensationScale = std::pow(2.0f, exposureCompensation);
            auto now = std::chrono::steady_clock::now();

            if(!adaptationInitialized){
                filteredLogLuminance = std::log2(Math3D::Clamp(observedLuminance, 0.0001f, 64.0f));
                float targetExposure = (0.18f / Math3D::Max(observedLuminance, 0.0001f)) * compensationScale;
                adaptedExposure = Math3D::Clamp(targetExposure, minExp, maxExp);
                adaptationInitialized = true;
                meteringFrameCounter = METER_INTERVAL_FRAMES / 2;
                lastAdaptationTime = now;
                return;
            }

            float dt = std::chrono::duration<float>(now - lastAdaptationTime).count();
            dt = Math3D::Clamp(dt, 0.001f, 0.25f);
            lastAdaptationTime = now;

            float observedLog = std::log2(Math3D::Clamp(observedLuminance, 0.0001f, 64.0f));
            const float meteringFilterRate = 3.5f;
            const float filterAlpha = 1.0f - std::exp(-meteringFilterRate * dt);
            filteredLogLuminance += (observedLog - filteredLogLuminance) * filterAlpha;

            float filteredLuminance = std::exp2(filteredLogLuminance);
            float targetExposure = (0.18f / Math3D::Max(filteredLuminance, 0.0001f)) * compensationScale;
            targetExposure = Math3D::Clamp(targetExposure, minExp, maxExp);

            float currentExposure = Math3D::Clamp(adaptedExposure, minExp, maxExp);
            const float speedUp = Math3D::Max(adaptationSpeedUp, 0.01f);
            const float speedDown = Math3D::Max(adaptationSpeedDown, 0.01f);
            const float adaptationRate = (targetExposure > currentExposure) ? speedUp : speedDown;

            float currentEv = std::log2(Math3D::Max(currentExposure, 0.0001f));
            float targetEv = std::log2(Math3D::Max(targetExposure, 0.0001f));
            float evDelta = targetEv - currentEv;
            if(std::abs(evDelta) < 0.015f){
                evDelta = 0.0f;
            }
            float maxEvStep = Math3D::Max(0.001f, adaptationRate * dt);
            evDelta = Math3D::Clamp(evDelta, -maxEvStep, maxEvStep);
            float steppedExposure = std::exp2(currentEv + evDelta);
            adaptedExposure = Math3D::Clamp(steppedExposure, minExp, maxExp);
        }

        /**
         * @brief Checks whether ensure compiled.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ensureCompiled(){
            if(!shader){
                return false;
            }
            if(shader->getID() != 0){
                return true;
            }
            if(compileAttempted){
                return false;
            }
            compileAttempted = true;
            if(shader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile Auto Exposure shader:\n%s", shader->getLog().c_str());
                return false;
            }
            return true;
        }

    public:
        float minExposure = 0.6f;
        float maxExposure = 2.4f;
        float exposureCompensation = 0.0f;
        float adaptationSpeedUp = 1.2f;
        float adaptationSpeedDown = 0.7f;

        /**
         * @brief Constructs a new AutoExposureEffect instance.
         */
        AutoExposureEffect(){
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(AUTO_EXPOSURE_FRAG_SHADER);
        }

        /**
         * @brief Destroys this AutoExposureEffect instance.
         */
        ~AutoExposureEffect() override {
            if(meteringReadFbo != 0){
                glDeleteFramebuffers(1, &meteringReadFbo);
                meteringReadFbo = 0;
            }
        }

        /**
         * @brief Resets adaptation.
         */
        void resetAdaptation(){
            adaptationInitialized = false;
            adaptedExposure = 1.0f;
            filteredLogLuminance = std::log2(0.18f);
            meteringFrameCounter = 0;
            lastAdaptationTime = std::chrono::steady_clock::now();
        }

        /**
         * @brief Returns the current exposure.
         * @return Computed numeric result.
         */
        float getCurrentExposure() const{
            return adaptedExposure;
        }

        /**
         * @brief Applies current settings.
         * @param tex Value for tex.
         * @param PTexture Value for p texture.
         * @param outFbo Output value for fbo.
         * @param quad Value for quad.
         * @return True when the operation succeeds; otherwise false.
         */
        bool apply(PTexture tex, PTexture, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {
            if(!outFbo || !quad || !tex){
                return false;
            }
            if(!ensureCompiled()){
                return false;
            }

            updateAdaptation(tex);

            outFbo->bind();
            glDisable(GL_DEPTH_TEST);

            shader->bind();
            shader->setUniformFast("screenTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(tex, 0)));
            shader->setUniformFast("u_exposure", Uniform<float>(Math3D::Clamp(adaptedExposure, 0.01f, 64.0f)));

            static const Math3D::Mat4 IDENTITY;
            shader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));
            quad->draw(IDENTITY, IDENTITY, IDENTITY);
            outFbo->unbind();
            return true;
        }

        /**
         * @brief Creates a new object.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<AutoExposureEffect> New(){
            return std::make_shared<AutoExposureEffect>();
        }
};

/// @brief Represents the FXAAEffect type.
class FXAAEffect : public Graphics::PostProcessing::PostProcessingEffect {
    private:
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;
        const std::string FXAA_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D screenTexture;
            uniform vec2 u_texelSize;
            uniform float u_subpix;
            uniform float u_edgeThreshold;
            uniform float u_edgeThresholdMin;

            /**
             * @brief Computes luminance.
             * @param c Value for c.
             * @return Computed numeric result.
             */
            float luma(vec3 c){
                return dot(c, vec3(0.299, 0.587, 0.114));
            }

            /**
             * @brief Executes the main shader pass.
             */
            void main() {
                vec3 rgbNW = texture(screenTexture, TexCoords + vec2(-1.0, -1.0) * u_texelSize).rgb;
                vec3 rgbNE = texture(screenTexture, TexCoords + vec2( 1.0, -1.0) * u_texelSize).rgb;
                vec3 rgbSW = texture(screenTexture, TexCoords + vec2(-1.0,  1.0) * u_texelSize).rgb;
                vec3 rgbSE = texture(screenTexture, TexCoords + vec2( 1.0,  1.0) * u_texelSize).rgb;
                vec3 rgbM  = texture(screenTexture, TexCoords).rgb;

                float lumaNW = luma(rgbNW);
                float lumaNE = luma(rgbNE);
                float lumaSW = luma(rgbSW);
                float lumaSE = luma(rgbSE);
                float lumaM = luma(rgbM);

                float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
                float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
                float lumaRange = lumaMax - lumaMin;
                if(lumaRange < max(u_edgeThresholdMin, lumaMax * u_edgeThreshold)){
                    FragColor = vec4(rgbM, 1.0);
                    return;
                }

                vec2 dir;
                dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
                dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

                float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * u_subpix), 1.0 / 128.0);
                float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
                dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * u_texelSize;

                vec3 rgbA = 0.5 * (
                    texture(screenTexture, TexCoords + dir * (1.0 / 3.0 - 0.5)).rgb +
                    texture(screenTexture, TexCoords + dir * (2.0 / 3.0 - 0.5)).rgb
                );

                vec3 rgbB = rgbA * 0.5 + 0.25 * (
                    texture(screenTexture, TexCoords + dir * -0.5).rgb +
                    texture(screenTexture, TexCoords + dir *  0.5).rgb
                );

                float lumaB = luma(rgbB);
                if((lumaB < lumaMin) || (lumaB > lumaMax)){
                    FragColor = vec4(rgbA, 1.0);
                }else{
                    FragColor = vec4(rgbB, 1.0);
                }
            }
        )";

        /**
         * @brief Checks whether ensure compiled.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ensureCompiled(){
            if(!shader){
                return false;
            }
            if(shader->getID() != 0){
                return true;
            }
            if(compileAttempted){
                return false;
            }
            compileAttempted = true;
            if(shader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile FXAA shader:\n%s", shader->getLog().c_str());
                return false;
            }
            return true;
        }

    public:
        AntiAliasingPreset preset = AntiAliasingPreset::FXAA_Medium;

        /**
         * @brief Constructs a new FXAAEffect instance.
         */
        FXAAEffect() {
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(FXAA_FRAG_SHADER);
        }

        /**
         * @brief Applies current settings.
         * @param tex Value for tex.
         * @param PTexture Value for p texture.
         * @param outFbo Output value for fbo.
         * @param quad Value for quad.
         * @return True when the operation succeeds; otherwise false.
         */
        bool apply(PTexture tex, PTexture, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {
            if(!outFbo || !quad || !tex || preset == AntiAliasingPreset::Off){
                return false;
            }
            if(!ensureCompiled()){
                return false;
            }

            float subpix = 0.75f;
            float edgeThreshold = 0.166f;
            float edgeThresholdMin = 0.0833f;
            switch(preset){
                case AntiAliasingPreset::FXAA_Low:
                    subpix = 0.50f;
                    edgeThreshold = 0.250f;
                    edgeThresholdMin = 0.120f;
                    break;
                case AntiAliasingPreset::FXAA_Medium:
                    subpix = 0.75f;
                    edgeThreshold = 0.166f;
                    edgeThresholdMin = 0.0833f;
                    break;
                case AntiAliasingPreset::FXAA_High:
                    subpix = 1.00f;
                    edgeThreshold = 0.125f;
                    edgeThresholdMin = 0.0312f;
                    break;
                case AntiAliasingPreset::Off:
                default:
                    break;
            }

            outFbo->bind();
            glDisable(GL_DEPTH_TEST);

            shader->bind();
            shader->setUniformFast("screenTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(tex, 0)));
            shader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(Math3D::Vec2(1.0f / (float)outFbo->getWidth(), 1.0f / (float)outFbo->getHeight())));
            shader->setUniformFast("u_subpix", Uniform<float>(subpix));
            shader->setUniformFast("u_edgeThreshold", Uniform<float>(edgeThreshold));
            shader->setUniformFast("u_edgeThresholdMin", Uniform<float>(edgeThresholdMin));

            static const Math3D::Mat4 IDENTITY;
            shader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));
            quad->draw(IDENTITY, IDENTITY, IDENTITY);
            outFbo->unbind();
            return true;
        }

        /**
         * @brief Creates a new object.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<FXAAEffect> New() {
            return std::make_shared<FXAAEffect>();
        }
};

#endif //SCREENEFFECTS_H

