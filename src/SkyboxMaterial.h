#ifndef SKYBOX_MATERIAL_H
#define SKYBOX_MATERIAL_H

#include <memory>

#include "Material.h"
#include "CubeMap.h"
#include "ValueContainer.h"

class SkyboxMaterial : public Material {
    public:
        ValueContainer<std::shared_ptr<CubeMap>> EnvMap;

        SkyboxMaterial(std::shared_ptr<ShaderProgram> program);
        void bind() override;
        static std::shared_ptr<SkyboxMaterial> Create(std::shared_ptr<CubeMap> envMap);
};

#endif // SKYBOX_MATERIAL_H
