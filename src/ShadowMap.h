#ifndef SHADOW_MAP_H
#define SHADOW_MAP_H

#include <glad/glad.h>

class ShadowMap2D {
private:
    GLuint fbo = 0;
    GLuint depthTex = 0;
    int size = 0;

    void init();

public:
    ShadowMap2D() = default;
    explicit ShadowMap2D(int size);
    ~ShadowMap2D();
    ShadowMap2D(const ShadowMap2D&) = delete;
    ShadowMap2D& operator=(const ShadowMap2D&) = delete;
    ShadowMap2D(ShadowMap2D&& other) noexcept;
    ShadowMap2D& operator=(ShadowMap2D&& other) noexcept;

    void resize(int newSize);
    void bind() const;
    void unbind() const;
    void clear() const;

    GLuint getDepthTexture() const { return depthTex; }
    int getSize() const { return size; }
};

class ShadowMapCube {
private:
    GLuint fbo = 0;
    GLuint depthCube = 0;
    int size = 0;

    void init();

public:
    ShadowMapCube() = default;
    explicit ShadowMapCube(int size);
    ~ShadowMapCube();
    ShadowMapCube(const ShadowMapCube&) = delete;
    ShadowMapCube& operator=(const ShadowMapCube&) = delete;
    ShadowMapCube(ShadowMapCube&& other) noexcept;
    ShadowMapCube& operator=(ShadowMapCube&& other) noexcept;

    void resize(int newSize);
    void bindFace(GLenum face) const;
    void unbind() const;
    void clear() const;

    GLuint getDepthTexture() const { return depthCube; }
    int getSize() const { return size; }
};

#endif // SHADOW_MAP_H
