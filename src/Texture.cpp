#include "Texture.h"

#include "Logbot.h"

#include <iostream>
#include <cstring>

#include "Graphics.h"

// ==========================================================================
// ====================     STB IMPLEMENTATION     ==========================
// ==========================================================================
#define STB_IMAGE_IMPLEMENTATION
#include "STB/stb_image.h"
// ==========================================================================

Logbot textureLogger = Logbot::CreateInstance("Texture");

Texture::Texture(std::shared_ptr<Graphics::Image::Image> imagePtr) :
    textureID(0), width(0), height(0), cpuImage(imagePtr){

    if(!cpuImage){
        textureLogger.Log(LOG_ERRO,"Failed to create texture from image Ptr, Ptr == null.");
        throw std::runtime_error("Failed to create texture, Ptr == null");
    }

     
    width = cpuImage->width;
    height = cpuImage->height;

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(
        GL_TEXTURE_2D, 
        0, 
        GL_RGBA8, 
        width, height, 
        0, 
        GL_RGBA, 
        GL_UNSIGNED_BYTE, 
        cpuImage->pixelData.data()
    );

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
    if(textureID != 0){
        textureLogger.Log(LOG_INFO, "Deleting Texture ID: %u", textureID);
        glDeleteTextures(1, &textureID);
        textureID = 0;
    }
}

void Texture::FlipVerticallyOnLoad(int i){
    stbi_set_flip_vertically_on_load(i);
}

std::shared_ptr<Texture> Texture::Load(PAsset asset){
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

    unsigned char * stb_integer_data = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(fileBuffer.data()),
        static_cast<int>(fileBuffer.size()),
        &stb_w,
        &stb_h,
        &stb_channels,
        4
    );

    if(!stb_integer_data){
        textureLogger.Log(LOG_ERRO,"STB Faild to decode texture data for Texture: %s",asset->getFileHandle()->getFileName().c_str());
        return nullptr;
    }

    auto cpuImg = std::make_shared<Graphics::Image::Image>(stb_w, stb_h);
    std::memcpy(cpuImg->pixelData.data(), stb_integer_data, stb_w * stb_h * 4);
    stbi_image_free(stb_integer_data);

    return std::make_shared<Texture>(cpuImg);
}

std::shared_ptr<Texture> Texture::CreateEmpty(int width, int height){
    auto tex = std::make_shared<Texture>();
    tex->width = width;
    tex->height = height;

    glGenTextures(1, &tex->getID());
    glBindTexture(GL_TEXTURE_2D, tex->getID());

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

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