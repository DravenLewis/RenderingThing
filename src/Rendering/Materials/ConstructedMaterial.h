#ifndef CONSTRUCTED_MATERIAL_H
#define CONSTRUCTED_MATERIAL_H

#include "Rendering/Materials/Material.h"

#include <string>
#include <vector>

class Texture;

class ConstructedMaterial : public Material {
    public:
        enum class FieldType {
            Float = 0,
            Int,
            Bool,
            Vec2,
            Vec3,
            Vec4,
            Texture2D
        };

        struct Field {
            std::string key;
            std::string displayName;
            std::string uniformName;
            std::string mirrorUniformName;
            std::string presenceUniformName;
            FieldType type = FieldType::Float;

            float floatValue = 0.0f;
            int intValue = 0;
            bool boolValue = false;
            Math3D::Vec2 vec2Value = Math3D::Vec2(0.0f, 0.0f);
            Math3D::Vec3 vec3Value = Math3D::Vec3(0.0f, 0.0f, 0.0f);
            Math3D::Vec4 vec4Value = Math3D::Vec4(0.0f, 0.0f, 0.0f, 1.0f);

            std::string textureAssetRef;
            std::shared_ptr<Texture> texturePtr;
            std::string loadedTextureRef;
            int textureSlot = 0;
        };

        explicit ConstructedMaterial(const std::shared_ptr<ShaderProgram>& shaderProgram);

        static std::shared_ptr<ConstructedMaterial> CreateWithPBRShader();

        void setSourceAssetRef(const std::string& ref);
        const std::string& getSourceAssetRef() const;

        void setSourceMaterialName(const std::string& name);
        const std::string& getSourceMaterialName() const;

        Field& addFloatField(const std::string& key,
                             const std::string& displayName,
                             const std::string& uniformName,
                             float value,
                             const std::string& mirrorUniformName = "");
        Field& addIntField(const std::string& key,
                           const std::string& displayName,
                           const std::string& uniformName,
                           int value,
                           const std::string& mirrorUniformName = "");
        Field& addBoolField(const std::string& key,
                            const std::string& displayName,
                            const std::string& uniformName,
                            bool value,
                            const std::string& mirrorUniformName = "");
        Field& addVec2Field(const std::string& key,
                            const std::string& displayName,
                            const std::string& uniformName,
                            const Math3D::Vec2& value,
                            const std::string& mirrorUniformName = "");
        Field& addVec3Field(const std::string& key,
                            const std::string& displayName,
                            const std::string& uniformName,
                            const Math3D::Vec3& value,
                            const std::string& mirrorUniformName = "");
        Field& addVec4Field(const std::string& key,
                            const std::string& displayName,
                            const std::string& uniformName,
                            const Math3D::Vec4& value,
                            const std::string& mirrorUniformName = "");
        Field& addTexture2DField(const std::string& key,
                                 const std::string& displayName,
                                 const std::string& uniformName,
                                 const std::string& textureAssetRef,
                                 int textureSlot,
                                 const std::string& presenceUniformName = "");

        std::vector<Field>& fields();
        const std::vector<Field>& fields() const;

        void clearFields();
        void markFieldsDirty();
        void applyField(size_t index);
        void applyAllFields();

        void bind() override;

    private:
        Field& emplaceField(Field&& field);
        void applyFieldInternal(Field& field);

        std::vector<Field> dynamicFields;
        bool fieldsDirty = true;
        std::string sourceAssetRef;
        std::string sourceMaterialName;
};

#endif // CONSTRUCTED_MATERIAL_H
