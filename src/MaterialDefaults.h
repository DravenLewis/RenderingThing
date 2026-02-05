#ifndef MATERIAL_DEFAULTS_H
#define MATERIAL_DEFAULTS_H

#include <string>
#include <map>
#include <memory>

#include "Logbot.h"
#include "Material.h"
#include "ShaderProgram.h"
#include "Asset.h"
#include "Color.h"
#include "Texture.h"
#include "Light.h"
#include "LightUtils.h"


#include "ValueContainer.h"

class Screen;

namespace MaterialDefaults{
    
    class ColorMaterial : public Material{
        public:
            ValueContainer<Math3D::Vec4> Color;
            ColorMaterial(std::shared_ptr<ShaderProgram> program);
            static std::shared_ptr<ColorMaterial> Create(Math3D::Vec4 color = Color::MAGENTA);
    };

    class ImageMaterial : public Material{
        public:

            ValueContainer<Math3D::Vec4> Color;
            ValueContainer<std::shared_ptr<Texture>> Tex;
            ValueContainer<Math3D::Vec2> UV;

            ImageMaterial(std::shared_ptr<ShaderProgram> program, std::shared_ptr<Texture> texture);
            static std::shared_ptr<ImageMaterial> Create(PTexture tex, Math3D::Vec4 color = Color::WHITE, Math3D::Vec2 uv = Math3D::Vec2(0,0));
    };

    class LitColorMaterial : public Material{
        public:

            ValueContainer<Math3D::Vec4> Color;
            ValueContainer<Math3D::Vec3> ViewPos;

            LitColorMaterial(std::shared_ptr<ShaderProgram> program);
            void bind() override;
            static std::shared_ptr<LitColorMaterial> Create(Math3D::Vec4 color = Color::WHITE);
    };

    class LitImageMaterial : public Material{
        public:

            ValueContainer<Math3D::Vec4> Color;
            ValueContainer<std::shared_ptr<Texture>> Tex;
            ValueContainer<Math3D::Vec3> ViewPos;

            LitImageMaterial(std::shared_ptr<ShaderProgram> program, std::shared_ptr<Texture> texture);
            void bind() override;
            static std::shared_ptr<LitImageMaterial> Create(PTexture tex, Math3D::Vec4 color = Color::WHITE);
    };

    class FlatColorMaterial : public Material {
    public:
        ValueContainer<Math3D::Vec4> Color;
        ValueContainer<Math3D::Vec3> ViewPos;

        FlatColorMaterial(std::shared_ptr<ShaderProgram> program);
        void bind() override;
        static std::shared_ptr<FlatColorMaterial> Create(Math3D::Vec4 color = Color::WHITE);
    };    

    class FlatImageMaterial : public Material {
        public:

            ValueContainer<Math3D::Vec4> Color;
            ValueContainer<std::shared_ptr<Texture>> Tex;
            ValueContainer<Math3D::Vec3> ViewPos;

            FlatImageMaterial(std::shared_ptr<ShaderProgram> program, std::shared_ptr<Texture> texture);
            void bind() override;
            static std::shared_ptr<FlatImageMaterial> Create(std::shared_ptr<Texture> tex, Math3D::Vec4 color = Color::WHITE);
    };
};

#endif //MATERIAL_DEFAULTS_H