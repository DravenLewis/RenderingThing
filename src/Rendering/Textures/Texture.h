/**
 * @file src/Rendering/Textures/Texture.h
 * @brief Declarations for Texture.
 */

#ifndef TEXTURE_H
#define TEXTURE_H

#include <glad/glad.h>
#include <string>
#include <memory>
#include <vector>

#include "Assets/Core/Asset.h"
#include "Foundation/Math/Color.h"

namespace Graphics { // Hollow Structure of the Graphics Class.
    namespace Image{
        class Image;
        class BufferedImage;
    }
}

enum class TextureFilterMode{
    NEAREST,
    LINEAR,
    TRILINEAR
};

enum class TextureWrapMode{
    REPEAT,
    CLAMP_EDGE,
    CLAMP_BORDER
};

/// @brief Represents the Texture type.
class Texture{
    private:
        GLuint textureID;
        std::shared_ptr<Graphics::Image::Image> cpuImage;
        int width, height;
        bool ownsTexture = true;
        std::string sourceAssetRef;
    public:

        /**
         * @brief Constructs a new Texture instance.
         */
        Texture() : textureID(0), width(0), height(0){};
        ~Texture();
        Texture(std::shared_ptr<Graphics::Image::Image> image, GLenum imageHint = GL_TEXTURE_2D);
        Texture(const Texture&) = delete; // dont allow copying Texture tex = val.getTexture(); is invald Texture* tex = val.getTexturePtr() is okay tho.
        Texture(int width, int height) : textureID(0), width(width), height(height) {};

        void bind(unsigned int slot = 0) const;
        void unbind() const;
        void dispose();
        void setFilterMode(TextureFilterMode mode, bool generateMipmaps = false);
        void setWrapMode(TextureWrapMode mode);
        void setBorderColor(const Color& color);

        inline int getWidth() {return this->width;}
        inline int getHeight() {return this->height;}
        GLuint& getID() {return this->textureID;}
        const std::string& getSourceAssetRef() const { return sourceAssetRef; }
        void setSourceAssetRef(const std::string& assetRef) { sourceAssetRef = assetRef; }

        std::shared_ptr<Graphics::Image::Image> getImageData() const {return cpuImage;}

        static std::shared_ptr<Texture> Load(PAsset asset, GLenum imageHint = GL_TEXTURE_2D, bool flipVertically = true);
        static std::shared_ptr<Graphics::Image::Image> LoadImage(PAsset asset, bool flipVertically = true);
        static std::shared_ptr<Texture> CreateEmpty(
            int width,
            int height,
            TextureFilterMode filterMode = TextureFilterMode::NEAREST,
            TextureWrapMode wrapMode = TextureWrapMode::CLAMP_EDGE
        );
        static std::shared_ptr<Texture> CreateRenderTarget(
            int width,
            int height,
            GLenum internalFormat = GL_RGBA16F,
            GLenum format = GL_RGBA,
            GLenum type = GL_FLOAT,
            TextureFilterMode filterMode = TextureFilterMode::NEAREST,
            TextureWrapMode wrapMode = TextureWrapMode::CLAMP_EDGE
        );
        static std::shared_ptr<Texture> CreateDepthTarget(
            int width,
            int height,
            GLenum internalFormat = GL_DEPTH_COMPONENT32F,
            GLenum type = GL_FLOAT,
            TextureFilterMode filterMode = TextureFilterMode::NEAREST,
            TextureWrapMode wrapMode = TextureWrapMode::CLAMP_EDGE
        );
        static std::shared_ptr<Texture> CreateFromAlphaBuffer(
            int width,
            int height,
            const unsigned char* alphaData,
            TextureFilterMode filterMode = TextureFilterMode::LINEAR,
            TextureWrapMode wrapMode = TextureWrapMode::CLAMP_EDGE
        );
        static std::shared_ptr<Texture> CreateFromExisting(GLuint id, int width, int height, bool owns = false);
        static void FlushPendingDeletes();

        static void Unload(std::shared_ptr<Texture>& tex){
            if(tex){
                tex->dispose();
                tex.reset();
            }
        }

        static void FlipVerticallyOnLoad(int i);
};

typedef std::shared_ptr<Texture> PTexture; 

#endif // TEXTURE_H
