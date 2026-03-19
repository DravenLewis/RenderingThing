/**
 * @file src/Rendering/PostFX/LensFlareEffect.h
 * @brief Declarations for LensFlareEffect.
 */

#ifndef LENS_FLARE_EFFECT_H
#define LENS_FLARE_EFFECT_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glad/glad.h>

#include "Assets/Descriptors/LensFlareAsset.h"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Core/Graphics.h"
#include "Rendering/Lighting/Light.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Rendering/Textures/Texture.h"

/// @brief Represents the LensFlareEffect type.
class LensFlareEffect : public Graphics::PostProcessing::PostProcessingEffect {
    public:
        /// @brief Holds data for FlareEmitter.
        struct FlareEmitter {
            LightType type = LightType::POINT;
            Math3D::Vec3 position = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec3 direction = Math3D::Vec3(0.0f, -1.0f, 0.0f);
            Math3D::Vec4 color = Math3D::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
            float intensity = 1.0f;
            std::string assetRef;
        };

        LensFlareEffect();
        ~LensFlareEffect() override;

        /**
         * @brief Replaces the current flare-emitter list.
         * @param newEmitters Emitters used for the next frame.
         */
        void setEmitters(const std::vector<FlareEmitter>& newEmitters);

        bool apply(
            PTexture inputTex,
            PTexture depthTex,
            PFrameBuffer frameBuffer,
            std::shared_ptr<ModelPart> quad
        ) override;

    private:
        /// @brief Holds data for CachedFlareAsset.
        struct CachedFlareAsset {
            LensFlareAssetData data;
            std::unordered_map<std::string, PTexture> textures;
            std::unordered_map<std::string, std::uint64_t> textureRevisions;
            bool valid = false;
            bool warned = false;
            std::uint64_t assetRevision = 0;
        };

        /// @brief Holds data for projected emitter placement on screen.
        struct ProjectedEmitterSource {
            Math3D::Vec2 uv = Math3D::Vec2(0.5f, 0.5f);
            Math3D::Vec3 tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
            float visibility = 1.0f;
            float brightnessScale = 1.0f;
        };

        /// @brief Holds data for SpritePassSource.
        struct SpritePassSource {
            LensFlareElementType type = LensFlareElementType::Image;
            PTexture texture;
            Math3D::Vec2 sourceUv = Math3D::Vec2(0.5f, 0.5f);
            Math3D::Vec3 tint = Math3D::Vec3(1.0f, 1.0f, 1.0f);
            float intensity = 1.0f;
            float sizePx = 96.0f;
            float axisPosition = 1.0f;
            float centerFalloff = 1.0f;
            int polygonSides = 6;
        };

        /// @brief Holds data for GlareSettings.
        struct GlareSettings {
            bool enabled = false;
            float threshold = 1.0f;
            float intensity = 0.0f;
            float lengthPx = 0.0f;
            float falloff = 1.35f;
        };

        std::vector<FlareEmitter> emitters;
        std::unordered_map<std::string, CachedFlareAsset> assetCache;
        std::shared_ptr<ShaderProgram> compositeShader;
        std::shared_ptr<ShaderProgram> spriteShader;
        bool compositeCompileAttempted = false;
        bool spriteCompileAttempted = false;
        GLuint depthReadFbo = 0;
        GLint depthReadPrevFbo = 0;
        bool depthReadPrepared = false;

        bool ensureShadersCompiled();
        bool beginDepthRead(PTexture depthTex);
        void endDepthRead();
        CachedFlareAsset* resolveAsset(const std::string& assetRef);
        PTexture resolveTexture(CachedFlareAsset& asset, const std::string& textureRef);
        float sampleDepthAtUv(PTexture depthTex, const Math3D::Vec2& uv);
        bool buildProjectedEmitterSource(const FlareEmitter& emitter,
                                         PTexture depthTex,
                                         ProjectedEmitterSource& outSource);
        void appendSpriteSources(const FlareEmitter& emitter,
                                 CachedFlareAsset& asset,
                                 const ProjectedEmitterSource& emitterSource,
                                 std::vector<SpritePassSource>& outSources);
};

#endif // LENS_FLARE_EFFECT_H
