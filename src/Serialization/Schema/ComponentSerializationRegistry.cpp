#include "Serialization/Schema/ComponentSerializationRegistry.h"

#include "ECS/Core/ECSComponents.h"
#include "Assets/Core/Asset.h"
#include "Assets/Descriptors/MaterialAsset.h"
#include "Assets/Descriptors/ModelAsset.h"
#include "Assets/Importers/OBJLoader.h"
#include "Rendering/Materials/MaterialDefaults.h"
#include "Rendering/Materials/PBRMaterial.h"
#include "Serialization/Json/JsonUtils.h"
#include "Foundation/Util/StringUtils.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

void setComponentSerializationError(std::string* outError, const std::string& message){
    if(outError){
        *outError = message;
    }
}

template<typename TEnum>
TEnum enumFromIntClamped(int value, int minValue, int maxValue, TEnum fallback){
    if(value < minValue || value > maxValue){
        return fallback;
    }
    return static_cast<TEnum>(value);
}

bool readPayloadFromJsonString(const std::string& json, JsonUtils::Document& outDoc, yyjson_val*& outPayload, std::string* outError){
    if(!JsonUtils::LoadDocumentFromText(json, outDoc, outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to parse component payload JSON: " + *outError;
        }
        return false;
    }

    outPayload = outDoc.root();
    if(!outPayload || !yyjson_is_obj(outPayload)){
        setComponentSerializationError(outError, "Component payload JSON root must be an object.");
        return false;
    }
    return true;
}

bool addStringArrayField(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const std::vector<std::string>& values, std::string* outError){
    yyjson_mut_val* arr = yyjson_mut_obj_add_arr(doc, obj, key);
    if(!arr){
        setComponentSerializationError(outError, std::string("Failed to create array field '") + key + "'.");
        return false;
    }
    for(const std::string& value : values){
        if(!yyjson_mut_arr_add_strcpy(doc, arr, value.c_str())){
            setComponentSerializationError(outError, std::string("Failed to append value to array field '") + key + "'.");
            return false;
        }
    }
    return true;
}

bool readStringArrayField(yyjson_val* obj, const char* key, std::vector<std::string>& outValues, std::string* outError){
    outValues.clear();
    yyjson_val* arr = JsonUtils::ObjGetArray(obj, key);
    if(!arr){
        return true;
    }

    const size_t count = yyjson_arr_size(arr);
    outValues.reserve(count);
    for(size_t i = 0; i < count; ++i){
        yyjson_val* item = yyjson_arr_get(arr, i);
        if(!item || !yyjson_is_str(item)){
            setComponentSerializationError(outError, std::string("Field '") + key + "' must be a string array.");
            return false;
        }
        const char* value = yyjson_get_str(item);
        outValues.push_back(value ? value : "");
    }
    return true;
}

bool writeTransformFields(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* keyPrefix, const Math3D::Transform& transform){
    (void)keyPrefix;
    Math3D::Vec3 rotationEuler = transform.rotation.ToEuler();
    return JsonUtils::MutObjAddVec3(doc, obj, "position", transform.position) &&
           JsonUtils::MutObjAddVec3(doc, obj, "rotationEuler", rotationEuler) &&
           JsonUtils::MutObjAddVec3(doc, obj, "scale", transform.scale);
}

void readTransformFields(yyjson_val* obj, Math3D::Transform& transform){
    Math3D::Vec3 position = transform.position;
    Math3D::Vec3 rotationEuler = transform.rotation.ToEuler();
    Math3D::Vec3 scale = transform.scale;
    JsonUtils::TryGetVec3(obj, "position", position);
    JsonUtils::TryGetVec3(obj, "rotationEuler", rotationEuler);
    JsonUtils::TryGetVec3(obj, "scale", scale);
    transform.position = position;
    transform.setRotation(rotationEuler);
    transform.scale = scale;
}

bool addFloatArrayField(yyjson_mut_doc* doc,
                        yyjson_mut_val* obj,
                        const char* key,
                        const std::vector<float>& values,
                        std::string* outError){
    yyjson_mut_val* arr = yyjson_mut_obj_add_arr(doc, obj, key);
    if(!arr){
        setComponentSerializationError(outError, std::string("Failed to create float array field '") + key + "'.");
        return false;
    }
    for(float value : values){
        if(!yyjson_mut_arr_add_real(doc, arr, static_cast<double>(value))){
            setComponentSerializationError(outError, std::string("Failed to append float to array field '") + key + "'.");
            return false;
        }
    }
    return true;
}

bool addUIntArrayField(yyjson_mut_doc* doc,
                       yyjson_mut_val* obj,
                       const char* key,
                       const std::vector<std::uint32_t>& values,
                       std::string* outError){
    yyjson_mut_val* arr = yyjson_mut_obj_add_arr(doc, obj, key);
    if(!arr){
        setComponentSerializationError(outError, std::string("Failed to create uint array field '") + key + "'.");
        return false;
    }
    for(std::uint32_t value : values){
        if(!yyjson_mut_arr_add_uint(doc, arr, static_cast<std::uint64_t>(value))){
            setComponentSerializationError(outError, std::string("Failed to append uint to array field '") + key + "'.");
            return false;
        }
    }
    return true;
}

bool readFloatArrayField(yyjson_val* obj,
                         const char* key,
                         std::vector<float>& outValues,
                         std::string* outError){
    outValues.clear();
    yyjson_val* arr = JsonUtils::ObjGetArray(obj, key);
    if(!arr){
        return true;
    }

    const size_t count = yyjson_arr_size(arr);
    outValues.reserve(count);
    for(size_t i = 0; i < count; ++i){
        yyjson_val* item = yyjson_arr_get(arr, i);
        if(!item || !yyjson_is_num(item)){
            setComponentSerializationError(outError, std::string("Field '") + key + "' must be a number array.");
            return false;
        }
        outValues.push_back(static_cast<float>(yyjson_get_num(item)));
    }
    return true;
}

bool readUIntArrayField(yyjson_val* obj,
                        const char* key,
                        std::vector<std::uint32_t>& outValues,
                        std::string* outError){
    outValues.clear();
    yyjson_val* arr = JsonUtils::ObjGetArray(obj, key);
    if(!arr){
        return true;
    }

    const size_t count = yyjson_arr_size(arr);
    outValues.reserve(count);
    for(size_t i = 0; i < count; ++i){
        yyjson_val* item = yyjson_arr_get(arr, i);
        if(!item || !yyjson_is_num(item)){
            setComponentSerializationError(outError, std::string("Field '") + key + "' must be a number array.");
            return false;
        }

        const double numberValue = yyjson_get_num(item);
        if(numberValue < 0.0 || numberValue > static_cast<double>(UINT32_MAX)){
            setComponentSerializationError(outError, std::string("Field '") + key + "' contains an out-of-range index value.");
            return false;
        }
        outValues.push_back(static_cast<std::uint32_t>(numberValue));
    }
    return true;
}

std::string textureSourceRef(const std::shared_ptr<Texture>& texture){
    if(!texture){
        return std::string();
    }
    return texture->getSourceAssetRef();
}

