/**
 * @file src/Rendering/Lighting/ShadowMap.h
 * @brief Declarations for ShadowMap.
 */

#ifndef SHADOW_MAP_H
#define SHADOW_MAP_H

#include <glad/glad.h>

/// @brief Represents the ShadowMap2D type.
class ShadowMap2D {
private:
    GLuint fbo = 0;
    GLuint depthTex = 0;
    int size = 0;

    /**
     * @brief Initializes this object.
     */
    void init();

public:
    /**
     * @brief Constructs a new ShadowMap2D instance.
     */
    ShadowMap2D() = default;
    /**
     * @brief Constructs a new ShadowMap2D instance.
     * @param size Number of elements or bytes.
      * @return Result of this operation.
     */
    explicit ShadowMap2D(int size);
    /**
     * @brief Destroys this ShadowMap2D instance.
     */
    ~ShadowMap2D();
    /**
     * @brief Constructs a new ShadowMap2D instance.
     */
    ShadowMap2D(const ShadowMap2D&) = delete;
    /**
     * @brief Assigns from another instance.
     */
    ShadowMap2D& operator=(const ShadowMap2D&) = delete;
    /**
     * @brief Constructs a new ShadowMap2D instance.
     * @param other Value for other.
     */
    ShadowMap2D(ShadowMap2D&& other) noexcept;
    /**
     * @brief Assigns from another instance.
     * @param other Value for other.
     */
    ShadowMap2D& operator=(ShadowMap2D&& other) noexcept;

    /**
     * @brief Resizes internal resources.
     * @param newSize Number of elements or bytes.
     */
    void resize(int newSize);
    /**
     * @brief Binds this resource.
     */
    void bind() const;
    /**
     * @brief Unbinds this resource.
     */
    void unbind() const;
    /**
     * @brief Clears the current state.
     */
    void clear() const;

    /**
     * @brief Returns the depth texture.
     * @return Result of this operation.
     */
    GLuint getDepthTexture() const { return depthTex; }
    int getSize() const { return size; }
};

/// @brief Represents the ShadowMapCube type.
class ShadowMapCube {
private:
    GLuint fbo = 0;
    GLuint depthCube = 0;
    int size = 0;

    /**
     * @brief Initializes this object.
     */
    void init();

public:
    /**
     * @brief Constructs a new ShadowMapCube instance.
     */
    ShadowMapCube() = default;
    /**
     * @brief Constructs a new ShadowMapCube instance.
     * @param size Number of elements or bytes.
      * @return Result of this operation.
     */
    explicit ShadowMapCube(int size);
    /**
     * @brief Destroys this ShadowMapCube instance.
     */
    ~ShadowMapCube();
    /**
     * @brief Constructs a new ShadowMapCube instance.
     */
    ShadowMapCube(const ShadowMapCube&) = delete;
    /**
     * @brief Assigns from another instance.
     */
    ShadowMapCube& operator=(const ShadowMapCube&) = delete;
    /**
     * @brief Constructs a new ShadowMapCube instance.
     * @param other Value for other.
     */
    ShadowMapCube(ShadowMapCube&& other) noexcept;
    /**
     * @brief Assigns from another instance.
     * @param other Value for other.
     */
    ShadowMapCube& operator=(ShadowMapCube&& other) noexcept;

    /**
     * @brief Resizes internal resources.
     * @param newSize Number of elements or bytes.
     */
    void resize(int newSize);
    /**
     * @brief Binds face.
     * @param face Value for face.
     */
    void bindFace(GLenum face) const;
    /**
     * @brief Unbinds this resource.
     */
    void unbind() const;
    /**
     * @brief Clears the current state.
     */
    void clear() const;

    /**
     * @brief Returns the depth texture.
     * @return Result of this operation.
     */
    GLuint getDepthTexture() const { return depthCube; }
    int getSize() const { return size; }
};

#endif // SHADOW_MAP_H
