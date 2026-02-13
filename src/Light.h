#ifndef LIGHT_H
#define LIGHT_H

#include "Math.h"
#include "Color.h"
#include <vector>
#include <memory>

enum class LightType {
    POINT,
    DIRECTIONAL,
    SPOT
};

enum class ShadowType {
    Hard,
    Standard,
    Smooth
};

#define MAX_LIGHTS 16

struct Light {
    LightType type;
    Math3D::Vec3 position;
    Math3D::Vec3 direction;  // For directional and spot lights
    Math3D::Vec4 color;      // RGB + Alpha for intensity
    float intensity;
    float range;             // Maximum distance for point/spot lights
    float falloff;           // 1.0 = linear, 2.0 = quadratic, 3.0 = cubic, etc
    float spotAngle;         // For spot lights (in degrees)
    float shadowRange;       // Shadow far plane for point/spot (<=0 uses range)
    bool castsShadows;
    ShadowType shadowType;
    float shadowBias;
    float shadowNormalBias;
    float shadowStrength;
    
    Light()
        : type(LightType::POINT),
          position(Math3D::Vec3(0, 0, 0)),
          direction(Math3D::Vec3(0, -1, 0)),
          color(Color::WHITE),
          intensity(1.0f),
          range(20.0f),
          falloff(2.0f),
          spotAngle(45.0f),
          shadowRange(200.0f),
          castsShadows(false),
          shadowType(ShadowType::Smooth),
          shadowBias(0.001f),
          shadowNormalBias(0.002f),
          shadowStrength(1.0f)
    {}
    
    static Light CreatePointLight(
        Math3D::Vec3 position,
        Math3D::Vec4 color = Color::WHITE,
        float intensity = 1.0f,
        float range = 10.0f,
        float falloff = 1.0f
    ) {
        Light light;
        light.type = LightType::POINT;
        light.position = position;
        light.color = color;
        light.intensity = intensity;
        light.range = range;
        light.falloff = Math3D::Clamp(falloff, 0.1f, 3.0f);
        light.shadowRange = 200.0f;
        light.castsShadows = true;
        return light;
    }
    
    static Light CreateDirectionalLight(
        Math3D::Vec3 direction,
        Math3D::Vec4 color = Color::WHITE,
        float intensity = 1.0f
    ) {
        Light light;
        light.type = LightType::DIRECTIONAL;
        light.direction = Math3D::Vec3(glm::normalize(glm::vec3(direction.x, direction.y, direction.z)));
        light.color = color;
        light.intensity = intensity;
        light.range = 20.0f;
        light.shadowRange = 200.0f;
        light.castsShadows = true;
        return light;
    }
    
    static Light CreateSpotLight(
        Math3D::Vec3 position,
        Math3D::Vec3 direction,
        Math3D::Vec4 color = Color::WHITE,
        float intensity = 1.0f,
        float range = 10.0f,
        float falloff = 1.0f,
        float spotAngle = 45.0f
    ) {
        Light light;
        light.type = LightType::SPOT;
        light.position = position;
        light.direction = Math3D::Vec3(glm::normalize(glm::vec3(direction.x, direction.y, direction.z)));
        light.color = color;
        light.intensity = intensity;
        light.range = range;
        light.falloff = Math3D::Clamp(falloff, 0.1f, 3.0f);
        light.spotAngle = spotAngle;
        light.shadowRange = 200.0f;
        light.castsShadows = true;
        return light;
    }
};

class LightManager {
private:
    std::vector<Light> lights;
    
public:
    LightManager() : lights() {}
    
    void addLight(const Light& light) {
        if (lights.size() < MAX_LIGHTS) {
            lights.push_back(light);
        }
    }
    
    void removeLight(size_t index) {
        if (index < lights.size()) {
            lights.erase(lights.begin() + index);
        }
    }
    
    void clearLights() {
        lights.clear();
    }
    
    size_t getLightCount() const {
        return lights.size();
    }
    
    Light& getLight(size_t index) {
        return lights[index];
    }
    
    const Light& getLight(size_t index) const {
        return lights[index];
    }
    
    const std::vector<Light>& getAllLights() const {
        return lights;
    }
    
    static LightManager GlobalLightManager;
};

#endif // LIGHT_H
