/**
 * @file src/Rendering/PostFX/LensFlareEffect.cpp
 * @brief Implementation for LensFlareEffect.
 */

#include "Rendering/PostFX/LensFlareEffect.h"

#include "Assets/Core/Asset.h"
#include "Assets/Descriptors/ImageAsset.h"
#include "Foundation/Logging/Logbot.h"
#include "Rendering/Core/Screen.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace {
    Math3D::Vec3 safeNormalizeVec3(const Math3D::Vec3& value, const Math3D::Vec3& fallback){
        if(!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)){
            return fallback;
        }
        if(value.length() <= Math3D::EPSILON){
            return fallback;
        }
        return value.normalize();
    }

    float max3(float a, float b, float c){
        return std::max(a, std::max(b, c));
    }

    float smoothStep(float edge0, float edge1, float x){
        if(std::abs(edge1 - edge0) <= 1e-6f){
            return (x >= edge1) ? 1.0f : 0.0f;
        }
        float t = (x - edge0) / (edge1 - edge0);
        t = Math3D::Clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

LensFlareEffect::LensFlareEffect(){
    compositeShader = std::make_shared<ShaderProgram>();
    compositeShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
    compositeShader->setFragmentShader(R"(
        #version 330 core

        out vec4 FragColor;
        in vec2 TexCoords;

        uniform sampler2D screenTexture;
        uniform vec2 u_texelSize;
        uniform float u_glareThreshold;
        uniform float u_glareIntensity;
        uniform float u_glareLengthPx;
        uniform float u_glareFalloff;

        vec3 prefilterBright(vec3 color, float threshold){
            float peak = max(color.r, max(color.g, color.b));
            if(peak <= threshold){
                return vec3(0.0);
            }
            float gain = (peak - threshold) / max(peak, 0.0001);
            return color * gain;
        }

        void main(){
            vec3 base = texture(screenTexture, TexCoords).rgb;
            vec3 glare = vec3(0.0);

            if(u_glareIntensity > 0.0001 && u_glareLengthPx > 0.5){
                const vec2 kDirections[3] = vec2[](
                    vec2(1.0, 0.0),
                    vec2(0.0, 1.0),
                    normalize(vec2(1.0, 1.0))
                );
                const float kDirectionWeights[3] = float[](1.00, 1.00, 1.15);
                const int kSamples = 4;

                for(int d = 0; d < 3; ++d){
                    vec3 directionAccum = vec3(0.0);
                    for(int i = 1; i <= kSamples; ++i){
                        float t = float(i) / float(kSamples);
                        float weight = pow(1.0 - t, max(u_glareFalloff, 0.05));
                        vec2 offset = kDirections[d] * u_texelSize * u_glareLengthPx * (t * t);
                        vec3 sampleA = texture(screenTexture, clamp(TexCoords + offset, vec2(0.0), vec2(1.0))).rgb;
                        vec3 sampleB = texture(screenTexture, clamp(TexCoords - offset, vec2(0.0), vec2(1.0))).rgb;
                        directionAccum += (prefilterBright(sampleA, u_glareThreshold) + prefilterBright(sampleB, u_glareThreshold)) * weight;
                    }
                    glare += directionAccum * kDirectionWeights[d];
                }

                glare *= u_glareIntensity / float(kSamples);
            }

            FragColor = vec4(max(base + glare, vec3(0.0)), 1.0);
        }
    )");

    spriteShader = std::make_shared<ShaderProgram>();
    spriteShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
    spriteShader->setFragmentShader(R"(
        #version 330 core

        out vec4 FragColor;
        in vec2 TexCoords;

        uniform sampler2D flareTexture;
        uniform vec2 u_screenSize;
        uniform vec2 u_sourceUv;
        uniform vec3 u_sourceTint;
        uniform float u_sourceIntensity;
        uniform float u_sizePx;
        uniform float u_axisPosition;
        uniform float u_centerFalloff;
        uniform int u_elementType;
        uniform int u_polygonSides;

        const float PI = 3.14159265359;

        float regularPolygonMask(vec2 p, int sides){
            float sideCount = float(max(sides, 3));
            float sector = (2.0 * PI) / sideCount;
            float halfSector = sector * 0.5;
            float angle = atan(p.y, p.x);
            float radius = length(p);
            float boundary = cos(PI / sideCount) /
                             max(cos(mod(angle + halfSector, sector) - halfSector), 0.0001);
            float signedDistance = radius - boundary;
            return 1.0 - smoothstep(0.0, 0.08, signedDistance);
        }

        void main(){
            vec2 centerUv = vec2(0.5, 0.5);
            vec2 axis = u_sourceUv - centerUv;
            vec2 elementUv = centerUv + axis * u_axisPosition;
            vec2 local = ((TexCoords - elementUv) * u_screenSize) / max(u_sizePx, 1.0);

            vec3 base = vec3(0.0);
            float mask = 0.0;
            if(u_elementType == 0){
                vec2 texUv = local * 0.5 + 0.5;
                if(texUv.x >= 0.0 && texUv.x <= 1.0 && texUv.y >= 0.0 && texUv.y <= 1.0){
                    base = texture(flareTexture, texUv).rgb;
                    mask = pow(max(0.0, 1.0 - length(local)), max(u_centerFalloff, 0.05));
                }
            }else{
                float polygon = regularPolygonMask(local, u_polygonSides);
                float fill = pow(max(0.0, 1.0 - length(local)), max(u_centerFalloff, 0.05));
                base = vec3(fill);
                mask = polygon;
            }

            vec3 flare = max(base, vec3(0.0)) * max(mask, 0.0) * u_sourceTint * u_sourceIntensity;
            FragColor = vec4(max(flare, vec3(0.0)), 1.0);
        }
    )");
}

LensFlareEffect::~LensFlareEffect(){
    if(depthReadFbo != 0){
        glDeleteFramebuffers(1, &depthReadFbo);
        depthReadFbo = 0;
    }
}

void LensFlareEffect::setEmitters(const std::vector<FlareEmitter>& newEmitters){
    emitters = newEmitters;
}

bool LensFlareEffect::ensureShadersCompiled(){
    if(!compositeShader || !spriteShader){
        return false;
    }

    if(compositeShader->getID() == 0 && !compositeCompileAttempted){
        compositeCompileAttempted = true;
        if(compositeShader->compile() == 0){
            LogBot.Log(LOG_ERRO, "Failed to compile LensFlare composite shader:\n%s", compositeShader->getLog().c_str());
            return false;
        }
    }
    if(spriteShader->getID() == 0 && !spriteCompileAttempted){
        spriteCompileAttempted = true;
        if(spriteShader->compile() == 0){
            LogBot.Log(LOG_ERRO, "Failed to compile LensFlare sprite shader:\n%s", spriteShader->getLog().c_str());
            return false;
        }
    }

    return compositeShader->getID() != 0 && spriteShader->getID() != 0;
}

bool LensFlareEffect::beginDepthRead(PTexture depthTex){
    depthReadPrepared = false;
    depthReadPrevFbo = 0;
    if(!depthTex || depthTex->getID() == 0 || depthTex->getWidth() <= 0 || depthTex->getHeight() <= 0){
        return false;
    }
    if(depthReadFbo == 0){
        glGenFramebuffers(1, &depthReadFbo);
    }
    if(depthReadFbo == 0){
        return false;
    }

    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &depthReadPrevFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, depthReadFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex->getID(), 0);
    glReadBuffer(GL_NONE);
    if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(depthReadPrevFbo));
        depthReadPrevFbo = 0;
        return false;
    }

    depthReadPrepared = true;
    return true;
}