bool captureEmbeddedMaterialData(const std::shared_ptr<Material>& material, MaterialAssetData& outData){
    outData = MaterialAssetData{};
    if(!material){
        return false;
    }

    outData.castsShadows = material->castsShadows();
    outData.receivesShadows = material->receivesShadows();

    if(auto pbr = Material::GetAs<PBRMaterial>(material)){
        outData.type = MaterialAssetType::PBR;
        outData.color = pbr->BaseColor.get();
        outData.metallic = pbr->Metallic.get();
        outData.roughness = pbr->Roughness.get();
        outData.normalScale = pbr->NormalScale.get();
        outData.heightScale = pbr->HeightScale.get();
        outData.emissiveColor = pbr->EmissiveColor.get();
        outData.emissiveStrength = pbr->EmissiveStrength.get();
        outData.occlusionStrength = pbr->OcclusionStrength.get();
        outData.envStrength = pbr->EnvStrength.get();
        outData.useEnvMap = pbr->UseEnvMap.get();
        outData.uvScale = pbr->UVScale.get();
        outData.uvOffset = pbr->UVOffset.get();
        outData.alphaCutoff = pbr->AlphaCutoff.get();
        outData.useAlphaClip = pbr->UseAlphaClip.get();
        outData.baseColorTexRef = textureSourceRef(pbr->BaseColorTex.get());
        outData.roughnessTexRef = textureSourceRef(pbr->RoughnessTex.get());
        outData.metallicRoughnessTexRef = textureSourceRef(pbr->MetallicRoughnessTex.get());
        outData.normalTexRef = textureSourceRef(pbr->NormalTex.get());
        outData.heightTexRef = textureSourceRef(pbr->HeightTex.get());
        outData.emissiveTexRef = textureSourceRef(pbr->EmissiveTex.get());
        outData.occlusionTexRef = textureSourceRef(pbr->OcclusionTex.get());
        return true;
    }
    if(auto litImage = Material::GetAs<MaterialDefaults::LitImageMaterial>(material)){
        outData.type = MaterialAssetType::LitImage;
        outData.color = litImage->Color.get();
        outData.textureRef = textureSourceRef(litImage->Tex.get());
        return true;
    }
    if(auto flatImage = Material::GetAs<MaterialDefaults::FlatImageMaterial>(material)){
        outData.type = MaterialAssetType::FlatImage;
        outData.color = flatImage->Color.get();
        outData.textureRef = textureSourceRef(flatImage->Tex.get());
        return true;
    }
    if(auto image = Material::GetAs<MaterialDefaults::ImageMaterial>(material)){
        outData.type = MaterialAssetType::Image;
        outData.color = image->Color.get();
        outData.uv = image->UV.get();
        outData.textureRef = textureSourceRef(image->Tex.get());
        return true;
    }
    if(auto litColor = Material::GetAs<MaterialDefaults::LitColorMaterial>(material)){
        outData.type = MaterialAssetType::LitColor;
        outData.color = litColor->Color.get();
        return true;
    }
    if(auto flatColor = Material::GetAs<MaterialDefaults::FlatColorMaterial>(material)){
        outData.type = MaterialAssetType::FlatColor;
        outData.color = flatColor->Color.get();
        return true;
    }
    if(auto color = Material::GetAs<MaterialDefaults::ColorMaterial>(material)){
        outData.type = MaterialAssetType::Color;
        outData.color = color->Color.get();
        return true;
    }

    return false;
}

bool writeEmbeddedMaterialDataField(const MaterialAssetData& data,
                                    yyjson_mut_doc* doc,
                                    yyjson_mut_val* partObj,
                                    std::string* outError){
    yyjson_mut_val* materialObj = yyjson_mut_obj_add_obj(doc, partObj, "embeddedMaterial");
    if(!materialObj){
        setComponentSerializationError(outError, "Failed to create embeddedMaterial object.");
        return false;
    }

    if(!JsonUtils::MutObjAddString(doc, materialObj, "type", MaterialAssetIO::TypeToString(data.type)) ||
       !JsonUtils::MutObjAddString(doc, materialObj, "shaderAssetRef", data.shaderAssetRef) ||
       !JsonUtils::MutObjAddVec4(doc, materialObj, "color", data.color) ||
       !JsonUtils::MutObjAddVec2(doc, materialObj, "uv", data.uv) ||
       !JsonUtils::MutObjAddString(doc, materialObj, "textureRef", data.textureRef) ||
       !JsonUtils::MutObjAddFloat(doc, materialObj, "metallic", data.metallic) ||
       !JsonUtils::MutObjAddFloat(doc, materialObj, "roughness", data.roughness) ||
       !JsonUtils::MutObjAddFloat(doc, materialObj, "normalScale", data.normalScale) ||
       !JsonUtils::MutObjAddFloat(doc, materialObj, "heightScale", data.heightScale) ||
       !JsonUtils::MutObjAddVec3(doc, materialObj, "emissiveColor", data.emissiveColor) ||
       !JsonUtils::MutObjAddFloat(doc, materialObj, "emissiveStrength", data.emissiveStrength) ||
       !JsonUtils::MutObjAddFloat(doc, materialObj, "occlusionStrength", data.occlusionStrength) ||
       !JsonUtils::MutObjAddFloat(doc, materialObj, "envStrength", data.envStrength) ||
       !JsonUtils::MutObjAddInt(doc, materialObj, "useEnvMap", data.useEnvMap) ||
       !JsonUtils::MutObjAddVec2(doc, materialObj, "uvScale", data.uvScale) ||
       !JsonUtils::MutObjAddVec2(doc, materialObj, "uvOffset", data.uvOffset) ||
       !JsonUtils::MutObjAddFloat(doc, materialObj, "alphaCutoff", data.alphaCutoff) ||
       !JsonUtils::MutObjAddInt(doc, materialObj, "useAlphaClip", data.useAlphaClip) ||
       !JsonUtils::MutObjAddString(doc, materialObj, "baseColorTexRef", data.baseColorTexRef) ||
       !JsonUtils::MutObjAddString(doc, materialObj, "roughnessTexRef", data.roughnessTexRef) ||
       !JsonUtils::MutObjAddString(doc, materialObj, "metallicRoughnessTexRef", data.metallicRoughnessTexRef) ||
       !JsonUtils::MutObjAddString(doc, materialObj, "normalTexRef", data.normalTexRef) ||
       !JsonUtils::MutObjAddString(doc, materialObj, "heightTexRef", data.heightTexRef) ||
       !JsonUtils::MutObjAddString(doc, materialObj, "emissiveTexRef", data.emissiveTexRef) ||
       !JsonUtils::MutObjAddString(doc, materialObj, "occlusionTexRef", data.occlusionTexRef) ||
       !JsonUtils::MutObjAddBool(doc, materialObj, "castsShadows", data.castsShadows) ||
       !JsonUtils::MutObjAddBool(doc, materialObj, "receivesShadows", data.receivesShadows)){
        setComponentSerializationError(outError, "Failed to serialize embedded material fields.");
        return false;
    }

    return true;
}

