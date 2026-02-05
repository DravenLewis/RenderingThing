#ifndef TEXTURE_H
#define TEXTURE_H

#include <glad/glad.h>
#include <string>
#include <memory>
#include <vector>

#include "Asset.h"

namespace Graphics { // Hollow Structure of the Graphics Class.
    namespace Image{
        class Image;
        class BufferedImage;
    }
}

class Texture{
    private:
        GLuint textureID;
        std::shared_ptr<Graphics::Image::Image> cpuImage;
        int width, height;
        bool ownsTexture = true;
    public:

        Texture() : textureID(0), width(0), height(0){};
        ~Texture();
        Texture(std::shared_ptr<Graphics::Image::Image> image);
        Texture(const Texture&) = delete; // dont allow copying Texture tex = val.getTexture(); is invald Texture* tex = val.getTexturePtr() is okay tho.
        Texture(int width, int height) : textureID(0), width(width), height(height) {};

        void bind(unsigned int slot = 0) const;
        void unbind() const;
        void dispose();

        inline int getWidth() {return this->width;}
        inline int getHeight() {return this->height;}
        GLuint& getID() {return this->textureID;}

        std::shared_ptr<Graphics::Image::Image> getImageData() const {return cpuImage;}

        static std::shared_ptr<Texture> Load(PAsset asset);
        static std::shared_ptr<Texture> CreateEmpty(int width, int height);
        static std::shared_ptr<Texture> CreateFromAlphaBuffer(int width, int height, const unsigned char* alphaData);
        static std::shared_ptr<Texture> CreateFromExisting(GLuint id, int width, int height, bool owns = false);

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