void LensFlareEffect::endDepthRead(){
    if(depthReadPrepared){
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(depthReadPrevFbo));
    }
    depthReadPrepared = false;
    depthReadPrevFbo = 0;
}

LensFlareEffect::CachedFlareAsset* LensFlareEffect::resolveAsset(const std::string& assetRef){
    if(assetRef.empty()){
        return nullptr;
    }

    CachedFlareAsset& cache = assetCache[assetRef];
    const std::uint64_t currentRevision = AssetManager::Instance.getRevision(assetRef);
    if(cache.valid && cache.assetRevision == currentRevision){
        return &cache;
    }

    std::string error;
    if(!LensFlareAssetIO::LoadFromAssetRef(assetRef, cache.data, &error)){
        if(!cache.warned){
            LogBot.Log(LOG_WARN, "Failed to load lens flare asset '%s': %s", assetRef.c_str(), error.c_str());
            cache.warned = true;
        }
        cache.textures.clear();
        cache.textureRevisions.clear();
        cache.valid = false;
        cache.assetRevision = currentRevision;
        return &cache;
    }

    cache.textures.clear();
    cache.textureRevisions.clear();
    cache.valid = true;
    cache.warned = false;
    cache.assetRevision = currentRevision;
    return &cache;
}

PTexture LensFlareEffect::resolveTexture(CachedFlareAsset& asset, const std::string& textureRef){
    const std::string resolvedRef = textureRef;
    if(resolvedRef.empty()){
        return nullptr;
    }

    const std::uint64_t currentRevision = ImageAssetIO::GetTextureRefRevision(resolvedRef);
    auto it = asset.textures.find(resolvedRef);
    auto revisionIt = asset.textureRevisions.find(resolvedRef);
    if(it != asset.textures.end() &&
       revisionIt != asset.textureRevisions.end() &&
       revisionIt->second == currentRevision){
        return it->second;
    }

    PTexture texture = ImageAssetIO::InstantiateTextureFromRef(resolvedRef);
    if(!texture && !asset.warned){
        LogBot.Log(LOG_WARN, "Lens flare asset '%s' could not resolve a usable image element texture.", asset.data.name.c_str());
        asset.warned = true;
    }

    asset.textures[resolvedRef] = texture;
    asset.textureRevisions[resolvedRef] = currentRevision;
    return texture;
}