bool tryReadEmbeddedMaterialDataField(yyjson_val* partObj, MaterialAssetData& outData){
    yyjson_val* materialObj = JsonUtils::ObjGetObject(partObj, "embeddedMaterial");
    if(!materialObj){
        return false;
    }

    outData = MaterialAssetData{};

    std::string materialType;
    if(JsonUtils::TryGetString(materialObj, "type", materialType)){
        outData.type = MaterialAssetIO::TypeFromString(materialType);
    }

    JsonUtils::TryGetString(materialObj, "shaderAssetRef", outData.shaderAssetRef);
    JsonUtils::TryGetVec4(materialObj, "color", outData.color);
    JsonUtils::TryGetVec2(materialObj, "uv", outData.uv);
    JsonUtils::TryGetString(materialObj, "textureRef", outData.textureRef);
    JsonUtils::TryGetFloat(materialObj, "metallic", outData.metallic);
    JsonUtils::TryGetFloat(materialObj, "roughness", outData.roughness);
    JsonUtils::TryGetFloat(materialObj, "normalScale", outData.normalScale);
    JsonUtils::TryGetFloat(materialObj, "heightScale", outData.heightScale);
    JsonUtils::TryGetVec3(materialObj, "emissiveColor", outData.emissiveColor);
    JsonUtils::TryGetFloat(materialObj, "emissiveStrength", outData.emissiveStrength);
    JsonUtils::TryGetFloat(materialObj, "occlusionStrength", outData.occlusionStrength);
    JsonUtils::TryGetFloat(materialObj, "envStrength", outData.envStrength);
    JsonUtils::TryGetInt(materialObj, "useEnvMap", outData.useEnvMap);
    JsonUtils::TryGetVec2(materialObj, "uvScale", outData.uvScale);
    JsonUtils::TryGetVec2(materialObj, "uvOffset", outData.uvOffset);
    JsonUtils::TryGetFloat(materialObj, "alphaCutoff", outData.alphaCutoff);
    JsonUtils::TryGetInt(materialObj, "useAlphaClip", outData.useAlphaClip);
    JsonUtils::TryGetString(materialObj, "baseColorTexRef", outData.baseColorTexRef);
    JsonUtils::TryGetString(materialObj, "roughnessTexRef", outData.roughnessTexRef);
    JsonUtils::TryGetString(materialObj, "metallicRoughnessTexRef", outData.metallicRoughnessTexRef);
    JsonUtils::TryGetString(materialObj, "normalTexRef", outData.normalTexRef);
    JsonUtils::TryGetString(materialObj, "heightTexRef", outData.heightTexRef);
    JsonUtils::TryGetString(materialObj, "emissiveTexRef", outData.emissiveTexRef);
    JsonUtils::TryGetString(materialObj, "occlusionTexRef", outData.occlusionTexRef);
    JsonUtils::TryGetBool(materialObj, "castsShadows", outData.castsShadows);
    JsonUtils::TryGetBool(materialObj, "receivesShadows", outData.receivesShadows);
    return true;
}

bool writeEmbeddedModelField(const MeshRendererComponent& component,
                             yyjson_mut_doc* doc,
                             yyjson_mut_val* payload,
                             std::string* outError){
    if(!component.model){
        return true;
    }

    yyjson_mut_val* embeddedModelObj = yyjson_mut_obj_add_obj(doc, payload, "embeddedModel");
    if(!embeddedModelObj){
        setComponentSerializationError(outError, "Failed to create embeddedModel object.");
        return false;
    }

    if(!JsonUtils::MutObjAddBool(doc, embeddedModelObj, "enableBackfaceCulling", component.model->isBackfaceCullingEnabled())){
        setComponentSerializationError(outError, "Failed to write embeddedModel backface culling flag.");
        return false;
    }

    yyjson_mut_val* partsArr = yyjson_mut_obj_add_arr(doc, embeddedModelObj, "parts");
    if(!partsArr){
        setComponentSerializationError(outError, "Failed to create embeddedModel parts array.");
        return false;
    }

    const auto& parts = component.model->getParts();
    for(size_t partIndex = 0; partIndex < parts.size(); ++partIndex){
        const auto& part = parts[partIndex];
        if(!part || !part->mesh){
            continue;
        }

        const std::vector<Vertex>& vertices = part->mesh->getVertecies();
        const std::vector<std::uint32_t>& indices = part->mesh->getFaces();
        if(vertices.empty() || indices.empty()){
            continue;
        }

        std::vector<float> vertexData;
        vertexData.reserve(vertices.size() * Vertex::VERTEX_DATA_WIDTH);
        for(const Vertex& vertex : vertices){
            vertexData.push_back(vertex.Position.x);
            vertexData.push_back(vertex.Position.y);
            vertexData.push_back(vertex.Position.z);
            vertexData.push_back(vertex.Color.x);
            vertexData.push_back(vertex.Color.y);
            vertexData.push_back(vertex.Color.z);
            vertexData.push_back(vertex.Color.w);
            vertexData.push_back(vertex.Normal.x);
            vertexData.push_back(vertex.Normal.y);
            vertexData.push_back(vertex.Normal.z);
            vertexData.push_back(vertex.TexCoords.x);
            vertexData.push_back(vertex.TexCoords.y);
        }

        yyjson_mut_val* partObj = yyjson_mut_arr_add_obj(doc, partsArr);
        if(!partObj){
            setComponentSerializationError(outError, "Failed to append embedded model part object.");
            return false;
        }

        if(!JsonUtils::MutObjAddBool(doc, partObj, "visible", part->visible) ||
           !JsonUtils::MutObjAddBool(doc, partObj, "hideInEditorTree", part->hideInEditorTree) ||
           !writeTransformFields(doc, partObj, "", part->localTransform) ||
           !addFloatArrayField(doc, partObj, "vertexData", vertexData, outError) ||
           !addUIntArrayField(doc, partObj, "indexData", indices, outError)){
            if(outError && outError->empty()){
                *outError = "Failed to serialize embedded model part.";
            }
            return false;
        }

        std::string partMaterialAssetRef;
        if(partIndex < component.modelPartMaterialAssetRefs.size()){
            partMaterialAssetRef = component.modelPartMaterialAssetRefs[partIndex];
        }
        if(!partMaterialAssetRef.empty() &&
           !JsonUtils::MutObjAddString(doc, partObj, "materialAssetRef", partMaterialAssetRef)){
            setComponentSerializationError(outError, "Failed to serialize embedded part material asset ref.");
            return false;
        }

        MaterialAssetData embeddedMaterial;
        if(captureEmbeddedMaterialData(part->material, embeddedMaterial) &&
           !writeEmbeddedMaterialDataField(embeddedMaterial, doc, partObj, outError)){
            return false;
        }
    }

    return true;
}

bool tryReadEmbeddedModelField(yyjson_val* payload,
                               MeshRendererComponent& component,
                               std::string* outError){
    yyjson_val* embeddedModelObj = JsonUtils::ObjGetObject(payload, "embeddedModel");
    if(!embeddedModelObj){
        return true;
    }

    yyjson_val* partsArr = JsonUtils::ObjGetArray(embeddedModelObj, "parts");
    if(!partsArr){
        return true;
    }

    auto reconstructedModel = Model::Create();
    if(!reconstructedModel){
        setComponentSerializationError(outError, "Failed to allocate embedded model.");
        return false;
    }

    std::vector<float> vertexData;
    std::vector<std::uint32_t> indexData;
    const size_t partCount = yyjson_arr_size(partsArr);
    if(component.modelPartMaterialAssetRefs.size() < partCount){
        component.modelPartMaterialAssetRefs.resize(partCount);
    }
    for(size_t i = 0; i < partCount; ++i){
        yyjson_val* partObj = yyjson_arr_get(partsArr, i);
        if(!partObj || !yyjson_is_obj(partObj)){
            continue;
        }

        if(!readFloatArrayField(partObj, "vertexData", vertexData, outError) ||
           !readUIntArrayField(partObj, "indexData", indexData, outError)){
            return false;
        }
        if(vertexData.empty() || indexData.empty()){
            continue;
        }
        if(vertexData.size() % Vertex::VERTEX_DATA_WIDTH != 0){
            setComponentSerializationError(outError, "Embedded model vertex data is not aligned to Vertex::VERTEX_DATA_WIDTH.");
            return false;
        }

        std::vector<Vertex> vertices;
        vertices.reserve(vertexData.size() / Vertex::VERTEX_DATA_WIDTH);
        for(size_t rawIndex = 0; rawIndex < vertexData.size(); rawIndex += Vertex::VERTEX_DATA_WIDTH){
            Vertex vertex;
            vertex.Position = Math3D::Vec3(vertexData[rawIndex + 0], vertexData[rawIndex + 1], vertexData[rawIndex + 2]);
            vertex.Color = Math3D::Vec4(vertexData[rawIndex + 3], vertexData[rawIndex + 4], vertexData[rawIndex + 5], vertexData[rawIndex + 6]);
            vertex.Normal = Math3D::Vec3(vertexData[rawIndex + 7], vertexData[rawIndex + 8], vertexData[rawIndex + 9]);
            vertex.TexCoords = Math3D::Vec2(vertexData[rawIndex + 10], vertexData[rawIndex + 11]);
            vertices.push_back(vertex);
        }

        auto mesh = std::make_shared<Mesh>();
        if(!mesh){
            setComponentSerializationError(outError, "Failed to allocate embedded mesh.");
            return false;
        }
        mesh->upload(std::move(vertices), std::move(indexData));

        auto part = std::make_shared<ModelPart>();
        if(!part){
            setComponentSerializationError(outError, "Failed to allocate embedded model part.");
            return false;
        }

        part->mesh = mesh;
        std::string partMaterialAssetRef;
        JsonUtils::TryGetString(partObj, "materialAssetRef", partMaterialAssetRef);
        if(partMaterialAssetRef.empty() && i < component.modelPartMaterialAssetRefs.size()){
            partMaterialAssetRef = component.modelPartMaterialAssetRefs[i];
        }
        if(i < component.modelPartMaterialAssetRefs.size()){
            component.modelPartMaterialAssetRefs[i] = partMaterialAssetRef;
        }

        std::shared_ptr<Material> partMaterial;
        if(!partMaterialAssetRef.empty()){
            partMaterial = MaterialAssetIO::InstantiateMaterialFromRef(partMaterialAssetRef, nullptr, nullptr);
        }
        if(!partMaterial){
            MaterialAssetData embeddedMaterial;
            if(tryReadEmbeddedMaterialDataField(partObj, embeddedMaterial)){
                partMaterial = MaterialAssetIO::InstantiateMaterial(embeddedMaterial, nullptr);
            }
        }
        if(!partMaterial){
            partMaterial = PBRMaterial::Create(Color::WHITE);
        }
        if(!partMaterial){
            partMaterial = MaterialDefaults::LitColorMaterial::Create(Color::WHITE);
        }
        part->material = partMaterial;
        part->visible = JsonUtils::GetBoolOrDefault(partObj, "visible", true);
        part->hideInEditorTree = JsonUtils::GetBoolOrDefault(partObj, "hideInEditorTree", true);
        readTransformFields(partObj, part->localTransform);
        reconstructedModel->addPart(part);
    }

    if(reconstructedModel->getParts().empty()){
        return true;
    }

    reconstructedModel->setBackfaceCulling(JsonUtils::GetBoolOrDefault(embeddedModelObj, "enableBackfaceCulling", true));
    component.model = reconstructedModel;
    return true;
}

