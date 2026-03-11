/**
 * @file src/Rendering/Materials/PBRMaterial.h
 * @brief Declarations for PBRMaterial.
 */

#ifndef PBR_MATERIAL_H
#define PBR_MATERIAL_H

#include <memory>

#include "Rendering/Materials/Material.h"
#include "Rendering/Textures/Texture.h"
#include "Rendering/Textures/CubeMap.h"
#include "Foundation/Util/ValueContainer.h"
#include "Foundation/Math/Color.h"

/// @brief Represents the PBRMaterial type.
class PBRMaterial : public Material {
    public:
        ValueContainer<Math3D::Vec4> BaseColor;
        ValueContainer<std::shared_ptr<Texture>> BaseColorTex;
        ValueContainer<float> Metallic;
        ValueContainer<float> Roughness;
        ValueContainer<std::shared_ptr<Texture>> RoughnessTex;
        ValueContainer<std::shared_ptr<Texture>> MetallicRoughnessTex;
        ValueContainer<std::shared_ptr<Texture>> NormalTex;
        ValueContainer<float> NormalScale;
        ValueContainer<std::shared_ptr<Texture>> HeightTex;
        ValueContainer<float> HeightScale;
        ValueContainer<std::shared_ptr<Texture>> EmissiveTex;
        ValueContainer<Math3D::Vec3> EmissiveColor;
        ValueContainer<float> EmissiveStrength;
        ValueContainer<std::shared_ptr<Texture>> OcclusionTex;
        ValueContainer<float> OcclusionStrength;
        ValueContainer<std::shared_ptr<CubeMap>> EnvMap;
        ValueContainer<float> EnvStrength;
        ValueContainer<int> UseEnvMap;
        ValueContainer<Math3D::Vec2> UVScale;
        ValueContainer<Math3D::Vec2> UVOffset;
        ValueContainer<float> AlphaCutoff;
        ValueContainer<int> UseAlphaClip;
        ValueContainer<Math3D::Vec3> ViewPos;

        /**
         * @brief Constructs a new PBRMaterial instance.
         * @param program Value for program.
         */
        PBRMaterial(std::shared_ptr<ShaderProgram> program);
        /**
         * @brief Binds this resource.
         */
        void bind() override;

        /**
         * @brief Creates a new object.
         * @param baseColor Color value.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<PBRMaterial> Create(Math3D::Vec4 baseColor = Color::WHITE);
};

#endif // PBR_MATERIAL_H
