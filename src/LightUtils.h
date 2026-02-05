#ifndef LIGHT_UTILS_H
#define LIGHT_UTILS_H

#include "Light.h"
#include "ShaderProgram.h"
#include <memory>

class LightUniformUploader {
public:
    static void UploadLights(std::shared_ptr<ShaderProgram> program, const std::vector<Light>& lights);
};

#endif // LIGHT_UTILS_H