bool registerDefaultTransformSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<TransformComponent>(
        "TransformComponent",
        1,
        [](const TransformComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            Math3D::Vec3 rotationEuler = component.local.rotation.ToEuler();
            return JsonUtils::MutObjAddVec3(doc, payload, "position", component.local.position) &&
                   JsonUtils::MutObjAddVec3(doc, payload, "rotationEuler", rotationEuler) &&
                   JsonUtils::MutObjAddVec3(doc, payload, "scale", component.local.scale);
        },
        [](TransformComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            Math3D::Vec3 position = component.local.position;
            Math3D::Vec3 scale = component.local.scale;
            Math3D::Vec3 rotationEuler = component.local.rotation.ToEuler();

            JsonUtils::TryGetVec3(payload, "position", position);
            JsonUtils::TryGetVec3(payload, "rotationEuler", rotationEuler);
            JsonUtils::TryGetVec3(payload, "scale", scale);

            component.local.position = position;
            component.local.setRotation(rotationEuler);
            component.local.scale = scale;
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultBoundsSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<BoundsComponent>(
        "BoundsComponent",
        1,
        [](const BoundsComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddInt(doc, payload, "type", static_cast<int>(component.type)) &&
                   JsonUtils::MutObjAddVec3(doc, payload, "size", component.size) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "radius", component.radius) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "height", component.height);
        },
        [](BoundsComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;

            int type = static_cast<int>(component.type);
            Math3D::Vec3 size = component.size;
            float radius = component.radius;
            float height = component.height;

            JsonUtils::TryGetInt(payload, "type", type);
            JsonUtils::TryGetVec3(payload, "size", size);
            JsonUtils::TryGetFloat(payload, "radius", radius);
            JsonUtils::TryGetFloat(payload, "height", height);

            component.type = enumFromIntClamped(type, 0, 2, BoundsType::Sphere);
            component.size = size;
            component.radius = Math3D::Max(0.0f, radius);
            component.height = Math3D::Max(0.0f, height);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultMeshRendererSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<MeshRendererComponent>(
        "MeshRendererComponent",
        1,
        [](const MeshRendererComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            std::string modelSourceRef = component.modelSourceRef;
            bool modelForceSmoothNormals = (component.modelForceSmoothNormals != 0);
            if(component.model){
                if(modelSourceRef.empty()){
                    modelSourceRef = component.model->getSourceAssetRef();
                }
                if(modelSourceRef == component.model->getSourceAssetRef()){
                    modelForceSmoothNormals = component.model->getSourceForceSmoothNormals();
                }
            }
            const bool shouldEmbedModel = component.model &&
                                          component.modelAssetRef.empty() &&
                                          modelSourceRef.empty();

            if(!JsonUtils::MutObjAddBool(doc, payload, "visible", component.visible) ||
               !JsonUtils::MutObjAddBool(doc, payload, "enableBackfaceCulling", component.enableBackfaceCulling) ||
               !JsonUtils::MutObjAddString(doc, payload, "modelAssetRef", component.modelAssetRef) ||
               !JsonUtils::MutObjAddString(doc, payload, "modelSourceRef", modelSourceRef) ||
               !JsonUtils::MutObjAddBool(doc, payload, "modelForceSmoothNormals", modelForceSmoothNormals) ||
               !JsonUtils::MutObjAddString(doc, payload, "materialAssetRef", component.materialAssetRef)){
                setComponentSerializationError(error, "Failed to write mesh renderer scalar fields.");
                return false;
            }

            yyjson_mut_val* offsetObj = yyjson_mut_obj_add_obj(doc, payload, "localOffset");
            if(!offsetObj){
                setComponentSerializationError(error, "Failed to write mesh renderer localOffset object.");
                return false;
            }
            if(!writeTransformFields(doc, offsetObj, "", component.localOffset)){
                setComponentSerializationError(error, "Failed to write mesh renderer localOffset fields.");
                return false;
            }

            if(!addStringArrayField(doc, payload, "modelPartMaterialAssetRefs", component.modelPartMaterialAssetRefs, error)){
                return false;
            }
            if(shouldEmbedModel && !writeEmbeddedModelField(component, doc, payload, error)){
                return false;
            }
            return true;
        },
        [](MeshRendererComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;

            JsonUtils::TryGetBool(payload, "visible", component.visible);
            JsonUtils::TryGetBool(payload, "enableBackfaceCulling", component.enableBackfaceCulling);
            JsonUtils::TryGetString(payload, "modelAssetRef", component.modelAssetRef);
            JsonUtils::TryGetString(payload, "modelSourceRef", component.modelSourceRef);
            bool modelForceSmoothNormals = (component.modelForceSmoothNormals != 0);
            JsonUtils::TryGetBool(payload, "modelForceSmoothNormals", modelForceSmoothNormals);
            component.modelForceSmoothNormals = modelForceSmoothNormals ? 1 : 0;
            JsonUtils::TryGetString(payload, "materialAssetRef", component.materialAssetRef);

            if(yyjson_val* offsetObj = JsonUtils::ObjGetObject(payload, "localOffset")){
                readTransformFields(offsetObj, component.localOffset);
            }
            readStringArrayField(payload, "modelPartMaterialAssetRefs", component.modelPartMaterialAssetRefs, nullptr);

            component.mesh.reset();
            component.material.reset();
            component.model.reset();

            if(!component.modelAssetRef.empty()){
                component.model = ModelAssetIO::InstantiateModelFromRef(component.modelAssetRef, nullptr, nullptr);
            }

            if(!component.model && !component.modelSourceRef.empty()){
                const std::filesystem::path sourcePath(component.modelSourceRef);
                const std::string sourceExt = StringUtils::ToLowerCase(sourcePath.extension().string());
                auto sourceAsset = AssetManager::Instance.getOrLoad(component.modelSourceRef);
                if(sourceAsset && sourceExt == ".obj"){
                    component.model = OBJLoader::LoadFromAsset(
                        sourceAsset,
                        nullptr,
                        component.modelForceSmoothNormals != 0
                    );
                }
            }
            if(!component.model && !tryReadEmbeddedModelField(payload, component, error)){
                return false;
            }

            if(component.model && component.modelSourceRef.empty()){
                component.modelSourceRef = component.model->getSourceAssetRef();
                component.modelForceSmoothNormals = component.model->getSourceForceSmoothNormals() ? 1 : 0;
            }

            if(component.model){
                const auto& parts = component.model->getParts();
                if(!component.modelPartMaterialAssetRefs.empty()){
                    const size_t maxCount = std::min(parts.size(), component.modelPartMaterialAssetRefs.size());
                    for(size_t i = 0; i < maxCount; ++i){
                        auto& part = parts[i];
                        if(!part){
                            continue;
                        }
                        if(component.modelPartMaterialAssetRefs[i].empty()){
                            continue;
                        }
                        std::shared_ptr<Material> material =
                            MaterialAssetIO::InstantiateMaterialFromRef(component.modelPartMaterialAssetRefs[i], nullptr, nullptr);
                        if(material){
                            part->material = material;
                        }
                    }
                }
            }

            if(!component.materialAssetRef.empty()){
                component.material =
                    MaterialAssetIO::InstantiateMaterialFromRef(component.materialAssetRef, nullptr, nullptr);
            }

            return true;
        },
        {},
        outError
    );
}

bool registerDefaultLightSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<LightComponent>(
        "LightComponent",
        1,
        [](const LightComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            yyjson_mut_val* lightObj = yyjson_mut_obj_add_obj(doc, payload, "light");
            if(!lightObj){
                setComponentSerializationError(error, "Failed to allocate light payload object.");
                return false;
            }

            return JsonUtils::MutObjAddInt(doc, lightObj, "type", static_cast<int>(component.light.type)) &&
                   JsonUtils::MutObjAddVec3(doc, lightObj, "position", component.light.position) &&
                   JsonUtils::MutObjAddVec3(doc, lightObj, "direction", component.light.direction) &&
                   JsonUtils::MutObjAddVec4(doc, lightObj, "color", component.light.color) &&
                   JsonUtils::MutObjAddFloat(doc, lightObj, "intensity", component.light.intensity) &&
                   JsonUtils::MutObjAddFloat(doc, lightObj, "range", component.light.range) &&
                   JsonUtils::MutObjAddFloat(doc, lightObj, "falloff", component.light.falloff) &&
                   JsonUtils::MutObjAddFloat(doc, lightObj, "spotAngle", component.light.spotAngle) &&
                   JsonUtils::MutObjAddFloat(doc, lightObj, "shadowRange", component.light.shadowRange) &&
                   JsonUtils::MutObjAddBool(doc, lightObj, "castsShadows", component.light.castsShadows) &&
                   JsonUtils::MutObjAddInt(doc, lightObj, "shadowType", static_cast<int>(component.light.shadowType)) &&
                   JsonUtils::MutObjAddFloat(doc, lightObj, "shadowBias", component.light.shadowBias) &&
                   JsonUtils::MutObjAddFloat(doc, lightObj, "shadowNormalBias", component.light.shadowNormalBias) &&
                   JsonUtils::MutObjAddFloat(doc, lightObj, "shadowStrength", component.light.shadowStrength) &&
                   JsonUtils::MutObjAddBool(doc, payload, "syncTransform", component.syncTransform) &&
                   JsonUtils::MutObjAddBool(doc, payload, "syncDirection", component.syncDirection);
        },
        [](LightComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;

            yyjson_val* lightObj = JsonUtils::ObjGetObject(payload, "light");
            if(lightObj){
                int type = static_cast<int>(component.light.type);
                int shadowType = static_cast<int>(component.light.shadowType);
                Math3D::Vec3 direction = component.light.direction;

                JsonUtils::TryGetInt(lightObj, "type", type);
                JsonUtils::TryGetVec3(lightObj, "position", component.light.position);
                JsonUtils::TryGetVec3(lightObj, "direction", direction);
                JsonUtils::TryGetVec4(lightObj, "color", component.light.color);
                JsonUtils::TryGetFloat(lightObj, "intensity", component.light.intensity);
                JsonUtils::TryGetFloat(lightObj, "range", component.light.range);
                JsonUtils::TryGetFloat(lightObj, "falloff", component.light.falloff);
                JsonUtils::TryGetFloat(lightObj, "spotAngle", component.light.spotAngle);
                JsonUtils::TryGetFloat(lightObj, "shadowRange", component.light.shadowRange);
                JsonUtils::TryGetBool(lightObj, "castsShadows", component.light.castsShadows);
                JsonUtils::TryGetInt(lightObj, "shadowType", shadowType);
                JsonUtils::TryGetFloat(lightObj, "shadowBias", component.light.shadowBias);
                JsonUtils::TryGetFloat(lightObj, "shadowNormalBias", component.light.shadowNormalBias);
                JsonUtils::TryGetFloat(lightObj, "shadowStrength", component.light.shadowStrength);

                component.light.type = enumFromIntClamped(type, 0, 2, LightType::POINT);
                component.light.shadowType = enumFromIntClamped(shadowType, 0, 2, ShadowType::Smooth);
                if(direction.length() > Math3D::EPSILON){
                    component.light.direction = direction.normalize();
                }
            }

            JsonUtils::TryGetBool(payload, "syncTransform", component.syncTransform);
            JsonUtils::TryGetBool(payload, "syncDirection", component.syncDirection);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultCameraSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<CameraComponent>(
        "CameraComponent",
        1,
        [](const CameraComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            if(!JsonUtils::MutObjAddBool(doc, payload, "hasCamera", component.camera != nullptr)){
                setComponentSerializationError(error, "Failed to write hasCamera field.");
                return false;
            }
            if(!component.camera){
                return true;
            }

            const CameraSettings& settings = component.camera->getSettings();
            yyjson_mut_val* cameraObj = yyjson_mut_obj_add_obj(doc, payload, "camera");
            if(!cameraObj){
                setComponentSerializationError(error, "Failed to create camera payload object.");
                return false;
            }

            return JsonUtils::MutObjAddBool(doc, cameraObj, "isOrtho", settings.isOrtho) &&
                   JsonUtils::MutObjAddFloat(doc, cameraObj, "nearPlane", settings.nearPlane) &&
                   JsonUtils::MutObjAddFloat(doc, cameraObj, "farPlane", settings.farPlane) &&
                   JsonUtils::MutObjAddFloat(doc, cameraObj, "fov", settings.fov) &&
                   JsonUtils::MutObjAddFloat(doc, cameraObj, "aspect", settings.aspect) &&
                   JsonUtils::MutObjAddVec2(doc, cameraObj, "viewPlanePosition", settings.viewPlane.position) &&
                   JsonUtils::MutObjAddVec2(doc, cameraObj, "viewPlaneSize", settings.viewPlane.size);
        },
        [](CameraComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            bool hasCamera = (component.camera != nullptr);
            JsonUtils::TryGetBool(payload, "hasCamera", hasCamera);
            if(!hasCamera){
                component.camera.reset();
                return true;
            }

            if(!component.camera){
                component.camera = Camera::CreatePerspective(45.0f, Math3D::Vec2(1280.0f, 720.0f), 0.1f, 1000.0f);
            }
            if(!component.camera){
                setComponentSerializationError(error, "Failed to allocate camera while deserializing camera component.");
                return false;
            }

            if(yyjson_val* cameraObj = JsonUtils::ObjGetObject(payload, "camera")){
                CameraSettings settings = component.camera->getSettings();
                JsonUtils::TryGetBool(cameraObj, "isOrtho", settings.isOrtho);
                JsonUtils::TryGetFloat(cameraObj, "nearPlane", settings.nearPlane);
                JsonUtils::TryGetFloat(cameraObj, "farPlane", settings.farPlane);
                JsonUtils::TryGetFloat(cameraObj, "fov", settings.fov);
                JsonUtils::TryGetFloat(cameraObj, "aspect", settings.aspect);
                JsonUtils::TryGetVec2(cameraObj, "viewPlanePosition", settings.viewPlane.position);
                JsonUtils::TryGetVec2(cameraObj, "viewPlaneSize", settings.viewPlane.size);
                component.camera->getSettings() = settings;
            }

            return true;
        },
        {},
        outError
    );
}

bool registerDefaultSkyboxSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<SkyboxComponent>(
        "SkyboxComponent",
        1,
        [](const SkyboxComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            (void)error;
            return JsonUtils::MutObjAddString(doc, payload, "skyboxAssetRef", StringUtils::Trim(component.skyboxAssetRef));
        },
        [](SkyboxComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            JsonUtils::TryGetString(payload, "skyboxAssetRef", component.skyboxAssetRef);
            component.skyboxAssetRef = StringUtils::Trim(component.skyboxAssetRef);
            component.loadedSkyboxAssetRef.clear();
            component.runtimeSkyBox.reset();
            return true;
        },
        [](NeoECS::GameObject* wrapper, std::string* error) -> bool {
            if(!wrapper){
                if(error){
                    *error = "Null GameObject wrapper while ensuring SkyboxComponent.";
                }
                return false;
            }

            if(!wrapper->hasComponent<CameraComponent>() && !wrapper->addComponent<CameraComponent>()){
                if(error){
                    *error = "SkyboxComponent requires CameraComponent.";
                }
                return false;
            }
            if(auto* camera = wrapper->getComponent<CameraComponent>()){
                if(!camera->camera){
                    camera->camera = Camera::CreatePerspective(45.0f, Math3D::Vec2(1280.0f, 720.0f), 0.1f, 1000.0f);
                    if(!camera->camera){
                        if(error){
                            *error = "Failed to allocate default camera for SkyboxComponent.";
                        }
                        return false;
                    }
                }
            }
            if(wrapper->hasComponent<SkyboxComponent>()){
                return true;
            }
            if(!wrapper->addComponent<SkyboxComponent>()){
                if(error){
                    *error = "Failed to add missing SkyboxComponent to entity.";
                }
                return false;
            }
            return wrapper->hasComponent<SkyboxComponent>();
        },
        outError
    );
}

bool registerDefaultColliderSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<ColliderComponent>(
        "ColliderComponent",
        1,
        [](const ColliderComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            if(!JsonUtils::MutObjAddInt(doc, payload, "shape", static_cast<int>(component.shape)) ||
               !JsonUtils::MutObjAddVec3(doc, payload, "boxHalfExtents", component.boxHalfExtents) ||
               !JsonUtils::MutObjAddFloat(doc, payload, "sphereRadius", component.sphereRadius) ||
               !JsonUtils::MutObjAddFloat(doc, payload, "capsuleRadius", component.capsuleRadius) ||
               !JsonUtils::MutObjAddFloat(doc, payload, "capsuleHeight", component.capsuleHeight) ||
               !JsonUtils::MutObjAddInt(doc, payload, "layer", static_cast<int>(component.layer)) ||
               !JsonUtils::MutObjAddUInt64(doc, payload, "collisionMask", static_cast<std::uint64_t>(component.collisionMask)) ||
               !JsonUtils::MutObjAddBool(doc, payload, "isTrigger", component.isTrigger)){
                setComponentSerializationError(error, "Failed to write collider scalar fields.");
                return false;
            }

            yyjson_mut_val* offsetObj = yyjson_mut_obj_add_obj(doc, payload, "localOffset");
            if(!offsetObj || !writeTransformFields(doc, offsetObj, "", component.localOffset)){
                setComponentSerializationError(error, "Failed to write collider local offset.");
                return false;
            }

            yyjson_mut_val* materialObj = yyjson_mut_obj_add_obj(doc, payload, "material");
            if(!materialObj){
                setComponentSerializationError(error, "Failed to write collider material object.");
                return false;
            }
            return JsonUtils::MutObjAddFloat(doc, materialObj, "staticFriction", component.material.staticFriction) &&
                   JsonUtils::MutObjAddFloat(doc, materialObj, "dynamicFriction", component.material.dynamicFriction) &&
                   JsonUtils::MutObjAddFloat(doc, materialObj, "restitution", component.material.restitution) &&
                   JsonUtils::MutObjAddFloat(doc, materialObj, "density", component.material.density);
        },
        [](ColliderComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            int shape = static_cast<int>(component.shape);
            int layer = static_cast<int>(component.layer);
            std::uint64_t mask = static_cast<std::uint64_t>(component.collisionMask);

            JsonUtils::TryGetInt(payload, "shape", shape);
            JsonUtils::TryGetVec3(payload, "boxHalfExtents", component.boxHalfExtents);
            JsonUtils::TryGetFloat(payload, "sphereRadius", component.sphereRadius);
            JsonUtils::TryGetFloat(payload, "capsuleRadius", component.capsuleRadius);
            JsonUtils::TryGetFloat(payload, "capsuleHeight", component.capsuleHeight);
            JsonUtils::TryGetInt(payload, "layer", layer);
            JsonUtils::TryGetUInt64(payload, "collisionMask", mask);
            JsonUtils::TryGetBool(payload, "isTrigger", component.isTrigger);

            if(yyjson_val* offsetObj = JsonUtils::ObjGetObject(payload, "localOffset")){
                readTransformFields(offsetObj, component.localOffset);
            }
            if(yyjson_val* materialObj = JsonUtils::ObjGetObject(payload, "material")){
                JsonUtils::TryGetFloat(materialObj, "staticFriction", component.material.staticFriction);
                JsonUtils::TryGetFloat(materialObj, "dynamicFriction", component.material.dynamicFriction);
                JsonUtils::TryGetFloat(materialObj, "restitution", component.material.restitution);
                JsonUtils::TryGetFloat(materialObj, "density", component.material.density);
            }

            component.shape = enumFromIntClamped(shape, 0, 2, PhysicsColliderShape::Box);
            component.layer = enumFromIntClamped(layer, 0, 4, PhysicsLayer::Default);
            component.collisionMask = static_cast<PhysicsLayerMask>(mask);
            component.sphereRadius = Math3D::Max(0.01f, component.sphereRadius);
            component.capsuleRadius = Math3D::Max(0.01f, component.capsuleRadius);
            component.capsuleHeight = Math3D::Max(0.01f, component.capsuleHeight);
            component.material.staticFriction = Math3D::Max(0.0f, component.material.staticFriction);
            component.material.dynamicFriction = Math3D::Max(0.0f, component.material.dynamicFriction);
            component.material.restitution = Math3D::Clamp(component.material.restitution, 0.0f, 1.0f);
            component.material.density = Math3D::Max(0.001f, component.material.density);
            component.runtimeShapeHandle = nullptr;
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultRigidBodySerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<RigidBodyComponent>(
        "RigidBodyComponent",
        1,
        [](const RigidBodyComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddInt(doc, payload, "bodyType", static_cast<int>(component.bodyType)) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "mass", component.mass) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "gravityScale", component.gravityScale) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "linearDamping", component.linearDamping) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "angularDamping", component.angularDamping) &&
                   JsonUtils::MutObjAddVec3(doc, payload, "linearVelocity", component.linearVelocity) &&
                   JsonUtils::MutObjAddVec3(doc, payload, "angularVelocity", component.angularVelocity) &&
                   JsonUtils::MutObjAddBool(doc, payload, "lockLinearX", component.lockLinearX) &&
                   JsonUtils::MutObjAddBool(doc, payload, "lockLinearY", component.lockLinearY) &&
                   JsonUtils::MutObjAddBool(doc, payload, "lockLinearZ", component.lockLinearZ) &&
                   JsonUtils::MutObjAddBool(doc, payload, "lockAngularX", component.lockAngularX) &&
                   JsonUtils::MutObjAddBool(doc, payload, "lockAngularY", component.lockAngularY) &&
                   JsonUtils::MutObjAddBool(doc, payload, "lockAngularZ", component.lockAngularZ) &&
                   JsonUtils::MutObjAddBool(doc, payload, "useContinuousCollision", component.useContinuousCollision) &&
                   JsonUtils::MutObjAddBool(doc, payload, "canSleep", component.canSleep) &&
                   JsonUtils::MutObjAddBool(doc, payload, "startAwake", component.startAwake);
        },
        [](RigidBodyComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;

            int bodyType = static_cast<int>(component.bodyType);
            JsonUtils::TryGetInt(payload, "bodyType", bodyType);
            JsonUtils::TryGetFloat(payload, "mass", component.mass);
            JsonUtils::TryGetFloat(payload, "gravityScale", component.gravityScale);
            JsonUtils::TryGetFloat(payload, "linearDamping", component.linearDamping);
            JsonUtils::TryGetFloat(payload, "angularDamping", component.angularDamping);
            JsonUtils::TryGetVec3(payload, "linearVelocity", component.linearVelocity);
            JsonUtils::TryGetVec3(payload, "angularVelocity", component.angularVelocity);
            JsonUtils::TryGetBool(payload, "lockLinearX", component.lockLinearX);
            JsonUtils::TryGetBool(payload, "lockLinearY", component.lockLinearY);
            JsonUtils::TryGetBool(payload, "lockLinearZ", component.lockLinearZ);
            JsonUtils::TryGetBool(payload, "lockAngularX", component.lockAngularX);
            JsonUtils::TryGetBool(payload, "lockAngularY", component.lockAngularY);
            JsonUtils::TryGetBool(payload, "lockAngularZ", component.lockAngularZ);
            JsonUtils::TryGetBool(payload, "useContinuousCollision", component.useContinuousCollision);
            JsonUtils::TryGetBool(payload, "canSleep", component.canSleep);
            JsonUtils::TryGetBool(payload, "startAwake", component.startAwake);

            component.bodyType = enumFromIntClamped(bodyType, 0, 2, PhysicsBodyType::Dynamic);
            component.mass = Math3D::Max(0.001f, component.mass);
            component.linearDamping = Math3D::Max(0.0f, component.linearDamping);
            component.angularDamping = Math3D::Max(0.0f, component.angularDamping);
            component.runtimeBodyHandle = nullptr;
            if(!component.canSleep){
                component.startAwake = true;
            }
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultScriptSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<ScriptComponent>(
        "ScriptComponent",
        1,
        [](const ScriptComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            return addStringArrayField(doc, payload, "scriptAssetRefs", component.scriptAssetRefs, error);
        },
        [](ScriptComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            std::vector<std::string> refs;
            if(!readStringArrayField(payload, "scriptAssetRefs", refs, error)){
                return false;
            }

            component.scriptAssetRefs.clear();
            for(const std::string& ref : refs){
                component.addScriptAsset(ref);
            }
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultSsaoSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<SSAOComponent>(
        "SSAOComponent",
        1,
        [](const SSAOComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddBool(doc, payload, "enabled", component.enabled) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "radiusPx", component.radiusPx) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "depthRadius", component.depthRadius) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "bias", component.bias) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "intensity", component.intensity) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "giBoost", component.giBoost) &&
                   JsonUtils::MutObjAddInt(doc, payload, "sampleCount", component.sampleCount);
        },
        [](SSAOComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            JsonUtils::TryGetBool(payload, "enabled", component.enabled);
            JsonUtils::TryGetFloat(payload, "radiusPx", component.radiusPx);
            JsonUtils::TryGetFloat(payload, "depthRadius", component.depthRadius);
            JsonUtils::TryGetFloat(payload, "bias", component.bias);
            JsonUtils::TryGetFloat(payload, "intensity", component.intensity);
            JsonUtils::TryGetFloat(payload, "giBoost", component.giBoost);
            JsonUtils::TryGetInt(payload, "sampleCount", component.sampleCount);
            component.runtimeEffect.reset();
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultDepthOfFieldSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<DepthOfFieldComponent>(
        "DepthOfFieldComponent",
        1,
        [](const DepthOfFieldComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddBool(doc, payload, "enabled", component.enabled) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "focusDistance", component.focusDistance) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "focusRange", component.focusRange) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "blurStrength", component.blurStrength) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "maxBlurPx", component.maxBlurPx) &&
                   JsonUtils::MutObjAddInt(doc, payload, "sampleCount", component.sampleCount);
        },
        [](DepthOfFieldComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            JsonUtils::TryGetBool(payload, "enabled", component.enabled);
            JsonUtils::TryGetFloat(payload, "focusDistance", component.focusDistance);
            JsonUtils::TryGetFloat(payload, "focusRange", component.focusRange);
            JsonUtils::TryGetFloat(payload, "blurStrength", component.blurStrength);
            JsonUtils::TryGetFloat(payload, "maxBlurPx", component.maxBlurPx);
            JsonUtils::TryGetInt(payload, "sampleCount", component.sampleCount);
            component.runtimeEffect.reset();
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultAntiAliasingSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<AntiAliasingComponent>(
        "AntiAliasingComponent",
        1,
        [](const AntiAliasingComponent& component, yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddInt(doc, payload, "preset", static_cast<int>(component.preset));
        },
        [](AntiAliasingComponent& component, yyjson_val* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            int preset = static_cast<int>(component.preset);
            JsonUtils::TryGetInt(payload, "preset", preset);
            component.preset = enumFromIntClamped(preset, 0, 3, AntiAliasingPreset::FXAA_Medium);
            component.runtimeEffect.reset();
            return true;
        },
        {},
        outError
    );
}

} // namespace

