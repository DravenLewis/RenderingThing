#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <memory>
#include <vector>

#include "Light.h"

class SkyBox;
typedef std::shared_ptr<SkyBox> PSkyBox;

class Environment {
    private:
        LightManager lightManager;
        PSkyBox skybox;
        bool lightingEnabled = false;

    public:
        Environment() = default;

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
