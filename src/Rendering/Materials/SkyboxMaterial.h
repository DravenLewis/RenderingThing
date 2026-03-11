/**
 * @file src/Rendering/Materials/SkyboxMaterial.h
 * @brief Declarations for SkyboxMaterial.
 */

#ifndef SKYBOX_MATERIAL_H
#define SKYBOX_MATERIAL_H

#include <memory>

#include "Rendering/Materials/Material.h"
#include "Rendering/Textures/CubeMap.h"
#include "Foundation/Util/ValueContainer.h"

/// @brief Represents the SkyboxMaterial type.
class SkyboxMaterial : public Material {
    public:
        ValueContainer<std::shared_ptr<CubeMap>> EnvMap;

        /**
         * @brief Constructs a new SkyboxMaterial instance.
         * @param program Value for program.
         */
        SkyboxMaterial(std::shared_ptr<ShaderProgram> program);
        /**
         * @brief Binds this resource.
         */
        void bind() override;
        /**
         * @brief Creates a new object.
         * @param envMap Value for env map.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<SkyboxMaterial> Create(std::shared_ptr<CubeMap> envMap);
};

#endif // SKYBOX_MATERIAL_H