namespace Serialization {

bool ComponentSerializationRegistry::registerSerializer(SerializerEntry entry, std::string* outError){
    if(entry.typeName.empty()){
        setComponentSerializationError(outError, "Component serializer registration requires a non-empty type name.");
        return false;
    }
    if(entry.version <= 0){
        setComponentSerializationError(outError, "Component serializer registration requires version > 0.");
        return false;
    }
    if(!entry.getComponent){
        setComponentSerializationError(outError, "Component serializer registration requires a getComponent callback.");
        return false;
    }
    if(!entry.ensureComponent){
        setComponentSerializationError(outError, "Component serializer registration requires an ensureComponent callback.");
        return false;
    }
    if(!entry.serializePayload){
        setComponentSerializationError(outError, "Component serializer registration requires a serializePayload callback.");
        return false;
    }
    if(!entry.deserializePayload){
        setComponentSerializationError(outError, "Component serializer registration requires a deserializePayload callback.");
        return false;
    }
    if(serializerIndexByType.find(entry.typeName) != serializerIndexByType.end()){
        setComponentSerializationError(outError, "Duplicate component serializer type registration: " + entry.typeName);
        return false;
    }

    serializerIndexByType[entry.typeName] = orderedSerializers.size();
    orderedSerializers.push_back(std::move(entry));
    return true;
}

bool ComponentSerializationRegistry::hasSerializer(const std::string& typeName) const{
    return serializerIndexByType.find(typeName) != serializerIndexByType.end();
}

bool ComponentSerializationRegistry::serializeEntityComponents(
    NeoECS::ECSComponentManager* manager,
    NeoECS::ECSEntity* entity,
    std::vector<ComponentRecord>& outRecords,
    std::string* outError
) const{
    if(!manager || !entity){
        setComponentSerializationError(outError, "Cannot serialize components for null manager/entity.");
        return false;
    }

    outRecords.clear();
    outRecords.reserve(orderedSerializers.size());

    for(const SerializerEntry& serializer : orderedSerializers){
        NeoECS::ECSComponent* component = serializer.getComponent(manager, entity);
        if(!component){
            continue;
        }

        ComponentRecord record;
        if(!serializeComponentRecord(serializer.typeName, component, record, outError)){
            if(outError && !outError->empty()){
                *outError = "Failed to serialize component '" + serializer.typeName + "': " + *outError;
            }
            return false;
        }
        outRecords.push_back(std::move(record));
    }

    return true;
}

bool ComponentSerializationRegistry::deserializeEntityComponents(
    NeoECS::ECSContext* context,
    NeoECS::ECSComponentManager* manager,
    NeoECS::ECSEntity* entity,
    const std::vector<ComponentRecord>& records,
    std::string* outError
) const{
    if(!context || !manager || !entity){
        setComponentSerializationError(outError, "Cannot deserialize components for null context/manager/entity.");
        return false;
    }

    std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(context, entity));
    if(!wrapper){
        setComponentSerializationError(outError, "Failed to create GameObject wrapper for entity component deserialization.");
        return false;
    }

