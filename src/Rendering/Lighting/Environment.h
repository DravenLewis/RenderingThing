/**
 * @file src/Rendering/Lighting/Environment.h
 * @brief Declarations for Environment.
 */

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <memory>
#include <vector>

#include "Rendering/Lighting/Light.h"

class SkyBox;
typedef std::shared_ptr<SkyBox> PSkyBox;

/// @brief Represents the Environment type.
class Environment {
    private:
        LightManager lightManager;
        PSkyBox skybox;
        bool lightingEnabled = false;

    public:
        /**
         * @brief Constructs a new Environment instance.
         */
        Environment() = default;

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

        const std::vector<Light>& getLightsForUpload() const {
            static const std::vector<Light> EMPTY;
            return lightingEnabled ? lightManager.getAllLights() : EMPTY;
        }
};

typedef std::shared_ptr<Environment> PEnvironment;

#endif // ENVIRONMENT_H
