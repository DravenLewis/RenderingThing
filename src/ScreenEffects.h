#ifndef SCREENEFFECTS_H
#define SCREENEFFECTS_H

#include "Graphics.h"
#include "ShaderProgram.h"
#include "Color.h"
#include "Logbot.h"

enum class AntiAliasingPreset {
    Off = 0,
    FXAA_Low,
    FXAA_Medium,
    FXAA_High
};

class GrayscaleEffect : public Graphics::PostProcessing::PostProcessingEffect{
    private:
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;

        const std::string GRAYSCALE_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;
            uniform sampler2D screenTexture;
            
            void main() {
                vec4 col = texture(screenTexture, TexCoords);
                float avg = 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
                FragColor = vec4(avg, avg, avg, col.a);
            }
        )";

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

        GrayscaleEffect(){
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(GRAYSCALE_SHADER);
        }

        bool apply(PTexture tex, PTexture depthTex, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {
            (void)depthTex;
            if(!tex || !outFbo || !quad){
                return false;
            }
            if(!ensureCompiled()){
                return false;
            }

            outFbo->bind();
            outFbo->clear();
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

        static Graphics::PostProcessing::PPostProcessingEffect New(){
            return std::make_shared<GrayscaleEffect>();
        }
};

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
                vec2( 0.14383161, -0.14100790)
            );

            float linearizeDepth(float depth){
                float z = depth * 2.0 - 1.0;
                return (2.0 * u_nearPlane * u_farPlane) / max((u_farPlane + u_nearPlane) - (z * (u_farPlane - u_nearPlane)), 0.0001);
            }

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

                // Scale thresholds with distance and local depth slope for temporal stability.
                float depthScale = clamp(centerDepth * 0.08, 1.0, 32.0);
                float adaptiveDepthRadius = depthRadius * depthScale;
                float depthSlope = abs(dFdx(centerDepth)) + abs(dFdy(centerDepth));
                float baseBias = max(u_bias * depthScale, adaptiveDepthRadius * 0.08);

                float occ = 0.0;
                float weightSum = 0.0;
                for(int i = 0; i < count; ++i){
                    vec2 pixelOffset = kPoisson[i] * radiusPx;
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
                    // Weight by raw geometric proximity so non-occluding neighbors still normalize.
                    float rangeWeight = 1.0 - smoothstep(0.0, adaptiveDepthRadius * 2.0, abs(depthDelta));
                    occ += sampleOcc * rangeWeight;
                    weightSum += rangeWeight;
                }

                occ /= max(weightSum, 0.0001);
                // Keep a floor so SSAO cannot collapse the whole frame into near-black.
                float ao = clamp(1.0 - (occ * u_intensity), 0.45, 1.0);

                float giFactor = (1.0 - ao);
                giFactor = giFactor * giFactor * u_giBoost;
                vec3 giBounce = base.rgb * giFactor;
                vec3 finalColor = (base.rgb * ao) + giBounce;
                FragColor = vec4(finalColor, base.a);
            }
        )";

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

        SSAOEffect() {
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(SSAO_FRAG_SHADER);
        }

        bool apply(PTexture tex, PTexture depthTex, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {
            if(!outFbo || !quad || !tex || !depthTex){
                return false;
            }
            if(!ensureCompiled()){
                return false;
            }

            outFbo->bind();
            outFbo->clear(Color::CLEAR);
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

        static std::shared_ptr<SSAOEffect> New() {
            return std::make_shared<SSAOEffect>();
        }
};

class DepthOfFieldEffect : public Graphics::PostProcessing::PostProcessingEffect {
    private:
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;
        const std::string DOF_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D screenTexture;
            uniform sampler2D depthTexture;
            uniform vec2 u_texelSize;
            uniform float u_focusDistance;
            uniform float u_focusRange;
            uniform float u_blurStrength;
            uniform float u_maxBlurPx;
            uniform int u_sampleCount;
            uniform float u_nearPlane;
            uniform float u_farPlane;

            float linearizeDepth(float depth){
                float z = depth * 2.0 - 1.0;
                return (2.0 * u_nearPlane * u_farPlane) / max((u_farPlane + u_nearPlane) - (z * (u_farPlane - u_nearPlane)), 0.0001);
            }

            vec3 gatherBlur(vec2 uv, float radiusPx, int sampleCount, float centerLinearDepth){
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
                float depthTolerance = max(u_focusRange * 0.35, 0.25);
                for(int i = 0; i < loops; ++i){
                    vec2 offset = dirs[i] * radiusPx * u_texelSize;
                    vec2 sampleUv = uv + offset;
                    vec3 sampleColor = texture(screenTexture, sampleUv).rgb;
                    float sampleLinearDepth = linearizeDepth(texture(depthTexture, sampleUv).r);
                    float depthDelta = abs(sampleLinearDepth - centerLinearDepth);
                    float depthWeight = 1.0 - smoothstep(0.0, depthTolerance, depthDelta);
                    accum += sampleColor * depthWeight;
                    weight += depthWeight;
                }
                return accum / max(weight, 0.0001);
            }

            void main() {
                vec4 base = texture(screenTexture, TexCoords);
                float depth = texture(depthTexture, TexCoords).r;
                float linearDepth = linearizeDepth(depth);

                float focusRange = max(u_focusRange, 0.001);
                float coc = clamp(abs(linearDepth - u_focusDistance) / focusRange, 0.0, 1.0);
                coc = clamp(coc * u_blurStrength, 0.0, 1.0);
                coc = coc * coc;

                float blurRadius = coc * max(u_maxBlurPx, 0.0);
                vec3 blurred = gatherBlur(TexCoords, blurRadius, u_sampleCount, linearDepth);
                vec3 color = mix(base.rgb, blurred, coc);
                FragColor = vec4(color, base.a);
            }
        )";

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

    public:
        float focusDistance = 8.0f;
        float focusRange = 4.0f;
        float blurStrength = 0.65f;
        float maxBlurPx = 7.0f;
        int sampleCount = 6;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;

        DepthOfFieldEffect() {
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(DOF_FRAG_SHADER);
        }

        bool apply(PTexture tex, PTexture depthTex, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {
            if(!outFbo || !quad || !tex || !depthTex){
                return false;
            }
            if(!ensureCompiled()){
                return false;
            }

            outFbo->bind();
            outFbo->clear(Color::CLEAR);
            glDisable(GL_DEPTH_TEST);

            shader->bind();
            shader->setUniformFast("screenTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(tex, 0)));
            shader->setUniformFast("depthTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(depthTex, 1)));
            shader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(Math3D::Vec2(1.0f / (float)outFbo->getWidth(), 1.0f / (float)outFbo->getHeight())));
            shader->setUniformFast("u_focusDistance", Uniform<float>(focusDistance));
            shader->setUniformFast("u_focusRange", Uniform<float>(focusRange));
            shader->setUniformFast("u_blurStrength", Uniform<float>(blurStrength));
            shader->setUniformFast("u_maxBlurPx", Uniform<float>(maxBlurPx));
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

        static std::shared_ptr<DepthOfFieldEffect> New() {
            return std::make_shared<DepthOfFieldEffect>();
        }
};

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

            float luma(vec3 c){
                return dot(c, vec3(0.299, 0.587, 0.114));
            }

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

        FXAAEffect() {
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(FXAA_FRAG_SHADER);
        }

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
            outFbo->clear(Color::CLEAR);
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

        static std::shared_ptr<FXAAEffect> New() {
            return std::make_shared<FXAAEffect>();
        }
};

#endif //SCREENEFFECTS_H