    for(size_t i = 0; i < records.size(); ++i){
        if(!deserializeComponentRecordWithWrapper(wrapper.get(), manager, entity, records[i], outError)){
            if(outError && !outError->empty()){
                *outError = "components[" + std::to_string(i) + "]: " + *outError;
            }
            return false;
        }
    }
    return true;
}

bool ComponentSerializationRegistry::serializeComponentRecord(
    const std::string& typeName,
    const NeoECS::ECSComponent* component,
    ComponentRecord& outRecord,
    std::string* outError
) const{
    auto it = serializerIndexByType.find(typeName);
    if(it == serializerIndexByType.end()){
        setComponentSerializationError(outError, "No serializer registered for component type '" + typeName + "'.");
        return false;
    }
    if(!component){
        setComponentSerializationError(outError, "Cannot serialize null component for type '" + typeName + "'.");
        return false;
    }

    const SerializerEntry& serializer = orderedSerializers[it->second];

    JsonUtils::MutableDocument payloadDoc;
    yyjson_mut_val* payload = payloadDoc.setRootObject();
    if(!payload){
        setComponentSerializationError(outError, "Failed to allocate component payload object for type '" + typeName + "'.");
        return false;
    }

    if(!serializer.serializePayload(component, payloadDoc.get(), payload, outError)){
        if(outError && !outError->empty()){
            *outError = "Serializer '" + typeName + "' failed: " + *outError;
        }
        return false;
    }

    std::string payloadJson;
    if(!JsonUtils::WriteDocumentToString(payloadDoc, payloadJson, outError, false)){
        if(outError && !outError->empty()){
            *outError = "Failed to build payload for '" + typeName + "': " + *outError;
        }
        return false;
    }

    outRecord.type = serializer.typeName;
    outRecord.version = serializer.version;
    outRecord.payloadJson = std::move(payloadJson);
    return true;
}

