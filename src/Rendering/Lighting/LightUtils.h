#ifndef LIGHT_UTILS_H
#define LIGHT_UTILS_H

#include "Rendering/Lighting/Light.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include <memory>

class LightUniformUploader {
public:
    static void UploadLights(std::shared_ptr<ShaderProgram> program, const std::vector<Light>& lights);
};

#endif // LIGHT_UTILS_H
