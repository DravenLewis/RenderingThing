#include "Rendering/Textures/Texture.h"

#include "Assets/Core/AssetDescriptorUtils.h"
#include "Foundation/Logging/Logbot.h"

#include <iostream>
#include <cstring>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

#include "Rendering/Core/Graphics.h"

// ==========================================================================
// ====================     STB IMPLEMENTATION     ==========================
// ==========================================================================
#define STB_IMAGE_IMPLEMENTATION
#include "STB/stb_image.h"
// ==========================================================================

Logbot textureLogger = Logbot::CreateInstance("Texture");

namespace {
    std::mutex g_pendingTextureDeleteMutex;
    std::vector<GLuint> g_pendingTextureDeletes;
}

Texture::Texture(std::shared_ptr<Graphics::Image::Image> imagePtr, GLenum imageHint) :
    textureID(0), width(0), height(0), cpuImage(imagePtr){

    if(!cpuImage){
        textureLogger.Log(LOG_ERRO,"Failed to create texture from image Ptr, Ptr == null.");
        throw std::runtime_error("Failed to create texture, Ptr == null");
    }

     
    width = cpuImage->width;
    height = cpuImage->height;

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    const bool preferNearest = (width <= 64 && height <= 64);
    if(preferNearest){
        // Preserve crisp texels for small pixel-art style textures.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }else{
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(
        imageHint, 
        0, 
        GL_RGBA8, 
        width, height, 
        0, 
        GL_RGBA, 
        GL_UNSIGNED_BYTE, 
        cpuImage->pixelData.data()
    );
    glGenerateMipmap(GL_TEXTURE_2D);

    unbind();
}

Texture::~Texture(){
    dispose();
}

void Texture::bind(unsigned int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, textureID);
}

void Texture::unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::dispose(){
    if(textureID != 0 && ownsTexture){
        const GLuint idToDelete = textureID;
        textureID = 0;

        if(SDL_GL_GetCurrentContext() != nullptr){
            glDeleteTextures(1, &idToDelete);
        }else{
            std::lock_guard<std::mutex> lock(g_pendingTextureDeleteMutex);
            g_pendingTextureDeletes.push_back(idToDelete);
            // Expected during shutdown or cross-thread destruction; keep this quiet to avoid log spam.
        }
    }
}

void Texture::FlipVerticallyOnLoad(int i){
    stbi_set_flip_vertically_on_load(i);
}

std::shared_ptr<Texture> Texture::Load(PAsset asset, GLenum imageHint, bool flipVertically){
    auto cpuImg = LoadImage(asset, flipVertically);
    if(!cpuImg){
        return nullptr;
    }
    auto texture = std::make_shared<Texture>(cpuImg, imageHint);
    if(texture && asset && asset->getFileHandle()){
        texture->setSourceAssetRef(
            AssetDescriptorUtils::AbsolutePathToAssetRef(
                std::filesystem::path(asset->getFileHandle()->getPath())
            )
        );
    }
    return texture;
}

std::shared_ptr<Graphics::Image::Image> Texture::LoadImage(PAsset asset, bool flipVertically){
    if(!asset){
        textureLogger.Log(LOG_ERRO,"Asset was invalid (nullptr)");
        return nullptr;
    }

    if(!asset->loaded()){
        textureLogger.Log(LOG_ERRO,"Asset was not loaded. Ensure you call Asset::load() on your asset object or use AssetMagager::getOrLoad() to autoload resource.");
        return nullptr;
    }

    BinaryBuffer fileBuffer = asset->asRaw();
    if(fileBuffer.empty()){
        textureLogger.Log(LOG_ERRO,"File was Empty (Reached EOF before data was found.)");
        return nullptr;
    }

    int stb_w, stb_h, stb_channels;

    stbi_set_flip_vertically_on_load_thread(flipVertically ? 1 : 0);
    unsigned char * stb_integer_data = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(fileBuffer.data()),
        static_cast<int>(fileBuffer.size()),
        &stb_w,
        &stb_h,
        &stb_channels,
        4
    );
    stbi_set_flip_vertically_on_load_thread(0);

    if(!stb_integer_data){
        textureLogger.Log(LOG_ERRO,"STB Faild to decode texture data for Texture: %s",asset->getFileHandle()->getFileName().c_str());
        return nullptr;
    }

    auto cpuImg = std::make_shared<Graphics::Image::Image>(stb_w, stb_h);
    std::memcpy(cpuImg->pixelData.data(), stb_integer_data, stb_w * stb_h * 4);
    stbi_image_free(stb_integer_data);

    return cpuImg;
}

std::shared_ptr<Texture> Texture::CreateEmpty(int width, int height){
    auto tex = std::make_shared<Texture>();
    tex->width = width;
    tex->height = height;

    glGenTextures(1, &tex->getID());
    glBindTexture(GL_TEXTURE_2D, tex->getID());

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    // Render targets should stay nearest to avoid unintended blur in post/deferred passes.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

std::shared_ptr<Texture> Texture::CreateFromAlphaBuffer(int width, int height, const unsigned char* alphaData){
    auto tex = std::make_shared<Texture>(width,height);

    // Convert 1-channel Alpha to 4-channel RGBA
    // This allows us to use standard shaders without modification.
    // The font is white (255, 255, 255) with the alpha from the buffer.
    std::vector<unsigned char> rgbaData(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        unsigned char alpha = alphaData[i];
        rgbaData[i * 4 + 0] = 255;   // R
        rgbaData[i * 4 + 1] = 255;   // G
        rgbaData[i * 4 + 2] = 255;   // B
        rgbaData[i * 4 + 3] = alpha; // A
    }

    glGenTextures(1, &tex->getID());
    glBindTexture(GL_TEXTURE_2D, tex->getID());

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // Added.

    // Upload as Standard RGBA
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData.data());

    // Linear filtering usually looks better for text than Nearest
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

std::shared_ptr<Texture> Texture::CreateFromExisting(GLuint id, int width, int height, bool owns){
    auto tex = std::make_shared<Texture>();
    tex->textureID = id;
    tex->width = width;
    tex->height = height;
    tex->ownsTexture = owns;
    return tex;
}

void Texture::FlushPendingDeletes(){
    if(SDL_GL_GetCurrentContext() == nullptr){
        return;
    }

    std::vector<GLuint> toDelete;
    {
        std::lock_guard<std::mutex> lock(g_pendingTextureDeleteMutex);
        if(g_pendingTextureDeletes.empty()){
            return;
        }
        toDelete.swap(g_pendingTextureDeletes);
    }

    glDeleteTextures(static_cast<GLsizei>(toDelete.size()), toDelete.data());
}