bool ComponentSerializationRegistry::deserializeComponentRecord(
    NeoECS::ECSContext* context,
    NeoECS::ECSComponentManager* manager,
    NeoECS::ECSEntity* entity,
    const ComponentRecord& record,
    std::string* outError
) const{
    if(!context || !manager || !entity){
        setComponentSerializationError(outError, "Cannot deserialize component record with null context/manager/entity.");
        return false;
    }

    std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(context, entity));
    if(!wrapper){
        setComponentSerializationError(outError, "Failed to create GameObject wrapper for component record deserialization.");
        return false;
    }
    return deserializeComponentRecordWithWrapper(wrapper.get(), manager, entity, record, outError);
}

bool ComponentSerializationRegistry::deserializeComponentRecordWithWrapper(
    NeoECS::GameObject* wrapper,
    NeoECS::ECSComponentManager* manager,
    NeoECS::ECSEntity* entity,
    const ComponentRecord& record,
    std::string* outError
) const{
    auto serializerIt = serializerIndexByType.find(record.type);
    if(serializerIt == serializerIndexByType.end()){
        // Unknown component types are ignored to keep loaders forward-compatible.
        return true;
    }

    const SerializerEntry& serializer = orderedSerializers[serializerIt->second];
    if(!serializer.ensureComponent(wrapper, outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to ensure component '" + record.type + "': " + *outError;
        }
        return false;
    }

    NeoECS::ECSComponent* component = serializer.getComponent(manager, entity);
    if(!component){
        setComponentSerializationError(outError, "Component '" + record.type + "' was not found after ensure step.");
        return false;
    }

    JsonUtils::Document payloadDoc;
    yyjson_val* payload = nullptr;
    if(!readPayloadFromJsonString(record.payloadJson, payloadDoc, payload, outError)){
        if(outError && !outError->empty()){
            *outError = "Invalid payload for component '" + record.type + "': " + *outError;
        }
        return false;
    }

    const int payloadVersion = (record.version > 0) ? record.version : serializer.version;
    if(!serializer.deserializePayload(component, payload, payloadVersion, outError)){
        if(outError && !outError->empty()){
            *outError = "Deserializer '" + record.type + "' failed: " + *outError;
        }
        return false;
    }
    return true;
}

