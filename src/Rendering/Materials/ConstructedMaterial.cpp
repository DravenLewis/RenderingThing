#include "Rendering/Materials/ConstructedMaterial.h"

#include "Assets/Core/Asset.h"
#include "Rendering/Materials/PBRMaterial.h"
#include "Rendering/Textures/Texture.h"

#include <utility>

ConstructedMaterial::ConstructedMaterial(const std::shared_ptr<ShaderProgram>& shaderProgram)
    : Material(shaderProgram){
}

std::shared_ptr<ConstructedMaterial> ConstructedMaterial::CreateWithPBRShader(){
    auto pbr = PBRMaterial::Create(Color::WHITE);
    auto shader = pbr ? pbr->getShader() : nullptr;
    return std::make_shared<ConstructedMaterial>(shader);
}

void ConstructedMaterial::setSourceAssetRef(const std::string& ref){
    sourceAssetRef = ref;
}

const std::string& ConstructedMaterial::getSourceAssetRef() const{
    return sourceAssetRef;
}

void ConstructedMaterial::setSourceMaterialName(const std::string& name){
    sourceMaterialName = name;
}

const std::string& ConstructedMaterial::getSourceMaterialName() const{
    return sourceMaterialName;
}

ConstructedMaterial::Field& ConstructedMaterial::emplaceField(Field&& field){
    dynamicFields.push_back(std::move(field));
    fieldsDirty = true;
    return dynamicFields.back();
}

ConstructedMaterial::Field& ConstructedMaterial::addFloatField(const std::string& key,
                                                               const std::string& displayName,
                                                               const std::string& uniformName,
                                                               float value,
                                                               const std::string& mirrorUniformName){
    Field field;
    field.key = key;
    field.displayName = displayName;
    field.uniformName = uniformName;
    field.mirrorUniformName = mirrorUniformName;
    field.type = FieldType::Float;
    field.floatValue = value;
    return emplaceField(std::move(field));
}

ConstructedMaterial::Field& ConstructedMaterial::addIntField(const std::string& key,
                                                             const std::string& displayName,
                                                             const std::string& uniformName,
                                                             int value,
                                                             const std::string& mirrorUniformName){
    Field field;
    field.key = key;
    field.displayName = displayName;
    field.uniformName = uniformName;
    field.mirrorUniformName = mirrorUniformName;
    field.type = FieldType::Int;
    field.intValue = value;
    return emplaceField(std::move(field));
}

ConstructedMaterial::Field& ConstructedMaterial::addBoolField(const std::string& key,
                                                              const std::string& displayName,
                                                              const std::string& uniformName,
                                                              bool value,
                                                              const std::string& mirrorUniformName){
    Field field;
    field.key = key;
    field.displayName = displayName;
    field.uniformName = uniformName;
    field.mirrorUniformName = mirrorUniformName;
    field.type = FieldType::Bool;
    field.boolValue = value;
    return emplaceField(std::move(field));
}

ConstructedMaterial::Field& ConstructedMaterial::addVec2Field(const std::string& key,
                                                              const std::string& displayName,
                                                              const std::string& uniformName,
                                                              const Math3D::Vec2& value,
                                                              const std::string& mirrorUniformName){
    Field field;
    field.key = key;
    field.displayName = displayName;
    field.uniformName = uniformName;
    field.mirrorUniformName = mirrorUniformName;
    field.type = FieldType::Vec2;
    field.vec2Value = value;
    return emplaceField(std::move(field));
}

ConstructedMaterial::Field& ConstructedMaterial::addVec3Field(const std::string& key,
                                                              const std::string& displayName,
                                                              const std::string& uniformName,
                                                              const Math3D::Vec3& value,
                                                              const std::string& mirrorUniformName){
    Field field;
    field.key = key;
    field.displayName = displayName;
    field.uniformName = uniformName;
    field.mirrorUniformName = mirrorUniformName;
    field.type = FieldType::Vec3;
    field.vec3Value = value;
    return emplaceField(std::move(field));
}

ConstructedMaterial::Field& ConstructedMaterial::addVec4Field(const std::string& key,
                                                              const std::string& displayName,
                                                              const std::string& uniformName,
                                                              const Math3D::Vec4& value,
                                                              const std::string& mirrorUniformName){
    Field field;
    field.key = key;
    field.displayName = displayName;
    field.uniformName = uniformName;
    field.mirrorUniformName = mirrorUniformName;
    field.type = FieldType::Vec4;
    field.vec4Value = value;
    return emplaceField(std::move(field));
}

