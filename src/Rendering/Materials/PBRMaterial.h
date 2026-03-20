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

/// @brief Enumerates PBR BSDF shading models.
enum class PBRBsdfModel {
    Standard = 0,
    Glass = 1,
    Water = 2
};

/// @brief Represents the PBRMaterial type.
class PBRMaterial : public Material {
    public:
        ValueContainer<Math3D::Vec4> BaseColor;
        ValueContainer<std::shared_ptr<Texture>> BaseColorTex;
        ValueContainer<int> BsdfModel;
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
        ValueContainer<float> Transmission;
        ValueContainer<float> Ior;
        ValueContainer<float> Thickness;
        ValueContainer<Math3D::Vec3> AttenuationColor;
        ValueContainer<float> AttenuationDistance;
        ValueContainer<float> ScatteringStrength;
        ValueContainer<int> EnableWaveDisplacement;
        ValueContainer<float> WaveAmplitude;
        ValueContainer<float> WaveFrequency;
        ValueContainer<float> WaveSpeed;
        ValueContainer<float> WaveChoppiness;
        ValueContainer<float> WaveSecondaryScale;
        ValueContainer<Math3D::Vec2> WaveDirection;
        ValueContainer<float> WaveTextureInfluence;
        ValueContainer<Math3D::Vec2> WaveTextureSpeed;
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
        /**
         * @brief Creates a glass-tuned PBR material.
         * @param baseColor Color value.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<PBRMaterial> CreateGlass(Math3D::Vec4 baseColor = Math3D::Vec4(0.97f, 0.985f, 1.00f, 0.18f));
        /**
         * @brief Creates a water-tuned PBR material.
         * @param baseColor Color value.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<PBRMaterial> CreateWater(Math3D::Vec4 baseColor = Math3D::Vec4(0.10f, 0.34f, 0.52f, 0.22f));
};

#endif // PBR_MATERIAL_H
