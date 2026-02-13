#include "ShadowMap.h"

#include <array>
#include "Logbot.h"

static void configureDepthTexture2D(GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void configureDepthTextureCube(GLuint tex) {
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

ShadowMap2D::ShadowMap2D(int size) : size(size) {
    init();
}

ShadowMap2D::~ShadowMap2D() {
    if(depthTex != 0) glDeleteTextures(1, &depthTex);
    if(fbo != 0) glDeleteFramebuffers(1, &fbo);
}

ShadowMap2D::ShadowMap2D(ShadowMap2D&& other) noexcept {
    fbo = other.fbo;
    depthTex = other.depthTex;
    size = other.size;
    other.fbo = 0;
    other.depthTex = 0;
    other.size = 0;
}

ShadowMap2D& ShadowMap2D::operator=(ShadowMap2D&& other) noexcept {
    if(this != &other){
        if(depthTex != 0) glDeleteTextures(1, &depthTex);
        if(fbo != 0) glDeleteFramebuffers(1, &fbo);
        fbo = other.fbo;
        depthTex = other.depthTex;
        size = other.size;
        other.fbo = 0;
        other.depthTex = 0;
        other.size = 0;
    }
    return *this;
}

void ShadowMap2D::init() {
    if(size <= 0) return;

    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &depthTex);

    glBindTexture(GL_TEXTURE_2D, depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    configureDepthTexture2D(depthTex);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if(status != GL_FRAMEBUFFER_COMPLETE){
        LogBot.Log(LOG_ERRO, "[ShadowMap2D] Incomplete FBO status=0x%X size=%d", status, size);
        if(depthTex != 0) glDeleteTextures(1, &depthTex);
        if(fbo != 0) glDeleteFramebuffers(1, &fbo);
        depthTex = 0;
        fbo = 0;
        size = 0;
    }
}

void ShadowMap2D::resize(int newSize) {
    if(newSize == size) return;
    size = newSize;
    if(depthTex != 0) glDeleteTextures(1, &depthTex);
    if(fbo != 0) glDeleteFramebuffers(1, &fbo);
    depthTex = 0;
    fbo = 0;
    init();
}

void ShadowMap2D::bind() const {
    if(fbo == 0){
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
}

void ShadowMap2D::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMap2D::clear() const {
    glClear(GL_DEPTH_BUFFER_BIT);
}

ShadowMapCube::ShadowMapCube(int size) : size(size) {
    init();
}

ShadowMapCube::~ShadowMapCube() {
    if(depthCube != 0) glDeleteTextures(1, &depthCube);
    if(fbo != 0) glDeleteFramebuffers(1, &fbo);
}

ShadowMapCube::ShadowMapCube(ShadowMapCube&& other) noexcept {
    fbo = other.fbo;
    depthCube = other.depthCube;
    size = other.size;
    other.fbo = 0;
    other.depthCube = 0;
    other.size = 0;
}

ShadowMapCube& ShadowMapCube::operator=(ShadowMapCube&& other) noexcept {
    if(this != &other){
        if(depthCube != 0) glDeleteTextures(1, &depthCube);
        if(fbo != 0) glDeleteFramebuffers(1, &fbo);
        fbo = other.fbo;
        depthCube = other.depthCube;
        size = other.size;
        other.fbo = 0;
        other.depthCube = 0;
        other.size = 0;
    }
    return *this;
}

void ShadowMapCube::init() {
    if(size <= 0) return;

    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &depthCube);

    glBindTexture(GL_TEXTURE_CUBE_MAP, depthCube);
    for(int face = 0; face < 6; ++face){
        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            0,
            GL_DEPTH_COMPONENT32F,
            size,
            size,
            0,
            GL_DEPTH_COMPONENT,
            GL_FLOAT,
            nullptr
        );
    }
    configureDepthTextureCube(depthCube);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthCube, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if(status != GL_FRAMEBUFFER_COMPLETE){
        LogBot.Log(LOG_ERRO, "[ShadowMapCube] Incomplete FBO status=0x%X size=%d", status, size);
        if(depthCube != 0) glDeleteTextures(1, &depthCube);
        if(fbo != 0) glDeleteFramebuffers(1, &fbo);
        depthCube = 0;
        fbo = 0;
        size = 0;
    }
}

void ShadowMapCube::resize(int newSize) {
    if(newSize == size) return;
    size = newSize;
    if(depthCube != 0) glDeleteTextures(1, &depthCube);
    if(fbo != 0) glDeleteFramebuffers(1, &fbo);
    depthCube = 0;
    fbo = 0;
    init();
}

void ShadowMapCube::bindFace(GLenum face) const {
    if(fbo == 0 || depthCube == 0){
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, face, depthCube, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
}

void ShadowMapCube::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMapCube::clear() const {
    glClear(GL_DEPTH_BUFFER_BIT);
}
