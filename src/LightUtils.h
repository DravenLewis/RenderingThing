#ifndef LIGHT_UTILS_H
#define LIGHT_UTILS_H

#include "Light.h"
#include "ShaderProgram.h"
#include <memory>

class LightUniformUploader {
public:
    static void UploadLights(std::shared_ptr<ShaderProgram> program, const std::vector<Light>& lights) {
        int lightCount = static_cast<int>(lights.size());
        if (lightCount > MAX_LIGHTS) lightCount = MAX_LIGHTS;
        
        // Upload light count
        GLint countLoc = glGetUniformLocation(program->getID(), "u_lightCount");
        if (countLoc != -1) {
            glUniform1i(countLoc, lightCount);
        }
        
        // Upload each light
        for (int i = 0; i < lightCount; ++i) {
            const Light& light = lights[i];
            std::string uniformBase = "u_lights[" + std::to_string(i) + "]";
            
            // Type
            GLint typeLoc = glGetUniformLocation(program->getID(), (uniformBase + ".type").c_str());
            if (typeLoc != -1) {
                glUniform1i(typeLoc, static_cast<int>(light.type));
            }
            
            // Position
            GLint posLoc = glGetUniformLocation(program->getID(), (uniformBase + ".position").c_str());
            if (posLoc != -1) {
                glUniform3f(posLoc, light.position.x, light.position.y, light.position.z);
            }
            
            // Direction
            GLint dirLoc = glGetUniformLocation(program->getID(), (uniformBase + ".direction").c_str());
            if (dirLoc != -1) {
                glUniform3f(dirLoc, light.direction.x, light.direction.y, light.direction.z);
            }
            
            // Color
            GLint colorLoc = glGetUniformLocation(program->getID(), (uniformBase + ".color").c_str());
            if (colorLoc != -1) {
                glUniform4f(colorLoc, light.color.x, light.color.y, light.color.z, light.color.w);
            }
            
            // Intensity
            GLint intensityLoc = glGetUniformLocation(program->getID(), (uniformBase + ".intensity").c_str());
            if (intensityLoc != -1) {
                glUniform1f(intensityLoc, light.intensity);
            }
            
            // Range
            GLint rangeLoc = glGetUniformLocation(program->getID(), (uniformBase + ".range").c_str());
            if (rangeLoc != -1) {
                glUniform1f(rangeLoc, light.range);
            }
            
            // Falloff
            GLint falloffLoc = glGetUniformLocation(program->getID(), (uniformBase + ".falloff").c_str());
            if (falloffLoc != -1) {
                glUniform1f(falloffLoc, light.falloff);
            }
            
            // Spot Angle
            GLint angleLoc = glGetUniformLocation(program->getID(), (uniformBase + ".spotAngle").c_str());
            if (angleLoc != -1) {
                glUniform1f(angleLoc, light.spotAngle);
            }
        }
    }
};

#endif // LIGHT_UTILS_H