float LensFlareEffect::sampleDepthAtUv(PTexture depthTex, const Math3D::Vec2& uv){
    if(!depthTex || depthTex->getID() == 0 || depthTex->getWidth() <= 0 || depthTex->getHeight() <= 0){
        return 1.0f;
    }

    const int width = depthTex->getWidth();
    const int height = depthTex->getHeight();
    const int x = Math3D::Clamp(static_cast<int>(std::round(uv.x * static_cast<float>(width - 1))), 0, width - 1);
    const int y = Math3D::Clamp(static_cast<int>(std::round(uv.y * static_cast<float>(height - 1))), 0, height - 1);

    float sampledDepth = 1.0f;
    if(depthReadPrepared){
        glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &sampledDepth);
        return sampledDepth;
    }

    if(!beginDepthRead(depthTex)){
        return 1.0f;
    }
    if(depthReadPrepared){
        glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &sampledDepth);
    }
    endDepthRead();
    return sampledDepth;
}

bool LensFlareEffect::buildProjectedEmitterSource(const FlareEmitter& emitter,
                                                  PTexture depthTex,
                                                  ProjectedEmitterSource& outSource){
    PCamera camera = Screen::GetCurrentCamera();
    if(!camera){
        return false;
    }

    Math3D::Vec3 worldPosition = emitter.position;
    if(emitter.type == LightType::DIRECTIONAL){
        const Math3D::Vec3 sunDirection = safeNormalizeVec3(emitter.direction * -1.0f, Math3D::Vec3(0.0f, -1.0f, 0.0f));
        Math3D::Vec3 cameraForward = camera->transform().forward() * -1.0f;
        cameraForward = safeNormalizeVec3(cameraForward, Math3D::Vec3(0.0f, 0.0f, -1.0f));
        if(Math3D::Vec3::dot(cameraForward, sunDirection) <= 0.02f){
            return false;
        }
        const float sourceDistance = Math3D::Max(camera->getSettings().farPlane * 0.80f, 100.0f);
        worldPosition = camera->transform().position + (sunDirection * sourceDistance);
    }

    const glm::mat4 viewMat = static_cast<glm::mat4>(camera->getViewMatrix());
    const glm::mat4 projMat = static_cast<glm::mat4>(camera->getProjectionMatrix());
    const glm::vec4 clip = projMat * viewMat * glm::vec4(worldPosition.x, worldPosition.y, worldPosition.z, 1.0f);
    if(clip.w <= 1e-5f){
        return false;
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if(ndc.z < -1.0f || ndc.z > 1.0f || ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f){
        return false;
    }

    const Math3D::Vec2 uv(
        (ndc.x * 0.5f) + 0.5f,
        (ndc.y * 0.5f) + 0.5f
    );
    const float sourceDepth = (ndc.z * 0.5f) + 0.5f;
    const float sceneDepth = sampleDepthAtUv(depthTex, uv);

    float visibility = 1.0f;
    if(emitter.type == LightType::DIRECTIONAL){
        visibility *= smoothStep(0.997f, 0.99995f, sceneDepth);
    }else{
        visibility *= (sourceDepth <= (sceneDepth + 0.0035f)) ? 1.0f : 0.0f;
    }

    const Math3D::Vec2 toCenter = uv - Math3D::Vec2(0.5f, 0.5f);
    const float edgeDistance = toCenter.length() * 2.0f;
    visibility *= 1.0f - smoothStep(0.72f, 1.12f, edgeDistance);

    const float lightBrightness =
        max3(std::max(emitter.color.x, 0.0f), std::max(emitter.color.y, 0.0f), std::max(emitter.color.z, 0.0f)) *
        Math3D::Max(emitter.intensity, 0.0f);
    visibility *= smoothStep(0.05f, 0.25f, lightBrightness);
    if(visibility <= 0.0001f){
        return false;
    }

    const float brightnessScale = std::sqrt(Math3D::Clamp(lightBrightness, 0.05f, 64.0f));
    outSource.uv = Math3D::Vec2(uv.x, uv.y);
    outSource.tint = Math3D::Vec3(
        Math3D::Max(emitter.color.x, 0.0f),
        Math3D::Max(emitter.color.y, 0.0f),
        Math3D::Max(emitter.color.z, 0.0f)
    );
    outSource.visibility = visibility;
    outSource.brightnessScale = brightnessScale;
    return true;
}

void LensFlareEffect::appendSpriteSources(const FlareEmitter& emitter,
                                          CachedFlareAsset& asset,
                                          const ProjectedEmitterSource& emitterSource,
                                          std::vector<SpritePassSource>& outSources){
    if(!asset.valid || asset.data.elements.empty()){
        return;
    }

    float baseSizePx = Math3D::Clamp(asset.data.spriteScale * (0.45f + (emitterSource.brightnessScale * 0.58f)), 28.0f, 420.0f);
    if(emitter.type == LightType::DIRECTIONAL){
        baseSizePx *= 1.20f;
    }
    const float baseIntensity = asset.data.intensity * emitterSource.visibility * (0.22f + (emitterSource.brightnessScale * 0.26f));

    for(const LensFlareElementData& element : asset.data.elements){
        SpritePassSource source;
        source.type = element.type;
        if(source.type == LensFlareElementType::Circle){
            source.type = LensFlareElementType::Polygon;
            source.polygonSides = 64;
        }else{
            source.polygonSides = std::max(element.polygonSides, 3);
        }

        if(source.type == LensFlareElementType::Image){
            source.texture = resolveTexture(asset, element.textureRef);
            if(!source.texture || source.texture->getID() == 0){
                continue;
            }
        }

        source.sourceUv = emitterSource.uv;
        source.tint = Math3D::Vec3(
            asset.data.tint.x * element.tint.x * emitterSource.tint.x,
            asset.data.tint.y * element.tint.y * emitterSource.tint.y,
            asset.data.tint.z * element.tint.z * emitterSource.tint.z
        );
        source.intensity = baseIntensity * Math3D::Clamp(element.intensity, 0.0f, 8.0f);
        source.sizePx = Math3D::Clamp(baseSizePx * Math3D::Clamp(element.sizeScale, 0.05f, 8.0f), 6.0f, 1024.0f);
        source.axisPosition = Math3D::Clamp(element.axisPosition, -3.0f, 3.0f);
        source.centerFalloff = (source.type == LensFlareElementType::Image) ? 1.20f : 0.65f;
        outSources.push_back(std::move(source));
    }
}

bool LensFlareEffect::apply(PTexture inputTex,
                            PTexture depthTex,
                            PFrameBuffer frameBuffer,
                            std::shared_ptr<ModelPart> quad){
    if(!inputTex || !frameBuffer || !quad || emitters.empty()){
        return false;
    }
    if(!ensureShadersCompiled()){
        return false;
    }

    std::vector<SpritePassSource> spriteSources;
    spriteSources.reserve(emitters.size());

    GlareSettings glare{};
    bool hasAnyValidAsset = false;
    float glareThresholdAccumulator = 0.0f;
    float glareFalloffAccumulator = 0.0f;
    int glareCount = 0;
    beginDepthRead(depthTex);

    for(const FlareEmitter& emitter : emitters){
        CachedFlareAsset* asset = resolveAsset(emitter.assetRef);
        if(!asset || !asset->valid){
            continue;
        }
        hasAnyValidAsset = true;

        ProjectedEmitterSource emitterSource;
        if(buildProjectedEmitterSource(emitter, depthTex, emitterSource)){
            glare.enabled = true;
            const float glareVisibilityScale = Math3D::Clamp(
                emitterSource.visibility * (0.50f + (emitterSource.brightnessScale * 0.35f)),
                0.0f,
                4.0f
            );
            glare.intensity = Math3D::Max(
                glare.intensity,
                Math3D::Max(asset->data.glareIntensity, 0.0f) * glareVisibilityScale
            );
            glare.lengthPx = Math3D::Max(glare.lengthPx, Math3D::Max(asset->data.glareLengthPx, 0.0f));
            glareThresholdAccumulator += Math3D::Clamp(asset->data.glareThreshold, 0.0f, 64.0f);
            glareFalloffAccumulator += Math3D::Clamp(asset->data.glareFalloff, 0.05f, 8.0f);
            glareCount++;
            appendSpriteSources(emitter, *asset, emitterSource, spriteSources);
        }
    }
    endDepthRead();

    if(!hasAnyValidAsset){
        return false;
    }

    if(glareCount > 0){
        glare.threshold = glareThresholdAccumulator / static_cast<float>(glareCount);
        glare.falloff = glareFalloffAccumulator / static_cast<float>(glareCount);
    }
    const bool hasGlarePass = glare.enabled && glare.intensity > 0.0001f && glare.lengthPx > 0.5f;
    if(spriteSources.empty() && !hasGlarePass){
        return false;
    }

    frameBuffer->bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    static const Math3D::Mat4 IDENTITY;

    compositeShader->bind();
    compositeShader->setUniformFast("screenTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(inputTex, 0)));
    compositeShader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(Math3D::Vec2(
        1.0f / static_cast<float>(Math3D::Max(frameBuffer->getWidth(), 1)),
        1.0f / static_cast<float>(Math3D::Max(frameBuffer->getHeight(), 1))
    )));
    compositeShader->setUniformFast("u_glareThreshold", Uniform<float>(hasGlarePass ? glare.threshold : 64.0f));
    compositeShader->setUniformFast("u_glareIntensity", Uniform<float>(hasGlarePass ? glare.intensity : 0.0f));
    compositeShader->setUniformFast("u_glareLengthPx", Uniform<float>(hasGlarePass ? glare.lengthPx : 0.0f));
    compositeShader->setUniformFast("u_glareFalloff", Uniform<float>(hasGlarePass ? glare.falloff : 1.35f));
    compositeShader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
    compositeShader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
    compositeShader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));
    quad->draw(IDENTITY, IDENTITY, IDENTITY);

    if(!spriteSources.empty()){
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);

        spriteShader->bind();
        spriteShader->setUniformFast("u_screenSize", Uniform<Math3D::Vec2>(Math3D::Vec2(
            static_cast<float>(Math3D::Max(frameBuffer->getWidth(), 1)),
            static_cast<float>(Math3D::Max(frameBuffer->getHeight(), 1))
        )));
        spriteShader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
        spriteShader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
        spriteShader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));

        for(const SpritePassSource& source : spriteSources){
            if(source.intensity <= 0.0001f || source.sizePx <= 1.0f){
                continue;
            }
            if(source.type == LensFlareElementType::Image && (!source.texture || source.texture->getID() == 0)){
                continue;
            }

            if(source.texture){
                spriteShader->setUniformFast("flareTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(source.texture, 0)));
            }
            spriteShader->setUniformFast("u_sourceUv", Uniform<Math3D::Vec2>(source.sourceUv));
            spriteShader->setUniformFast("u_sourceTint", Uniform<Math3D::Vec3>(source.tint));
            spriteShader->setUniformFast("u_sourceIntensity", Uniform<float>(source.intensity));
            spriteShader->setUniformFast("u_sizePx", Uniform<float>(source.sizePx));
            spriteShader->setUniformFast("u_axisPosition", Uniform<float>(source.axisPosition));
            spriteShader->setUniformFast("u_centerFalloff", Uniform<float>(source.centerFalloff));
            spriteShader->setUniformFast("u_elementType", Uniform<int>(source.type == LensFlareElementType::Image ? 0 : 1));
            spriteShader->setUniformFast("u_polygonSides", Uniform<int>(source.polygonSides));
            quad->draw(IDENTITY, IDENTITY, IDENTITY);
        }

        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    frameBuffer->unbind();
    return true;
}