ComponentSerializationRegistry ComponentSerializationRegistry::CreateDefault(){
    ComponentSerializationRegistry registry;
    RegisterDefaultComponentSerializers(registry, nullptr);
    return registry;
}

void RegisterDefaultComponentSerializers(ComponentSerializationRegistry& registry, std::string* outError){
    // Append new serializers here. Order defines deterministic output order.
    if(!registry.hasSerializer("TransformComponent") && !registerDefaultTransformSerializer(registry, outError)) return;
    if(!registry.hasSerializer("BoundsComponent") && !registerDefaultBoundsSerializer(registry, outError)) return;
    if(!registry.hasSerializer("MeshRendererComponent") && !registerDefaultMeshRendererSerializer(registry, outError)) return;
    if(!registry.hasSerializer("LightComponent") && !registerDefaultLightSerializer(registry, outError)) return;
    if(!registry.hasSerializer("CameraComponent") && !registerDefaultCameraSerializer(registry, outError)) return;
    if(!registry.hasSerializer("SkyboxComponent") && !registerDefaultSkyboxSerializer(registry, outError)) return;
    if(!registry.hasSerializer("ColliderComponent") && !registerDefaultColliderSerializer(registry, outError)) return;
    if(!registry.hasSerializer("RigidBodyComponent") && !registerDefaultRigidBodySerializer(registry, outError)) return;
    if(!registry.hasSerializer("ScriptComponent") && !registerDefaultScriptSerializer(registry, outError)) return;
    if(!registry.hasSerializer("SSAOComponent") && !registerDefaultSsaoSerializer(registry, outError)) return;
    if(!registry.hasSerializer("DepthOfFieldComponent") && !registerDefaultDepthOfFieldSerializer(registry, outError)) return;
    if(!registry.hasSerializer("AntiAliasingComponent") && !registerDefaultAntiAliasingSerializer(registry, outError)) return;
}

ComponentSerializationRegistry& DefaultComponentSerializationRegistry(){
    static ComponentSerializationRegistry registry = ComponentSerializationRegistry::CreateDefault();
    return registry;
}

} // namespace Serialization
