/**
 * @file src/Rendering/Materials/ConstructedMaterial.h
 * @brief Declarations for ConstructedMaterial.
 */

#ifndef CONSTRUCTED_MATERIAL_H
#define CONSTRUCTED_MATERIAL_H

#include "Rendering/Materials/Material.h"

#include <string>
#include <vector>

class Texture;

/// @brief Represents the ConstructedMaterial type.
class ConstructedMaterial : public Material {
    public:
        /// @brief Enumerates values for FieldType.
        enum class FieldType {
            Float = 0,
            Int,
            Bool,
            Vec2,
            Vec3,
            Vec4,
            Texture2D
        };

        /// @brief Holds data for Field.
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

        /**
         * @brief Constructs a new ConstructedMaterial instance.
         * @param shaderProgram Value for shader program.
          * @return Result of this operation.
         */
        explicit ConstructedMaterial(const std::shared_ptr<ShaderProgram>& shaderProgram);

        /**
         * @brief Creates with pbr shader.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<ConstructedMaterial> CreateWithPBRShader();

        /**
         * @brief Sets the source asset ref.
         * @param ref Reference to reference.
         */
        void setSourceAssetRef(const std::string& ref);
        /**
         * @brief Returns the source asset ref.
         * @return Resulting string value.
         */
        const std::string& getSourceAssetRef() const;

        /**
         * @brief Sets the source material name.
         * @param name Name used for name.
         */
        void setSourceMaterialName(const std::string& name);
        /**
         * @brief Returns the source material name.
         * @return Resulting string value.
         */
        const std::string& getSourceMaterialName() const;

        /**
         * @brief Adds a float field definition.
         * @param key Value for key.
         * @param displayName Name used for display name.
         * @param uniformName Name used for uniform name.
         * @param value Value for value.
         * @param mirrorUniformName Name used for mirror uniform name.
         * @return Reference to the resulting value.
         */
        Field& addFloatField(const std::string& key,
                             const std::string& displayName,
                             const std::string& uniformName,
                             float value,
                             const std::string& mirrorUniformName = "");
        /**
         * @brief Adds an integer field definition.
         * @param key Value for key.
         * @param displayName Name used for display name.
         * @param uniformName Name used for uniform name.
         * @param value Value for value.
         * @param mirrorUniformName Name used for mirror uniform name.
         * @return Reference to the resulting value.
         */
        Field& addIntField(const std::string& key,
                           const std::string& displayName,
                           const std::string& uniformName,
                           int value,
                           const std::string& mirrorUniformName = "");
        /**
         * @brief Adds a boolean field definition.
         * @param key Value for key.
         * @param displayName Name used for display name.
         * @param uniformName Name used for uniform name.
         * @param value Value for value.
         * @param mirrorUniformName Name used for mirror uniform name.
         * @return Reference to the resulting value.
         */
        Field& addBoolField(const std::string& key,
                            const std::string& displayName,
                            const std::string& uniformName,
                            bool value,
                            const std::string& mirrorUniformName = "");
        /**
         * @brief Adds a vec2 field definition.
         * @param key Value for key.
         * @param displayName Name used for display name.
         * @param uniformName Name used for uniform name.
         * @param value Value for value.
         * @param mirrorUniformName Name used for mirror uniform name.
         * @return Reference to the resulting value.
         */
        Field& addVec2Field(const std::string& key,
                            const std::string& displayName,
                            const std::string& uniformName,
                            const Math3D::Vec2& value,
                            const std::string& mirrorUniformName = "");
        /**
         * @brief Adds a vec3 field definition.
         * @param key Value for key.
         * @param displayName Name used for display name.
         * @param uniformName Name used for uniform name.
         * @param value Value for value.
         * @param mirrorUniformName Name used for mirror uniform name.
         * @return Reference to the resulting value.
         */
        Field& addVec3Field(const std::string& key,
                            const std::string& displayName,
                            const std::string& uniformName,
                            const Math3D::Vec3& value,
                            const std::string& mirrorUniformName = "");
        /**
         * @brief Adds a vec4 field definition.
         * @param key Value for key.
         * @param displayName Name used for display name.
         * @param uniformName Name used for uniform name.
         * @param value Value for value.
         * @param mirrorUniformName Name used for mirror uniform name.
         * @return Reference to the resulting value.
         */
        Field& addVec4Field(const std::string& key,
                            const std::string& displayName,
                            const std::string& uniformName,
                            const Math3D::Vec4& value,
                            const std::string& mirrorUniformName = "");
        /**
         * @brief Adds a texture field definition.
         * @param key Value for key.
         * @param displayName Name used for display name.
         * @param uniformName Name used for uniform name.
         * @param textureAssetRef Reference to texture asset.
         * @param textureSlot Value for texture slot.
         * @param presenceUniformName Name used for presence uniform name.
         * @return Reference to the resulting value.
         */
        Field& addTexture2DField(const std::string& key,
                                 const std::string& displayName,
                                 const std::string& uniformName,
                                 const std::string& textureAssetRef,
                                 int textureSlot,
                                 const std::string& presenceUniformName = "");

        /**
         * @brief Returns the material field list.
         * @return Reference to the resulting value.
         */
        std::vector<Field>& fields();
        /**
         * @brief Returns the material field list.
         * @return Reference to the resulting value.
         */
        const std::vector<Field>& fields() const;

        /**
         * @brief Clears fields.
         */
        void clearFields();
        /**
         * @brief Marks material fields dirty for reapply.
         */
        void markFieldsDirty();
        /**
         * @brief Applies field.
         * @param index Identifier or index value.
         */
        void applyField(size_t index);
        /**
         * @brief Applies all fields.
         */
        void applyAllFields();

        /**
         * @brief Binds this resource.
         */
        void bind() override;

    private:
        /**
         * @brief Inserts a material field definition.
         * @param field Value for field.
         * @return Reference to the resulting value.
         */
        Field& emplaceField(Field&& field);
        /**
         * @brief Applies field internal.
         * @param field Value for field.
         */
        void applyFieldInternal(Field& field);

        std::vector<Field> dynamicFields;
        bool fieldsDirty = true;
        std::string sourceAssetRef;
        std::string sourceMaterialName;
};

#endif // CONSTRUCTED_MATERIAL_H
