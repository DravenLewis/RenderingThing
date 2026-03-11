/**
 * @file src/Rendering/Lighting/LightUtils.h
 * @brief Declarations for LightUtils.
 */

#ifndef LIGHT_UTILS_H
#define LIGHT_UTILS_H

#include "Rendering/Lighting/Light.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include <memory>

/// @brief Represents the LightUniformUploader type.
class LightUniformUploader {
public:
    /**
     * @brief Uploads light data to shader uniforms.
     * @param program Value for program.
     * @param lights Value for lights.
     */
    static void UploadLights(std::shared_ptr<ShaderProgram> program, const std::vector<Light>& lights);
};

#endif // LIGHT_UTILS_H
