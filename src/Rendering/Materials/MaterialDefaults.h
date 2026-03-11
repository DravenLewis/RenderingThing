/**
 * @file src/Rendering/Materials/MaterialDefaults.h
 * @brief Declarations for MaterialDefaults.
 */

#ifndef MATERIAL_DEFAULTS_H
#define MATERIAL_DEFAULTS_H

#include <string>
#include <map>
#include <memory>

#include "Foundation/Logging/Logbot.h"
#include "Rendering/Materials/Material.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Assets/Core/Asset.h"
#include "Foundation/Math/Color.h"
#include "Rendering/Textures/Texture.h"
#include "Rendering/Lighting/Light.h"
#include "Rendering/Lighting/LightUtils.h"


#include "Foundation/Util/ValueContainer.h"

class Screen;

namespace MaterialDefaults{
    
    /// @brief Represents the ColorMaterial type.
    class ColorMaterial : public Material{
        public:
            ValueContainer<Math3D::Vec4> Color;
            /**
             * @brief Constructs a new ColorMaterial instance.
             * @param program Value for program.
             */
            ColorMaterial(std::shared_ptr<ShaderProgram> program);
            /**
             * @brief Creates a new object.
             * @param color Color value.
             * @return Pointer to the resulting object.
             */
            static std::shared_ptr<ColorMaterial> Create(Math3D::Vec4 color = Color::MAGENTA);
    };

    /// @brief Represents the ImageMaterial type.
    class ImageMaterial : public Material{
        public:

            ValueContainer<Math3D::Vec4> Color;
            ValueContainer<std::shared_ptr<Texture>> Tex;
            ValueContainer<Math3D::Vec2> UV;

            /**
             * @brief Constructs a new ImageMaterial instance.
             * @param program Value for program.
             * @param texture Value for texture.
             */
            ImageMaterial(std::shared_ptr<ShaderProgram> program, std::shared_ptr<Texture> texture);
            /**
             * @brief Creates a new object.
             * @param tex Value for tex.
             * @param color Color value.
             * @param uv Value for uv.
             * @return Pointer to the resulting object.
             */
            static std::shared_ptr<ImageMaterial> Create(PTexture tex, Math3D::Vec4 color = Color::WHITE, Math3D::Vec2 uv = Math3D::Vec2(0,0));
    };

    /// @brief Represents the LitColorMaterial type.
    class LitColorMaterial : public Material{
        public:

            ValueContainer<Math3D::Vec4> Color;
            ValueContainer<Math3D::Vec3> ViewPos;

            /**
             * @brief Constructs a new LitColorMaterial instance.
             * @param program Value for program.
             */
            LitColorMaterial(std::shared_ptr<ShaderProgram> program);
            /**
             * @brief Binds this resource.
             */
            void bind() override;
            /**
             * @brief Creates a new object.
             * @param color Color value.
             * @return Pointer to the resulting object.
             */
            static std::shared_ptr<LitColorMaterial> Create(Math3D::Vec4 color = Color::WHITE);
    };

    /// @brief Represents the LitImageMaterial type.
    class LitImageMaterial : public Material{
        public:

            ValueContainer<Math3D::Vec4> Color;
            ValueContainer<std::shared_ptr<Texture>> Tex;
            ValueContainer<Math3D::Vec3> ViewPos;

            /**
             * @brief Constructs a new LitImageMaterial instance.
             * @param program Value for program.
             * @param texture Value for texture.
             */
            LitImageMaterial(std::shared_ptr<ShaderProgram> program, std::shared_ptr<Texture> texture);
            /**
             * @brief Binds this resource.
             */
            void bind() override;
            /**
             * @brief Creates a new object.
             * @param tex Value for tex.
             * @param color Color value.
             * @return Pointer to the resulting object.
             */
            static std::shared_ptr<LitImageMaterial> Create(PTexture tex, Math3D::Vec4 color = Color::WHITE);
    };

    /// @brief Represents the FlatColorMaterial type.
    class FlatColorMaterial : public Material {
    public:
        ValueContainer<Math3D::Vec4> Color;
        ValueContainer<Math3D::Vec3> ViewPos;

        /**
         * @brief Constructs a new FlatColorMaterial instance.
         * @param program Value for program.
         */
        FlatColorMaterial(std::shared_ptr<ShaderProgram> program);
        /**
         * @brief Binds this resource.
         */
        void bind() override;
        /**
         * @brief Creates a new object.
         * @param color Color value.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<FlatColorMaterial> Create(Math3D::Vec4 color = Color::WHITE);
    };    

    /// @brief Represents the FlatImageMaterial type.
    class FlatImageMaterial : public Material {
        public:

            ValueContainer<Math3D::Vec4> Color;
            ValueContainer<std::shared_ptr<Texture>> Tex;
            ValueContainer<Math3D::Vec3> ViewPos;

            /**
             * @brief Constructs a new FlatImageMaterial instance.
             * @param program Value for program.
             * @param texture Value for texture.
             */
            FlatImageMaterial(std::shared_ptr<ShaderProgram> program, std::shared_ptr<Texture> texture);
            /**
             * @brief Binds this resource.
             */
            void bind() override;
            /**
             * @brief Creates a new object.
             * @param tex Value for tex.
             * @param color Color value.
             * @return Pointer to the resulting object.
             */
            static std::shared_ptr<FlatImageMaterial> Create(std::shared_ptr<Texture> tex, Math3D::Vec4 color = Color::WHITE);
    };
};

#endif //MATERIAL_DEFAULTS_H
