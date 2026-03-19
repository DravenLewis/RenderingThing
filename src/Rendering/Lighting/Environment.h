/**
 * @file src/Rendering/Lighting/Environment.h
 * @brief Declarations for Environment.
 */

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <algorithm>
#include <memory>
#include <vector>

#include "Foundation/Math/Color.h"
#include "Rendering/Lighting/Light.h"

class SkyBox;
typedef std::shared_ptr<SkyBox> PSkyBox;

/// @brief Holds editable/environment-asset-backed world lighting settings.
struct EnvironmentSettings {
    bool fogEnabled = false;
    Color fogColor = Color(0.72f, 0.79f, 0.88f, 1.0f);
    float fogStart = 24.0f;
    float fogStop = 80.0f;
    float fogEnd = 180.0f;

    Color ambientColor = Color(0.03f, 0.03f, 0.03f, 1.0f);
    float ambientIntensity = 1.0f;

    bool useProceduralSky = false;
    Math3D::Vec3 sunDirection = Math3D::Vec3(0.0f, -1.0f, 0.0f);
    float rayleighStrength = 1.0f;
    float mieStrength = 0.35f;
    float mieAnisotropy = 0.76f;
};

/// @brief Represents the Environment type.
class Environment {
    private:
        LightManager lightManager;
        PSkyBox skybox;
        bool lightingEnabled = false;
        EnvironmentSettings settings;

        void sanitizeSettings(){
            settings.fogStart = Math3D::Max(0.0f, settings.fogStart);
            settings.fogStop = Math3D::Max(settings.fogStart, settings.fogStop);
            settings.fogEnd = Math3D::Max(settings.fogStop, settings.fogEnd);
            settings.ambientIntensity = Math3D::Clamp(settings.ambientIntensity, 0.0f, 32.0f);
            settings.rayleighStrength = Math3D::Max(0.0f, settings.rayleighStrength);
            settings.mieStrength = Math3D::Max(0.0f, settings.mieStrength);
            settings.mieAnisotropy = Math3D::Clamp(settings.mieAnisotropy, 0.0f, 0.99f);
            if(settings.sunDirection.length() <= Math3D::EPSILON){
                settings.sunDirection = Math3D::Vec3(0.0f, -1.0f, 0.0f);
            }else{
                settings.sunDirection = settings.sunDirection.normalize();
            }
        }

    public:
        /**
         * @brief Constructs a new Environment instance.
         */
        Environment(){
            sanitizeSettings();
        }

        /**
         * @brief Returns the light manager.
         * @return Reference to the resulting value.
         */
        LightManager& getLightManager() { return lightManager; }
        const LightManager& getLightManager() const { return lightManager; }

        void setSkyBox(PSkyBox box) { skybox = box; }
        PSkyBox getSkyBox() const { return skybox; }

        void setLightingEnabled(bool enabled) { lightingEnabled = enabled; }
        bool isLightingEnabled() const { return lightingEnabled; }

        EnvironmentSettings& getSettings() { return settings; }
        const EnvironmentSettings& getSettings() const { return settings; }
        void setSettings(const EnvironmentSettings& value){
            settings = value;
            sanitizeSettings();
        }
        void sanitize(){
            sanitizeSettings();
        }

        const std::vector<Light>& getLightsForUpload() const {
            static const std::vector<Light> EMPTY;
            return lightingEnabled ? lightManager.getAllLights() : EMPTY;
        }
};

typedef std::shared_ptr<Environment> PEnvironment;

#endif // ENVIRONMENT_H
