#ifndef PBR_MATERIAL_H
#define PBR_MATERIAL_H

#include <memory>

#include "Material.h"
#include "Texture.h"
#include "CubeMap.h"
#include "ValueContainer.h"
#include "Color.h"

class PBRMaterial : public Material {
    public:
        ValueContainer<Math3D::Vec4> BaseColor;
        ValueContainer<std::shared_ptr<Texture>> BaseColorTex;
        ValueContainer<float> Metallic;
        ValueContainer<float> Roughness;
        ValueContainer<std::shared_ptr<Texture>> MetallicRoughnessTex;
        ValueContainer<std::shared_ptr<Texture>> NormalTex;
        ValueContainer<float> NormalScale;
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

        PBRMaterial(std::shared_ptr<ShaderProgram> program);
        void bind() override;

        static std::shared_ptr<PBRMaterial> Create(Math3D::Vec4 baseColor = Color::WHITE);
};

#endif // PBR_MATERIAL_H
