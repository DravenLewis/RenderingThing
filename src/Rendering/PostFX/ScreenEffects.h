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
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
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

/// @brief Represents the SSAOEffect type.
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
            uniform vec2 u_texelSize;
            uniform float u_radiusPx;
            uniform float u_depthRadius;
            uniform float u_bias;
            uniform float u_intensity;
            uniform float u_giBoost;
            uniform int u_sampleCount;
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

            /**
             * @brief Executes the main shader pass.
             */
            void main() {
                vec4 base = texture(screenTexture, TexCoords);
                float centerDepthRaw = texture(depthTexture, TexCoords).r;
                // Treat near-far depth as background/sky and skip AO to avoid horizon halos.
                const float kSkyDepthCutoff = 0.9995;
                if(centerDepthRaw >= kSkyDepthCutoff){
                    FragColor = base;
                    return;
                }
                float centerDepth = linearizeDepth(centerDepthRaw);

                float radiusPx = max(u_radiusPx, 0.25);
                float depthRadius = max(u_depthRadius, 0.00005);
                int count = clamp(u_sampleCount, 1, 16);
                // Use pixel-stable noise to avoid UV-space stripe patterns on large flat surfaces.
                float angle = hash12(floor(gl_FragCoord.xy)) * 6.2831853;
                float sinAngle = sin(angle);
                float cosAngle = cos(angle);

                // Scale thresholds with distance and local depth slope for temporal stability.
                // Avoid derivative-based slope (dFdx/dFdy), which can introduce primitive-edge seams.
                float depthScaleTarget = clamp(centerDepth * 0.08, 1.0, 32.0);
                // Smoothly ramp depth scaling to avoid a visible contour where distance scaling kicks in.
                float depthScaleBlend = smoothstep(4.0, 28.0, centerDepth);
                float depthScale = mix(1.0, depthScaleTarget, depthScaleBlend);
                float adaptiveDepthRadius = depthRadius * depthScale;
                float depthRightRaw = texture(depthTexture, clamp(TexCoords + vec2(u_texelSize.x, 0.0), vec2(0.0), vec2(1.0))).r;
                float depthUpRaw = texture(depthTexture, clamp(TexCoords + vec2(0.0, u_texelSize.y), vec2(0.0), vec2(1.0))).r;
                float depthRight = (depthRightRaw >= kSkyDepthCutoff) ? centerDepth : linearizeDepth(depthRightRaw);
                float depthUp = (depthUpRaw >= kSkyDepthCutoff) ? centerDepth : linearizeDepth(depthUpRaw);
                float depthSlope = abs(depthRight - centerDepth) + abs(depthUp - centerDepth);
                float baseBias = max(u_bias * depthScale, adaptiveDepthRadius * 0.10);

                float occ = 0.0;
                float weightSum = 0.0;
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
                        vec2 uv = TexCoords + (pixelOffset * u_texelSize);
                        if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0){
                            continue;
                        }
                        float sampleDepthRaw = texture(depthTexture, uv).r;
                        if(sampleDepthRaw >= kSkyDepthCutoff){
                            continue;
                        }
                        float sampleDepth = linearizeDepth(sampleDepthRaw);

                        float depthDelta = centerDepth - sampleDepth;
                        float slopeAllowance = depthSlope * length(pixelOffset);
                        float adjustedDelta = depthDelta - (baseBias + slopeAllowance);
                        float sampleOcc = smoothstep(0.0, adaptiveDepthRadius, adjustedDelta);
                        float radialBias = 1.0 - smoothstep(0.35, 1.0, ringT);
                        sampleOcc *= mix(1.0, 1.35, radialBias);
                        // Weight by raw geometric proximity so non-occluding neighbors still normalize.
                        float rangeWeight = 1.0 - smoothstep(0.0, adaptiveDepthRadius * 2.5, abs(depthDelta));
                        occ += sampleOcc * rangeWeight;
                        weightSum += rangeWeight;
                    }
                }

                occ = clamp(occ / max(weightSum, 0.0001), 0.0, 1.0);
                occ = pow(occ, 0.85);
                float effectiveIntensity = pow(max(u_intensity, 0.0), 0.6);
                // Keep a floor so SSAO cannot collapse the whole frame into near-black.
                float ao = clamp(1.0 - (occ * effectiveIntensity), 0.35, 1.0);

                float giFactor = (1.0 - ao);
                giFactor = giFactor * giFactor * u_giBoost;
                vec3 giBounce = base.rgb * giFactor;
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
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);

            shader->bind();
            shader->setUniformFast("screenTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(tex, 0)));
            shader->setUniformFast("depthTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(depthTex, 1)));
            shader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(Math3D::Vec2(1.0f / (float)outFbo->getWidth(), 1.0f / (float)outFbo->getHeight())));
            shader->setUniformFast("u_radiusPx", Uniform<float>(radiusPx));
            shader->setUniformFast("u_depthRadius", Uniform<float>(depthRadius));
            shader->setUniformFast("u_bias", Uniform<float>(bias));
            shader->setUniformFast("u_intensity", Uniform<float>(intensity));
            shader->setUniformFast("u_giBoost", Uniform<float>(giBoost));
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

/// @brief Represents the DepthOfFieldEffect type.
class DepthOfFieldEffect : public Graphics::PostProcessing::PostProcessingEffect {
    private:
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;
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
        std::vector<float> adaptiveFocusDepthCpu;
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

            int width = 0;
            int height = 0;
            GLint prevTexBinding = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexBinding);
            glBindTexture(GL_TEXTURE_2D, depthTex->getID());
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
            if(width <= 0 || height <= 0){
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexBinding));
                return false;
            }

            size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            if(pixelCount == 0){
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexBinding));
                return false;
            }
            if(adaptiveFocusDepthCpu.size() != pixelCount){
                adaptiveFocusDepthCpu.resize(pixelCount);
            }
            if(adaptiveFocusDepthCpu.empty()){
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexBinding));
                return false;
            }

            GLenum preReadErr = glGetError();
            (void)preReadErr;
            glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, adaptiveFocusDepthCpu.data());
            GLenum readErr = glGetError();
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexBinding));
            if(readErr != GL_NO_ERROR){
                return false;
            }

            const float kSkyDepthCutoff = 0.9995f;
            int x = Math3D::Clamp(static_cast<int>(std::round(focusUv.x * static_cast<float>(width - 1))), 0, width - 1);
            int y = Math3D::Clamp(static_cast<int>(std::round(focusUv.y * static_cast<float>(height - 1))), 0, height - 1);
            float depth = adaptiveFocusDepthCpu[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
            if(!std::isfinite(depth) || depth >= kSkyDepthCutoff){
                return false;
            }
            outDistance = linearizeDepthCpu(depth);
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
        ~DepthOfFieldEffect() override = default;

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
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
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
                if(sampleAdaptiveFocusDistance(depthTex, sampledFocusDistance)){
                    targetFocusPlaneDistance = Math3D::Max(0.01f, sampledFocusDistance);
                    hasRayFocusTarget = true;
                    debugAdaptiveCenterValid = true;
                    debugAdaptiveCenterDistance = targetFocusPlaneDistance;
                }else if(useExternalAdaptiveFocus && std::isfinite(externalAdaptiveDistance) && externalAdaptiveDistance > 0.0f){
                    targetFocusPlaneDistance = Math3D::Max(0.01f, externalAdaptiveDistance);
                    hasRayFocusTarget = true;
                    debugAdaptiveRayValid = true;
                    debugAdaptiveRayDistance = targetFocusPlaneDistance;
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
                outThresholdScale = smoothedThresholdScale;
                outIntensityScale = smoothedIntensityScale;
                outExposureScale = smoothedExposureScale;
                return;
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
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);

            shader->bind();
            shader->setUniformFast("screenTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(tex, 0)));
            shader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(Math3D::Vec2(1.0f / (float)outFbo->getWidth(), 1.0f / (float)outFbo->getHeight())));
            float thresholdScale = Math3D::Clamp(autoExposureThresholdScale, 0.25f, 4.0f);
            float intensityScale = Math3D::Clamp(autoExposureIntensityScale, 0.25f, 4.0f);
            float exposureScale = 1.0f;
            float adaptiveThresholdScale = 1.0f;
            float adaptiveIntensityScale = 1.0f;
            computeAdaptiveScales(tex, adaptiveThresholdScale, adaptiveIntensityScale, exposureScale);
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
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;
        GLuint meteringReadFbo = 0;
        bool adaptationInitialized = false;
        float adaptedExposure = 1.0f;
        float filteredLogLuminance = std::log2(0.18f);
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

            static const std::array<MeterTap, 13> kMeterTaps = {{
                {0.50f, 0.50f, 0.22f},
                {0.35f, 0.35f, 0.10f},
                {0.65f, 0.35f, 0.10f},
                {0.35f, 0.65f, 0.10f},
                {0.65f, 0.65f, 0.10f},
                {0.12f, 0.12f, 0.07f},
                {0.88f, 0.12f, 0.07f},
                {0.12f, 0.88f, 0.07f},
                {0.88f, 0.88f, 0.07f},
                {0.50f, 0.20f, 0.025f},
                {0.20f, 0.50f, 0.025f},
                {0.80f, 0.50f, 0.025f},
                {0.50f, 0.80f, 0.025f}
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
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
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
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
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