ConstructedMaterial::Field& ConstructedMaterial::addTexture2DField(const std::string& key,
                                                                   const std::string& displayName,
                                                                   const std::string& uniformName,
                                                                   const std::string& textureAssetRef,
                                                                   int textureSlot,
                                                                   const std::string& presenceUniformName){
    Field field;
    field.key = key;
    field.displayName = displayName;
    field.uniformName = uniformName;
    field.presenceUniformName = presenceUniformName;
    field.type = FieldType::Texture2D;
    field.textureAssetRef = textureAssetRef;
    field.textureSlot = textureSlot;
    return emplaceField(std::move(field));
}

std::vector<ConstructedMaterial::Field>& ConstructedMaterial::fields(){
    return dynamicFields;
}

const std::vector<ConstructedMaterial::Field>& ConstructedMaterial::fields() const{
    return dynamicFields;
}

void ConstructedMaterial::clearFields(){
    dynamicFields.clear();
    fieldsDirty = true;
}

void ConstructedMaterial::markFieldsDirty(){
    fieldsDirty = true;
}

void ConstructedMaterial::applyFieldInternal(Field& field){
    auto setIntMirror = [&](int value){
        if(!field.uniformName.empty()){
            set<int>(field.uniformName, value);
        }
        if(!field.mirrorUniformName.empty()){
            set<int>(field.mirrorUniformName, value);
        }
    };

    switch(field.type){
        case FieldType::Float:{
            if(!field.uniformName.empty()){
                set<float>(field.uniformName, field.floatValue);
            }
            if(!field.mirrorUniformName.empty()){
                set<float>(field.mirrorUniformName, field.floatValue);
            }
            break;
        }
        case FieldType::Int:{
            setIntMirror(field.intValue);
            break;
        }
        case FieldType::Bool:{
            setIntMirror(field.boolValue ? 1 : 0);
            break;
        }
        case FieldType::Vec2:{
            if(!field.uniformName.empty()){
                set<Math3D::Vec2>(field.uniformName, field.vec2Value);
            }
            if(!field.mirrorUniformName.empty()){
                set<Math3D::Vec2>(field.mirrorUniformName, field.vec2Value);
            }
            break;
        }
        case FieldType::Vec3:{
            if(!field.uniformName.empty()){
                set<Math3D::Vec3>(field.uniformName, field.vec3Value);
            }
            if(!field.mirrorUniformName.empty()){
                set<Math3D::Vec3>(field.mirrorUniformName, field.vec3Value);
            }
            break;
        }
        case FieldType::Vec4:{
            if(!field.uniformName.empty()){
                set<Math3D::Vec4>(field.uniformName, field.vec4Value);
            }
            if(!field.mirrorUniformName.empty()){
                set<Math3D::Vec4>(field.mirrorUniformName, field.vec4Value);
            }
            break;
        }
        case FieldType::Texture2D:{
            if(field.loadedTextureRef != field.textureAssetRef){
                field.texturePtr.reset();
                if(!field.textureAssetRef.empty()){
                    auto texAsset = AssetManager::Instance.getOrLoad(field.textureAssetRef);
                    if(texAsset){
                        field.texturePtr = Texture::Load(texAsset);
                    }
                }
                field.loadedTextureRef = field.textureAssetRef;
            }

            if(!field.uniformName.empty()){
                set<GLUniformUpload::TextureSlot>(
                    field.uniformName,
                    GLUniformUpload::TextureSlot(field.texturePtr, field.textureSlot)
                );
            }
            if(!field.presenceUniformName.empty()){
                set<int>(field.presenceUniformName, field.texturePtr ? 1 : 0);
            }
            break;
        }
        default:
            break;
    }
}

void ConstructedMaterial::applyField(size_t index){
    if(index >= dynamicFields.size()){
        return;
    }
    applyFieldInternal(dynamicFields[index]);
}

void ConstructedMaterial::applyAllFields(){
    for(auto& field : dynamicFields){
        applyFieldInternal(field);
    }
}

void ConstructedMaterial::bind(){
    if(fieldsDirty){
        applyAllFields();
        fieldsDirty = false;
    }
    Material::bind();
}
