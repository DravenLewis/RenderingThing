#include "CubeMap.h"

#include "Logbot.h"
#include <array>
#include <vector>

#include "Texture.h"
#include "Graphics.h"

Logbot cubemapLogger = Logbot::CreateInstance("CubeMap");

CubeMap::~CubeMap(){
    dispose();
}

CubeMap::CubeMap(CubeMap&& other) noexcept{
    textureID = other.textureID;
    size = other.size;
    ownsTexture = other.ownsTexture;
    other.textureID = 0;
    other.size = 0;
}

CubeMap& CubeMap::operator=(CubeMap&& other) noexcept{
    if(this != &other){
        dispose();
        textureID = other.textureID;
        size = other.size;
        ownsTexture = other.ownsTexture;
        other.textureID = 0;
        other.size = 0;
    }
    return *this;
}

void CubeMap::bind(unsigned int slot) const{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
}

void CubeMap::unbind() const{
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void CubeMap::dispose(){
    if(textureID != 0 && ownsTexture){
        cubemapLogger.Log(LOG_INFO, "Deleting CubeMap ID: %u", textureID);
        glDeleteTextures(1, &textureID);
        textureID = 0;
        size = 0;
    }
}

static bool loadFace(GLuint cubemapId, GLenum face, const PAsset& asset, int& outSize){
    if(!asset){
        cubemapLogger.Log(LOG_ERRO, "Cubemap face asset was null.");
        return false;
    }

    if(!asset->loaded()){
        cubemapLogger.Log(LOG_ERRO, "Cubemap asset not loaded: %s", asset->getFileHandle()->getFileName().c_str());
        return false;
    }

    auto cpuImg = Texture::LoadImage(asset);
    if(!cpuImg){
        cubemapLogger.Log(LOG_ERRO, "Failed to load image for: %s", asset->getFileHandle()->getFileName().c_str());
        return false;
    }

    if(cpuImg->height != cpuImg->width){
        cubemapLogger.Log(LOG_ERRO, "Cubemap face must be square: %s", asset->getFileHandle()->getFileName().c_str());
        return false;
    }

    if(outSize != 0 && cpuImg->width != outSize){
        cubemapLogger.Log(LOG_ERRO, "Cubemap faces must be the same size.");
        return false;
    }

    outSize = cpuImg->width;

    // Debug: log first pixel to confirm non-zero data
    if(!cpuImg->pixelData.empty()){
        uint32_t firstPixel = cpuImg->pixelData[0];
        cubemapLogger.Log(LOG_INFO, "Cubemap face %s first pixel: 0x%08X", asset->getFileHandle()->getFileName().c_str(), firstPixel);
    }

    // Ensure correct cubemap is bound before upload
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapId);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Clear previous GL errors
    while(glGetError() != GL_NO_ERROR) {}

    glTexImage2D(
        face,
        0,
        GL_RGBA8,
        cpuImg->width,
        cpuImg->height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        cpuImg->pixelData.data()
    );

    GLenum err = glGetError();
    if(err != GL_NO_ERROR){
        cubemapLogger.Log(LOG_ERRO, "Cubemap glTexImage2D error 0x%04X for face %s", err, asset->getFileHandle()->getFileName().c_str());
        return false;
    }

    return true;
}

std::shared_ptr<CubeMap> CubeMap::Load(
    PAsset posX,
    PAsset negX,
    PAsset posY,
    PAsset negY,
    PAsset posZ,
    PAsset negZ
){
    auto cubemap = std::make_shared<CubeMap>();

    glGenTextures(1, &cubemap->textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap->textureID);

    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxSize);
    cubemapLogger.Log(LOG_INFO, "Max cubemap size: %d", static_cast<int>(maxSize));

    int faceSize = 0;
    std::array<std::pair<GLenum, PAsset>, 6> faces = {{
        {GL_TEXTURE_CUBE_MAP_POSITIVE_X, posX},
        {GL_TEXTURE_CUBE_MAP_NEGATIVE_X, negX},
        {GL_TEXTURE_CUBE_MAP_POSITIVE_Y, posY},
        {GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, negY},
        {GL_TEXTURE_CUBE_MAP_POSITIVE_Z, posZ},
        {GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, negZ}
    }};

    for(const auto& face : faces){
        if(!loadFace(cubemap->textureID, face.first, face.second, faceSize)){
            cubemap->dispose();
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            return nullptr;
        }
    }

    cubemap->size = faceSize;

    // Debug: verify texture levels populated
    GLint faceWidth = 0;
    GLint faceHeight = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &faceWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_HEIGHT, &faceHeight);
    cubemapLogger.Log(LOG_INFO, "Cubemap face size reported by GL: %dx%d", faceWidth, faceHeight);

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    return cubemap;
}
