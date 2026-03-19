/**
 * @file src/Serialization/Schema/ComponentSerializationRegistry.cpp
 * @brief Implementation for ComponentSerializationRegistry.
 */

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
#include <cmath>
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

bool readPayloadFromJsonString(const std::string& json, JsonUtils::Document& outDoc, JsonUtils::JsonVal*& outPayload, std::string* outError){
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

bool addStringArrayField(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* obj, const char* key, const std::vector<std::string>& values, std::string* outError){
    JsonUtils::JsonMutVal* arr = yyjson_mut_obj_add_arr(doc, obj, key);
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

bool readStringArrayField(JsonUtils::JsonVal* obj, const char* key, std::vector<std::string>& outValues, std::string* outError){
    outValues.clear();
    JsonUtils::JsonVal* arr = JsonUtils::ObjGetArray(obj, key);
    if(!arr){
        return true;
    }

    const size_t count = yyjson_arr_size(arr);
    outValues.reserve(count);
    for(size_t i = 0; i < count; ++i){
        JsonUtils::JsonVal* item = yyjson_arr_get(arr, i);
        if(!item || !yyjson_is_str(item)){
            setComponentSerializationError(outError, std::string("Field '") + key + "' must be a string array.");
            return false;
        }
        const char* value = yyjson_get_str(item);
        outValues.push_back(value ? value : "");
    }
    return true;
}

bool writeEffectPropertyField(const EffectPropertyData& property,
                              yyjson_mut_doc* doc,
                              JsonUtils::JsonMutVal* obj,
                              std::string* outError){
    if(!JsonUtils::MutObjAddString(doc, obj, "key", property.key) ||
       !JsonUtils::MutObjAddString(doc, obj, "displayName", property.displayName) ||
       !JsonUtils::MutObjAddString(doc, obj, "uniformName", property.uniformName) ||
       !JsonUtils::MutObjAddString(doc, obj, "mirrorUniformName", property.mirrorUniformName) ||
       !JsonUtils::MutObjAddString(doc, obj, "presenceUniformName", property.presenceUniformName) ||
       !JsonUtils::MutObjAddInt(doc, obj, "type", static_cast<int>(property.type)) ||
       !JsonUtils::MutObjAddFloat(doc, obj, "floatValue", property.floatValue) ||
       !JsonUtils::MutObjAddInt(doc, obj, "intValue", property.intValue) ||
       !JsonUtils::MutObjAddBool(doc, obj, "boolValue", property.boolValue) ||
       !JsonUtils::MutObjAddVec2(doc, obj, "vec2Value", property.vec2Value) ||
       !JsonUtils::MutObjAddVec3(doc, obj, "vec3Value", property.vec3Value) ||
       !JsonUtils::MutObjAddVec4(doc, obj, "vec4Value", property.vec4Value) ||
       !JsonUtils::MutObjAddString(doc, obj, "textureAssetRef", property.textureAssetRef) ||
       !JsonUtils::MutObjAddInt(doc, obj, "textureSlot", property.textureSlot)){
        setComponentSerializationError(outError, "Failed to serialize effect property field.");
        return false;
    }
    return true;
}

bool readEffectPropertyField(JsonUtils::JsonVal* obj,
                             EffectPropertyData& outProperty,
                             std::string* outError){
    if(!obj || !yyjson_is_obj(obj)){
        setComponentSerializationError(outError, "Effect property entry must be an object.");
        return false;
    }

    outProperty = EffectPropertyData{};
    JsonUtils::TryGetString(obj, "key", outProperty.key);
    JsonUtils::TryGetString(obj, "displayName", outProperty.displayName);
    JsonUtils::TryGetString(obj, "uniformName", outProperty.uniformName);
    JsonUtils::TryGetString(obj, "mirrorUniformName", outProperty.mirrorUniformName);
    JsonUtils::TryGetString(obj, "presenceUniformName", outProperty.presenceUniformName);

    int typeValue = static_cast<int>(outProperty.type);
    JsonUtils::TryGetInt(obj, "type", typeValue);
    outProperty.type = enumFromIntClamped(typeValue, 0, 6, EffectPropertyType::Float);

    JsonUtils::TryGetFloat(obj, "floatValue", outProperty.floatValue);
    JsonUtils::TryGetInt(obj, "intValue", outProperty.intValue);
    JsonUtils::TryGetBool(obj, "boolValue", outProperty.boolValue);
    JsonUtils::TryGetVec2(obj, "vec2Value", outProperty.vec2Value);
    JsonUtils::TryGetVec3(obj, "vec3Value", outProperty.vec3Value);
    JsonUtils::TryGetVec4(obj, "vec4Value", outProperty.vec4Value);
    JsonUtils::TryGetString(obj, "textureAssetRef", outProperty.textureAssetRef);
    JsonUtils::TryGetInt(obj, "textureSlot", outProperty.textureSlot);
    outProperty.textureSlot = std::max(outProperty.textureSlot, 0);

    if(outProperty.key.empty()){
        if(!outProperty.uniformName.empty()){
            outProperty.key = outProperty.uniformName;
        }else{
            outProperty.key = "property";
        }
    }
    if(outProperty.displayName.empty()){
        outProperty.displayName = SanitizeEffectDisplayName(
            !outProperty.key.empty() ? outProperty.key : outProperty.uniformName
        );
    }
    outProperty.texturePtr.reset();
    outProperty.loadedTextureRef.clear();
    outProperty.loadedTextureRevision = 0;
    return true;
}

bool writePostProcessingStackEffectsField(const PostProcessingStackComponent& component,
                                          yyjson_mut_doc* doc,
                                          JsonUtils::JsonMutVal* payload,
                                          std::string* outError){
    JsonUtils::JsonMutVal* effectsArr = yyjson_mut_obj_add_arr(doc, payload, "effects");
    if(!effectsArr){
        setComponentSerializationError(outError, "Failed to create post-processing effect array.");
        return false;
    }

    for(const PostProcessingEffectEntry& effect : component.effects){
        JsonUtils::JsonMutVal* effectObj = yyjson_mut_arr_add_obj(doc, effectsArr);
        if(!effectObj){
            setComponentSerializationError(outError, "Failed to append post-processing effect entry.");
            return false;
        }

        if(!JsonUtils::MutObjAddInt(doc, effectObj, "kind", static_cast<int>(effect.kind)) ||
           !JsonUtils::MutObjAddBool(doc, effectObj, "enabled", effect.enabled) ||
           !JsonUtils::MutObjAddBool(doc, effectObj, "editorExpanded", effect.editorExpanded) ||
           !JsonUtils::MutObjAddString(doc, effectObj, "effectAssetRef", effect.effectAssetRef)){
            setComponentSerializationError(outError, "Failed to serialize post-processing effect metadata.");
            return false;
        }

        switch(effect.kind){
            case PostProcessingEffectKind::DepthOfField:
                if(!JsonUtils::MutObjAddBool(doc, effectObj, "adaptiveFocus", effect.depthOfField.adaptiveFocus) ||
                   !JsonUtils::MutObjAddBool(doc, effectObj, "adaptiveFocusDebugDraw", effect.depthOfField.adaptiveFocusDebugDraw) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "focusDistance", effect.depthOfField.focusDistance) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "focusRange", effect.depthOfField.focusRange) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "focusBandWidth", effect.depthOfField.focusBandWidth) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "blurRamp", effect.depthOfField.blurRamp) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "blurDistanceLerp", effect.depthOfField.blurDistanceLerp) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "fallbackFocusRange", effect.depthOfField.fallbackFocusRange) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "blurStrength", effect.depthOfField.blurStrength) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "maxBlurPx", effect.depthOfField.maxBlurPx) ||
                   !JsonUtils::MutObjAddInt(doc, effectObj, "sampleCount", effect.depthOfField.sampleCount) ||
                   !JsonUtils::MutObjAddBool(doc, effectObj, "debugCocView", effect.depthOfField.debugCocView)){
                    setComponentSerializationError(outError, "Failed to serialize depth-of-field stack settings.");
                    return false;
                }
                break;
            case PostProcessingEffectKind::Bloom:
                if(!JsonUtils::MutObjAddBool(doc, effectObj, "adaptiveBloom", effect.bloom.adaptiveBloom) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "threshold", effect.bloom.threshold) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "softKnee", effect.bloom.softKnee) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "intensity", effect.bloom.intensity) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "radiusPx", effect.bloom.radiusPx) ||
                   !JsonUtils::MutObjAddInt(doc, effectObj, "sampleCount", effect.bloom.sampleCount) ||
                   !JsonUtils::MutObjAddVec3(doc, effectObj, "tint", effect.bloom.tint)){
                    setComponentSerializationError(outError, "Failed to serialize bloom stack settings.");
                    return false;
                }
                break;
            case PostProcessingEffectKind::AutoExposure:
                if(!JsonUtils::MutObjAddFloat(doc, effectObj, "minExposure", effect.autoExposure.minExposure) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "maxExposure", effect.autoExposure.maxExposure) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "exposureCompensation", effect.autoExposure.exposureCompensation) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "adaptationSpeedUp", effect.autoExposure.adaptationSpeedUp) ||
                   !JsonUtils::MutObjAddFloat(doc, effectObj, "adaptationSpeedDown", effect.autoExposure.adaptationSpeedDown)){
                    setComponentSerializationError(outError, "Failed to serialize auto-exposure stack settings.");
                    return false;
                }
                break;
            case PostProcessingEffectKind::AntiAliasing:
                if(!JsonUtils::MutObjAddInt(doc, effectObj, "preset", static_cast<int>(effect.antiAliasing.preset))){
                    setComponentSerializationError(outError, "Failed to serialize anti-aliasing stack settings.");
                    return false;
                }
                break;
            case PostProcessingEffectKind::Loaded:{
                if(!addStringArrayField(doc, effectObj, "requiredEffects", effect.loadedRequiredEffects, outError)){
                    return false;
                }

                JsonUtils::JsonMutVal* propertiesArr = yyjson_mut_obj_add_arr(doc, effectObj, "properties");
                if(!propertiesArr){
                    setComponentSerializationError(outError, "Failed to create loaded-effect properties array.");
                    return false;
                }
                for(const EffectPropertyData& property : effect.loadedProperties){
                    JsonUtils::JsonMutVal* propertyObj = yyjson_mut_arr_add_obj(doc, propertiesArr);
                    if(!propertyObj){
                        setComponentSerializationError(outError, "Failed to append loaded-effect property.");
                        return false;
                    }
                    if(!writeEffectPropertyField(property, doc, propertyObj, outError)){
                        return false;
                    }
                }
                break;
            }
            case PostProcessingEffectKind::LensFlare:
            default:
                break;
        }
    }

    return true;
}

bool readPostProcessingStackEffectsField(JsonUtils::JsonVal* payload,
                                         PostProcessingStackComponent& component,
                                         std::string* outError){
    component.effects.clear();
    JsonUtils::JsonVal* effectsArr = JsonUtils::ObjGetArray(payload, "effects");
    if(!effectsArr){
        return true;
    }

    const size_t effectCount = yyjson_arr_size(effectsArr);
    component.effects.reserve(effectCount);
    for(size_t i = 0; i < effectCount; ++i){
        JsonUtils::JsonVal* effectObj = yyjson_arr_get(effectsArr, i);
        if(!effectObj || !yyjson_is_obj(effectObj)){
            setComponentSerializationError(outError, "Post-processing effect entry must be an object.");
            return false;
        }

        PostProcessingEffectEntry effect;
        int kindValue = static_cast<int>(effect.kind);
        JsonUtils::TryGetInt(effectObj, "kind", kindValue);
        effect.kind = enumFromIntClamped(kindValue, 0, 5, PostProcessingEffectKind::Bloom);
        JsonUtils::TryGetBool(effectObj, "enabled", effect.enabled);
        effect.hidden = false;
        JsonUtils::TryGetBool(effectObj, "editorExpanded", effect.editorExpanded);
        JsonUtils::TryGetString(effectObj, "effectAssetRef", effect.effectAssetRef);

        switch(effect.kind){
            case PostProcessingEffectKind::DepthOfField:
                JsonUtils::TryGetBool(effectObj, "adaptiveFocus", effect.depthOfField.adaptiveFocus);
                JsonUtils::TryGetBool(effectObj, "adaptiveFocusDebugDraw", effect.depthOfField.adaptiveFocusDebugDraw);
                JsonUtils::TryGetFloat(effectObj, "focusDistance", effect.depthOfField.focusDistance);
                JsonUtils::TryGetFloat(effectObj, "focusRange", effect.depthOfField.focusRange);
                JsonUtils::TryGetFloat(effectObj, "focusBandWidth", effect.depthOfField.focusBandWidth);
                JsonUtils::TryGetFloat(effectObj, "blurRamp", effect.depthOfField.blurRamp);
                JsonUtils::TryGetFloat(effectObj, "blurDistanceLerp", effect.depthOfField.blurDistanceLerp);
                JsonUtils::TryGetFloat(effectObj, "fallbackFocusRange", effect.depthOfField.fallbackFocusRange);
                JsonUtils::TryGetFloat(effectObj, "blurStrength", effect.depthOfField.blurStrength);
                JsonUtils::TryGetFloat(effectObj, "maxBlurPx", effect.depthOfField.maxBlurPx);
                JsonUtils::TryGetInt(effectObj, "sampleCount", effect.depthOfField.sampleCount);
                JsonUtils::TryGetBool(effectObj, "debugCocView", effect.depthOfField.debugCocView);
                effect.depthOfField.runtimeEffect.reset();
                break;
            case PostProcessingEffectKind::Bloom:
                JsonUtils::TryGetBool(effectObj, "adaptiveBloom", effect.bloom.adaptiveBloom);
                JsonUtils::TryGetFloat(effectObj, "threshold", effect.bloom.threshold);
                JsonUtils::TryGetFloat(effectObj, "softKnee", effect.bloom.softKnee);
                JsonUtils::TryGetFloat(effectObj, "intensity", effect.bloom.intensity);
                JsonUtils::TryGetFloat(effectObj, "radiusPx", effect.bloom.radiusPx);
                JsonUtils::TryGetInt(effectObj, "sampleCount", effect.bloom.sampleCount);
                JsonUtils::TryGetVec3(effectObj, "tint", effect.bloom.tint);
                effect.bloom.runtimeEffect.reset();
                effect.bloom.liveThreshold = effect.bloom.threshold;
                effect.bloom.liveIntensity = effect.bloom.intensity;
                effect.bloom.liveAutoExposureDriven = false;
                break;
            case PostProcessingEffectKind::AutoExposure:
                JsonUtils::TryGetFloat(effectObj, "minExposure", effect.autoExposure.minExposure);
                JsonUtils::TryGetFloat(effectObj, "maxExposure", effect.autoExposure.maxExposure);
                JsonUtils::TryGetFloat(effectObj, "exposureCompensation", effect.autoExposure.exposureCompensation);
                JsonUtils::TryGetFloat(effectObj, "adaptationSpeedUp", effect.autoExposure.adaptationSpeedUp);
                JsonUtils::TryGetFloat(effectObj, "adaptationSpeedDown", effect.autoExposure.adaptationSpeedDown);
                effect.autoExposure.runtimeEffect.reset();
                break;
            case PostProcessingEffectKind::AntiAliasing:{
                int presetValue = static_cast<int>(effect.antiAliasing.preset);
                JsonUtils::TryGetInt(effectObj, "preset", presetValue);
                effect.antiAliasing.preset = enumFromIntClamped(presetValue, 0, 3, AntiAliasingPreset::FXAA_Medium);
                effect.antiAliasing.runtimeEffect.reset();
                break;
            }
            case PostProcessingEffectKind::Loaded:{
                if(!readStringArrayField(effectObj, "requiredEffects", effect.loadedRequiredEffects, outError)){
                    return false;
                }

                JsonUtils::JsonVal* propertiesArr = JsonUtils::ObjGetArray(effectObj, "properties");
                if(propertiesArr){
                    const size_t propertyCount = yyjson_arr_size(propertiesArr);
                    effect.loadedProperties.reserve(propertyCount);
                    for(size_t propertyIndex = 0; propertyIndex < propertyCount; ++propertyIndex){
                        JsonUtils::JsonVal* propertyObj = yyjson_arr_get(propertiesArr, propertyIndex);
                        EffectPropertyData property;
                        if(!readEffectPropertyField(propertyObj, property, outError)){
                            return false;
                        }
                        effect.loadedProperties.push_back(std::move(property));
                    }
                }
                effect.loadedInputs.clear();
                effect.loadedAssetRevision = 0;
                effect.runtimeLoadedEffect.reset();
                break;
            }
            case PostProcessingEffectKind::LensFlare:
            default:
                effect.lensFlare.runtimeEffect.reset();
                break;
        }

        component.effects.push_back(std::move(effect));
    }

    return true;
}

bool writeEditorComponentStateFields(const IEditorCompatibleComponent& component,
                                     yyjson_mut_doc* doc,
                                     JsonUtils::JsonMutVal* payload,
                                     std::string* outError){
    const bool enabled = IsComponentActive(&component);
    const bool* hiddenState = component.getEditorHiddenState();
    const bool hidden = hiddenState ? *hiddenState : false;
    if(!JsonUtils::MutObjAddBool(doc, payload, "componentEnabled", enabled) ||
       !JsonUtils::MutObjAddBool(doc, payload, "componentHidden", hidden)){
        setComponentSerializationError(outError, "Failed to serialize component editor state.");
        return false;
    }
    return true;
}

void readEditorComponentStateFields(IEditorCompatibleComponent& component, JsonUtils::JsonVal* payload){
    if(bool* enabledState = component.getEditorEnabledState()){
        JsonUtils::TryGetBool(payload, "componentEnabled", *enabledState);
    }
    if(bool* hiddenState = component.getEditorHiddenState()){
        JsonUtils::TryGetBool(payload, "componentHidden", *hiddenState);
    }
}

void sanitizeEnvironmentSettings(EnvironmentSettings& settings){
    settings.fogStart = Math3D::Max(0.0f, settings.fogStart);
    settings.fogStop = Math3D::Max(settings.fogStart, settings.fogStop);
    settings.fogEnd = Math3D::Max(settings.fogStop, settings.fogEnd);
    settings.ambientIntensity = Math3D::Clamp(settings.ambientIntensity, 0.0f, 32.0f);
    settings.rayleighStrength = Math3D::Max(0.0f, settings.rayleighStrength);
    settings.mieStrength = Math3D::Max(0.0f, settings.mieStrength);
    settings.mieAnisotropy = Math3D::Clamp(settings.mieAnisotropy, 0.0f, 0.99f);
    if(settings.sunDirection.length() <= Math3D::EPSILON){
        settings.sunDirection = Math3D::Vec3(0.0f, -1.0f, 0.0f);
    }else{
        settings.sunDirection = settings.sunDirection.normalize();
    }
}

bool writeTransformFields(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* obj, const char* keyPrefix, const Math3D::Transform& transform){
    (void)keyPrefix;
    Math3D::Vec3 rotationEuler = transform.rotation.ToEuler();
    return JsonUtils::MutObjAddVec3(doc, obj, "position", transform.position) &&
           JsonUtils::MutObjAddVec3(doc, obj, "rotationEuler", rotationEuler) &&
           JsonUtils::MutObjAddVec3(doc, obj, "scale", transform.scale);
}

void readTransformFields(JsonUtils::JsonVal* obj, Math3D::Transform& transform){
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
                        JsonUtils::JsonMutVal* obj,
                        const char* key,
                        const std::vector<float>& values,
                        std::string* outError){
    JsonUtils::JsonMutVal* arr = yyjson_mut_obj_add_arr(doc, obj, key);
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
                       JsonUtils::JsonMutVal* obj,
                       const char* key,
                       const std::vector<std::uint32_t>& values,
                       std::string* outError){
    JsonUtils::JsonMutVal* arr = yyjson_mut_obj_add_arr(doc, obj, key);
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

bool readFloatArrayField(JsonUtils::JsonVal* obj,
                         const char* key,
                         std::vector<float>& outValues,
                         std::string* outError){
    outValues.clear();
    JsonUtils::JsonVal* arr = JsonUtils::ObjGetArray(obj, key);
    if(!arr){
        return true;
    }

    const size_t count = yyjson_arr_size(arr);
    outValues.reserve(count);
    for(size_t i = 0; i < count; ++i){
        JsonUtils::JsonVal* item = yyjson_arr_get(arr, i);
        if(!item || !yyjson_is_num(item)){
            setComponentSerializationError(outError, std::string("Field '") + key + "' must be a number array.");
            return false;
        }
        outValues.push_back(static_cast<float>(yyjson_get_num(item)));
    }
    return true;
}

bool readUIntArrayField(JsonUtils::JsonVal* obj,
                        const char* key,
                        std::vector<std::uint32_t>& outValues,
                        std::string* outError){
    outValues.clear();
    JsonUtils::JsonVal* arr = JsonUtils::ObjGetArray(obj, key);
    if(!arr){
        return true;
    }

    const size_t count = yyjson_arr_size(arr);
    outValues.reserve(count);
    for(size_t i = 0; i < count; ++i){
        JsonUtils::JsonVal* item = yyjson_arr_get(arr, i);
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
                                    JsonUtils::JsonMutVal* partObj,
                                    std::string* outError){
    JsonUtils::JsonMutVal* materialObj = yyjson_mut_obj_add_obj(doc, partObj, "embeddedMaterial");
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

bool tryReadEmbeddedMaterialDataField(JsonUtils::JsonVal* partObj, MaterialAssetData& outData){
    JsonUtils::JsonVal* materialObj = JsonUtils::ObjGetObject(partObj, "embeddedMaterial");
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

bool writeModelPartEmbeddedMaterialOverridesField(const MeshRendererComponent& component,
                                                  yyjson_mut_doc* doc,
                                                  JsonUtils::JsonMutVal* payload,
                                                  std::string* outError){
    if(!component.model){
        return true;
    }

    const auto& parts = component.model->getParts();
    bool hasAnyEmbeddedOverride = false;
    const size_t maxCount = std::min(parts.size(), component.modelPartMaterialOverrides.size());
    for(size_t i = 0; i < maxCount; ++i){
        if(component.modelPartMaterialOverrides[i] == 0){
            continue;
        }
        const auto& part = parts[i];
        MaterialAssetData embeddedMaterial;
        if(captureEmbeddedMaterialData(part ? part->material : nullptr, embeddedMaterial)){
            hasAnyEmbeddedOverride = true;
            break;
        }
    }
    if(!hasAnyEmbeddedOverride){
        return true;
    }

    JsonUtils::JsonMutVal* overridesArr = yyjson_mut_obj_add_arr(doc, payload, "modelPartEmbeddedMaterials");
    if(!overridesArr){
        setComponentSerializationError(outError, "Failed to create modelPartEmbeddedMaterials array.");
        return false;
    }

    for(size_t i = 0; i < parts.size(); ++i){
        const auto& part = parts[i];
        JsonUtils::JsonMutVal* partOverrideObj = yyjson_mut_arr_add_obj(doc, overridesArr);
        if(!partOverrideObj){
            setComponentSerializationError(outError, "Failed to append model part material override.");
            return false;
        }

        const bool shouldWriteOverride =
            (i < component.modelPartMaterialOverrides.size()) &&
            (component.modelPartMaterialOverrides[i] != 0);
        MaterialAssetData embeddedMaterial;
        if(shouldWriteOverride &&
           captureEmbeddedMaterialData(part ? part->material : nullptr, embeddedMaterial) &&
           !writeEmbeddedMaterialDataField(embeddedMaterial, doc, partOverrideObj, outError)){
            return false;
        }
    }

    return true;
}

void readModelPartEmbeddedMaterialOverridesField(JsonUtils::JsonVal* payload, MeshRendererComponent& component){
    if(!component.model){
        return;
    }

    const auto& parts = component.model->getParts();
    component.modelPartMaterialOverrides.assign(parts.size(), 0);

    JsonUtils::JsonVal* overridesArr = JsonUtils::ObjGetArray(payload, "modelPartEmbeddedMaterials");
    if(!overridesArr){
        return;
    }

    const size_t maxCount = std::min(parts.size(), yyjson_arr_size(overridesArr));
    for(size_t i = 0; i < maxCount; ++i){
        const auto& part = parts[i];
        if(!part){
            continue;
        }

        JsonUtils::JsonVal* partOverrideObj = yyjson_arr_get(overridesArr, i);
        if(!partOverrideObj || !yyjson_is_obj(partOverrideObj)){
            continue;
        }

        MaterialAssetData embeddedMaterial;
        if(!tryReadEmbeddedMaterialDataField(partOverrideObj, embeddedMaterial)){
            continue;
        }

        if(auto overrideMaterial = MaterialAssetIO::InstantiateMaterial(embeddedMaterial, nullptr)){
            part->material = overrideMaterial;
            component.modelPartMaterialOverrides[i] = 1;
        }
    }
}

bool writeEmbeddedModelField(const MeshRendererComponent& component,
                             yyjson_mut_doc* doc,
                             JsonUtils::JsonMutVal* payload,
                             std::string* outError){
    if(!component.model){
        return true;
    }

    JsonUtils::JsonMutVal* embeddedModelObj = yyjson_mut_obj_add_obj(doc, payload, "embeddedModel");
    if(!embeddedModelObj){
        setComponentSerializationError(outError, "Failed to create embeddedModel object.");
        return false;
    }

    if(!JsonUtils::MutObjAddBool(doc, embeddedModelObj, "enableBackfaceCulling", component.model->isBackfaceCullingEnabled())){
        setComponentSerializationError(outError, "Failed to write embeddedModel backface culling flag.");
        return false;
    }

    JsonUtils::JsonMutVal* partsArr = yyjson_mut_obj_add_arr(doc, embeddedModelObj, "parts");
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

        JsonUtils::JsonMutVal* partObj = yyjson_mut_arr_add_obj(doc, partsArr);
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

bool tryReadEmbeddedModelField(JsonUtils::JsonVal* payload,
                               MeshRendererComponent& component,
                               std::string* outError){
    JsonUtils::JsonVal* embeddedModelObj = JsonUtils::ObjGetObject(payload, "embeddedModel");
    if(!embeddedModelObj){
        return true;
    }

    JsonUtils::JsonVal* partsArr = JsonUtils::ObjGetArray(embeddedModelObj, "parts");
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
    component.modelPartMaterialOverrides.assign(partCount, 0);
    for(size_t i = 0; i < partCount; ++i){
        JsonUtils::JsonVal* partObj = yyjson_arr_get(partsArr, i);
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
        [](const TransformComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            Math3D::Vec3 rotationEuler = component.local.rotation.ToEuler();
            return JsonUtils::MutObjAddVec3(doc, payload, "position", component.local.position) &&
                   JsonUtils::MutObjAddVec3(doc, payload, "rotationEuler", rotationEuler) &&
                   JsonUtils::MutObjAddVec3(doc, payload, "scale", component.local.scale) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](TransformComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
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
            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultEntityPropertiesSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<EntityPropertiesComponent>(
        "EntityPropertiesComponent",
        1,
        [](const EntityPropertiesComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddBool(doc, payload, "ignoreRaycastHit", component.ignoreRaycastHit) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](EntityPropertiesComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            JsonUtils::TryGetBool(payload, "ignoreRaycastHit", component.ignoreRaycastHit);
            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultBoundsSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<BoundsComponent>(
        "BoundsComponent",
        2,
        [](const BoundsComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddInt(doc, payload, "type", static_cast<int>(component.type)) &&
                   JsonUtils::MutObjAddVec3(doc, payload, "size", component.size) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "radius", component.radius) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "height", component.height) &&
                   JsonUtils::MutObjAddVec3(doc, payload, "offset", component.offset) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](BoundsComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)error;

            int type = static_cast<int>(component.type);
            Math3D::Vec3 size = component.size;
            float radius = component.radius;
            float height = component.height;
            Math3D::Vec3 offset = Math3D::Vec3(0.0f, 0.0f, 0.0f);

            JsonUtils::TryGetInt(payload, "type", type);
            JsonUtils::TryGetVec3(payload, "size", size);
            JsonUtils::TryGetFloat(payload, "radius", radius);
            JsonUtils::TryGetFloat(payload, "height", height);
            if(version >= 2){
                JsonUtils::TryGetVec3(payload, "offset", offset);
            }

            component.type = enumFromIntClamped(type, 0, 2, BoundsType::Sphere);
            component.size = size;
            component.radius = Math3D::Max(0.0f, radius);
            component.height = Math3D::Max(0.0f, height);
            component.offset = offset;
            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultMeshRendererSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<MeshRendererComponent>(
        "MeshRendererComponent",
        3,
        [](const MeshRendererComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
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

            JsonUtils::JsonMutVal* offsetObj = yyjson_mut_obj_add_obj(doc, payload, "localOffset");
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
            const bool shouldWriteSingleEmbeddedMaterial = component.material &&
                (component.materialAssetRef.empty() || component.materialOverridesSource);
            if(shouldWriteSingleEmbeddedMaterial){
                MaterialAssetData embeddedMaterial;
                if(captureEmbeddedMaterialData(component.material, embeddedMaterial) &&
                   !writeEmbeddedMaterialDataField(embeddedMaterial, doc, payload, error)){
                    return false;
                }
            }
            if(component.model &&
               !shouldEmbedModel &&
               !writeModelPartEmbeddedMaterialOverridesField(component, doc, payload, error)){
                return false;
            }
            if(shouldEmbedModel && !writeEmbeddedModelField(component, doc, payload, error)){
                return false;
            }
            if(!writeEditorComponentStateFields(component, doc, payload, error)){
                return false;
            }
            return true;
        },
        [](MeshRendererComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            JsonUtils::TryGetBool(payload, "visible", component.visible);
            JsonUtils::TryGetBool(payload, "enableBackfaceCulling", component.enableBackfaceCulling);
            JsonUtils::TryGetString(payload, "modelAssetRef", component.modelAssetRef);
            JsonUtils::TryGetString(payload, "modelSourceRef", component.modelSourceRef);
            bool modelForceSmoothNormals = (component.modelForceSmoothNormals != 0);
            JsonUtils::TryGetBool(payload, "modelForceSmoothNormals", modelForceSmoothNormals);
            component.modelForceSmoothNormals = modelForceSmoothNormals ? 1 : 0;
            JsonUtils::TryGetString(payload, "materialAssetRef", component.materialAssetRef);

            if(JsonUtils::JsonVal* offsetObj = JsonUtils::ObjGetObject(payload, "localOffset")){
                readTransformFields(offsetObj, component.localOffset);
            }
            readStringArrayField(payload, "modelPartMaterialAssetRefs", component.modelPartMaterialAssetRefs, nullptr);
            component.materialOverridesSource = false;
            component.modelPartMaterialOverrides.clear();

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
                component.modelPartMaterialOverrides.assign(parts.size(), 0);
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
                readModelPartEmbeddedMaterialOverridesField(payload, component);
            }

            MaterialAssetData embeddedMaterial;
            const bool hasSingleEmbeddedMaterial = tryReadEmbeddedMaterialDataField(payload, embeddedMaterial);
            if(hasSingleEmbeddedMaterial){
                component.material = MaterialAssetIO::InstantiateMaterial(embeddedMaterial, nullptr);
                component.materialOverridesSource = !component.materialAssetRef.empty();
            }
            if(!component.material && !component.materialAssetRef.empty()){
                component.material =
                    MaterialAssetIO::InstantiateMaterialFromRef(component.materialAssetRef, nullptr, nullptr);
            }

            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultLightSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<LightComponent>(
        "LightComponent",
        2,
        [](const LightComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            JsonUtils::JsonMutVal* lightObj = yyjson_mut_obj_add_obj(doc, payload, "light");
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
                   JsonUtils::MutObjAddFloat(doc, lightObj, "cascadeLambda", component.light.cascadeLambda) &&
                   JsonUtils::MutObjAddFloat(doc, lightObj, "shadowStrength", component.light.shadowStrength) &&
                   JsonUtils::MutObjAddInt(doc, lightObj, "shadowDebugMode", component.light.shadowDebugMode) &&
                   JsonUtils::MutObjAddString(doc, payload, "flareAssetRef", component.flareAssetRef) &&
                   JsonUtils::MutObjAddBool(doc, payload, "syncTransform", component.syncTransform) &&
                   JsonUtils::MutObjAddBool(doc, payload, "syncDirection", component.syncDirection) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](LightComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;

            JsonUtils::JsonVal* lightObj = JsonUtils::ObjGetObject(payload, "light");
            if(lightObj){
                int type = static_cast<int>(component.light.type);
                int shadowType = static_cast<int>(component.light.shadowType);
                int shadowDebugMode = component.light.shadowDebugMode;
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
                JsonUtils::TryGetFloat(lightObj, "cascadeLambda", component.light.cascadeLambda);
                JsonUtils::TryGetFloat(lightObj, "shadowStrength", component.light.shadowStrength);
                JsonUtils::TryGetInt(lightObj, "shadowDebugMode", shadowDebugMode);

                component.light.type = enumFromIntClamped(type, 0, 2, LightType::POINT);
                component.light.shadowType = enumFromIntClamped(shadowType, 0, 2, ShadowType::Smooth);
                component.light.shadowDebugMode = Math3D::Clamp(shadowDebugMode, 0, 3);
                if(!std::isfinite(component.light.cascadeLambda)){
                    component.light.cascadeLambda = 0.82f;
                }
                component.light.cascadeLambda = Math3D::Clamp(component.light.cascadeLambda, 0.0f, 1.0f);
                if(direction.length() > Math3D::EPSILON){
                    component.light.direction = direction.normalize();
                }
            }

            JsonUtils::TryGetString(payload, "flareAssetRef", component.flareAssetRef);
            JsonUtils::TryGetBool(payload, "syncTransform", component.syncTransform);
            JsonUtils::TryGetBool(payload, "syncDirection", component.syncDirection);
            readEditorComponentStateFields(component, payload);
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
        [](const CameraComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            if(!JsonUtils::MutObjAddBool(doc, payload, "hasCamera", component.camera != nullptr)){
                setComponentSerializationError(error, "Failed to write hasCamera field.");
                return false;
            }
            if(!writeEditorComponentStateFields(component, doc, payload, error)){
                return false;
            }
            if(!component.camera){
                return true;
            }

            const CameraSettings& settings = component.camera->getSettings();
            JsonUtils::JsonMutVal* cameraObj = yyjson_mut_obj_add_obj(doc, payload, "camera");
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
        [](CameraComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            bool hasCamera = (component.camera != nullptr);
            JsonUtils::TryGetBool(payload, "hasCamera", hasCamera);
            if(!hasCamera){
                component.camera.reset();
                readEditorComponentStateFields(component, payload);
                return true;
            }

            if(!component.camera){
                component.camera = Camera::CreatePerspective(45.0f, Math3D::Vec2(1280.0f, 720.0f), 0.1f, 1000.0f);
            }
            if(!component.camera){
                setComponentSerializationError(error, "Failed to allocate camera while deserializing camera component.");
                return false;
            }

            if(JsonUtils::JsonVal* cameraObj = JsonUtils::ObjGetObject(payload, "camera")){
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

            readEditorComponentStateFields(component, payload);
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
        [](const SkyboxComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddString(doc, payload, "skyboxAssetRef", StringUtils::Trim(component.skyboxAssetRef)) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](SkyboxComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            JsonUtils::TryGetString(payload, "skyboxAssetRef", component.skyboxAssetRef);
            component.skyboxAssetRef = StringUtils::Trim(component.skyboxAssetRef);
            component.loadedSkyboxAssetRef.clear();
            component.runtimeSkyBox.reset();
            readEditorComponentStateFields(component, payload);
            return true;
        },
        [](NeoECS::GameObject* wrapper, std::string* error) -> bool {
            if(!wrapper){
                if(error){
                    *error = "Null GameObject wrapper while ensuring SkyboxComponent.";
                }
                return false;
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

bool registerDefaultEnvironmentSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<EnvironmentComponent>(
        "EnvironmentComponent",
        1,
        [](const EnvironmentComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            if(!JsonUtils::MutObjAddString(doc, payload, "environmentAssetRef", StringUtils::Trim(component.environmentAssetRef)) ||
               !JsonUtils::MutObjAddString(doc, payload, "skyboxAssetRef", StringUtils::Trim(component.skyboxAssetRef))){
                setComponentSerializationError(error, "Failed to serialize environment component asset references.");
                return false;
            }

            JsonUtils::JsonMutVal* settingsObj = yyjson_mut_obj_add_obj(doc, payload, "settings");
            if(!settingsObj){
                setComponentSerializationError(error, "Failed to create environment settings payload object.");
                return false;
            }

            EnvironmentSettings settings = component.settings;
            sanitizeEnvironmentSettings(settings);
            if(!JsonUtils::MutObjAddBool(doc, settingsObj, "fogEnabled", settings.fogEnabled) ||
               !JsonUtils::MutObjAddVec4(doc, settingsObj, "fogColor", settings.fogColor) ||
               !JsonUtils::MutObjAddFloat(doc, settingsObj, "fogStart", settings.fogStart) ||
               !JsonUtils::MutObjAddFloat(doc, settingsObj, "fogStop", settings.fogStop) ||
               !JsonUtils::MutObjAddFloat(doc, settingsObj, "fogEnd", settings.fogEnd) ||
               !JsonUtils::MutObjAddVec4(doc, settingsObj, "ambientColor", settings.ambientColor) ||
               !JsonUtils::MutObjAddFloat(doc, settingsObj, "ambientIntensity", settings.ambientIntensity) ||
               !JsonUtils::MutObjAddBool(doc, settingsObj, "useProceduralSky", settings.useProceduralSky) ||
               !JsonUtils::MutObjAddVec3(doc, settingsObj, "sunDirection", settings.sunDirection) ||
               !JsonUtils::MutObjAddFloat(doc, settingsObj, "rayleighStrength", settings.rayleighStrength) ||
               !JsonUtils::MutObjAddFloat(doc, settingsObj, "mieStrength", settings.mieStrength) ||
               !JsonUtils::MutObjAddFloat(doc, settingsObj, "mieAnisotropy", settings.mieAnisotropy)){
                setComponentSerializationError(error, "Failed to serialize environment settings.");
                return false;
            }

            return writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](EnvironmentComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;

            JsonUtils::TryGetString(payload, "environmentAssetRef", component.environmentAssetRef);
            JsonUtils::TryGetString(payload, "skyboxAssetRef", component.skyboxAssetRef);
            component.environmentAssetRef = StringUtils::Trim(component.environmentAssetRef);
            component.skyboxAssetRef = StringUtils::Trim(component.skyboxAssetRef);

            if(JsonUtils::JsonVal* settingsObj = JsonUtils::ObjGetObject(payload, "settings")){
                JsonUtils::TryGetBool(settingsObj, "fogEnabled", component.settings.fogEnabled);
                JsonUtils::TryGetVec4(settingsObj, "fogColor", component.settings.fogColor);
                JsonUtils::TryGetFloat(settingsObj, "fogStart", component.settings.fogStart);
                JsonUtils::TryGetFloat(settingsObj, "fogStop", component.settings.fogStop);
                JsonUtils::TryGetFloat(settingsObj, "fogEnd", component.settings.fogEnd);
                JsonUtils::TryGetVec4(settingsObj, "ambientColor", component.settings.ambientColor);
                JsonUtils::TryGetFloat(settingsObj, "ambientIntensity", component.settings.ambientIntensity);
                JsonUtils::TryGetBool(settingsObj, "useProceduralSky", component.settings.useProceduralSky);
                JsonUtils::TryGetVec3(settingsObj, "sunDirection", component.settings.sunDirection);
                JsonUtils::TryGetFloat(settingsObj, "rayleighStrength", component.settings.rayleighStrength);
                JsonUtils::TryGetFloat(settingsObj, "mieStrength", component.settings.mieStrength);
                JsonUtils::TryGetFloat(settingsObj, "mieAnisotropy", component.settings.mieAnisotropy);
            }

            sanitizeEnvironmentSettings(component.settings);
            component.loadedEnvironmentAssetRef.clear();
            component.loadedSkyboxAssetRef.clear();
            component.runtimeSkyBox.reset();
            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultColliderSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<ColliderComponent>(
        "ColliderComponent",
        1,
        [](const ColliderComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
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

            JsonUtils::JsonMutVal* offsetObj = yyjson_mut_obj_add_obj(doc, payload, "localOffset");
            if(!offsetObj || !writeTransformFields(doc, offsetObj, "", component.localOffset)){
                setComponentSerializationError(error, "Failed to write collider local offset.");
                return false;
            }

            JsonUtils::JsonMutVal* materialObj = yyjson_mut_obj_add_obj(doc, payload, "material");
            if(!materialObj){
                setComponentSerializationError(error, "Failed to write collider material object.");
                return false;
            }
            return JsonUtils::MutObjAddFloat(doc, materialObj, "staticFriction", component.material.staticFriction) &&
                   JsonUtils::MutObjAddFloat(doc, materialObj, "dynamicFriction", component.material.dynamicFriction) &&
                   JsonUtils::MutObjAddFloat(doc, materialObj, "restitution", component.material.restitution) &&
                   JsonUtils::MutObjAddFloat(doc, materialObj, "density", component.material.density) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](ColliderComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
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

            if(JsonUtils::JsonVal* offsetObj = JsonUtils::ObjGetObject(payload, "localOffset")){
                readTransformFields(offsetObj, component.localOffset);
            }
            if(JsonUtils::JsonVal* materialObj = JsonUtils::ObjGetObject(payload, "material")){
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
            readEditorComponentStateFields(component, payload);
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
        [](const RigidBodyComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
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
                   JsonUtils::MutObjAddBool(doc, payload, "startAwake", component.startAwake) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](RigidBodyComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
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
            readEditorComponentStateFields(component, payload);
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
        [](const ScriptComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return addStringArrayField(doc, payload, "scriptAssetRefs", component.scriptAssetRefs, error) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](ScriptComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            std::vector<std::string> refs;
            if(!readStringArrayField(payload, "scriptAssetRefs", refs, error)){
                return false;
            }

            component.scriptAssetRefs.clear();
            for(const std::string& ref : refs){
                component.addScriptAsset(ref);
            }
            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultSsaoSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<SSAOComponent>(
        "SSAOComponent",
        3,
        [](const SSAOComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddBool(doc, payload, "enabled", component.enabled) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "radiusPx", component.radiusPx) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "depthRadius", component.depthRadius) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "bias", component.bias) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "intensity", component.intensity) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "giBoost", component.giBoost) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "blurRadiusPx", component.blurRadiusPx) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "blurSharpness", component.blurSharpness) &&
                   JsonUtils::MutObjAddInt(doc, payload, "sampleCount", component.sampleCount) &&
                   JsonUtils::MutObjAddInt(doc, payload, "debugView", component.debugView) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](SSAOComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)error;
            JsonUtils::TryGetBool(payload, "enabled", component.enabled);
            JsonUtils::TryGetFloat(payload, "radiusPx", component.radiusPx);
            JsonUtils::TryGetFloat(payload, "depthRadius", component.depthRadius);
            JsonUtils::TryGetFloat(payload, "bias", component.bias);
            JsonUtils::TryGetFloat(payload, "intensity", component.intensity);
            JsonUtils::TryGetFloat(payload, "giBoost", component.giBoost);
            if(version >= 3){
                JsonUtils::TryGetFloat(payload, "blurRadiusPx", component.blurRadiusPx);
                JsonUtils::TryGetFloat(payload, "blurSharpness", component.blurSharpness);
            }
            JsonUtils::TryGetInt(payload, "sampleCount", component.sampleCount);
            JsonUtils::TryGetInt(payload, "debugView", component.debugView);
            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultPostProcessingStackSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<PostProcessingStackComponent>(
        "PostProcessingStackComponent",
        1,
        [](const PostProcessingStackComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return writePostProcessingStackEffectsField(component, doc, payload, error) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](PostProcessingStackComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            if(!readPostProcessingStackEffectsField(payload, component, error)){
                return false;
            }
            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultDepthOfFieldSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<DepthOfFieldComponent>(
        "DepthOfFieldComponent",
        6,
        [](const DepthOfFieldComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddBool(doc, payload, "enabled", component.enabled) &&
                   JsonUtils::MutObjAddBool(doc, payload, "adaptiveFocus", component.adaptiveFocus) &&
                   JsonUtils::MutObjAddBool(doc, payload, "adaptiveFocusDebugDraw", component.adaptiveFocusDebugDraw) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "focusDistance", component.focusDistance) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "focusRange", component.focusRange) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "focusBandWidth", component.focusBandWidth) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "blurRamp", component.blurRamp) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "blurDistanceLerp", component.blurDistanceLerp) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "fallbackFocusRange", component.fallbackFocusRange) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "blurStrength", component.blurStrength) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "maxBlurPx", component.maxBlurPx) &&
                   JsonUtils::MutObjAddInt(doc, payload, "sampleCount", component.sampleCount) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](DepthOfFieldComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)error;
            JsonUtils::TryGetBool(payload, "enabled", component.enabled);
            component.adaptiveFocus = false;
            JsonUtils::TryGetBool(payload, "adaptiveFocus", component.adaptiveFocus);
            component.adaptiveFocusDebugDraw = false;
            if(version >= 4){
                JsonUtils::TryGetBool(payload, "adaptiveFocusDebugDraw", component.adaptiveFocusDebugDraw);
            }
            JsonUtils::TryGetFloat(payload, "focusDistance", component.focusDistance);
            JsonUtils::TryGetFloat(payload, "focusRange", component.focusRange);
            component.focusBandWidth = 0.85f;
            component.blurRamp = 2.0f;
            component.blurDistanceLerp = 0.35f;
            if(version >= 5){
                JsonUtils::TryGetFloat(payload, "focusBandWidth", component.focusBandWidth);
                JsonUtils::TryGetFloat(payload, "blurRamp", component.blurRamp);
            }
            if(version >= 6){
                JsonUtils::TryGetFloat(payload, "blurDistanceLerp", component.blurDistanceLerp);
            }
            if(version >= 3){
                JsonUtils::TryGetFloat(payload, "fallbackFocusRange", component.fallbackFocusRange);
            }else{
                component.fallbackFocusRange = component.focusRange;
            }
            JsonUtils::TryGetFloat(payload, "blurStrength", component.blurStrength);
            JsonUtils::TryGetFloat(payload, "maxBlurPx", component.maxBlurPx);
            JsonUtils::TryGetInt(payload, "sampleCount", component.sampleCount);
            component.runtimeEffect.reset();
            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultBloomSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<BloomComponent>(
        "BloomComponent",
        1,
        [](const BloomComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddBool(doc, payload, "enabled", component.enabled) &&
                   JsonUtils::MutObjAddBool(doc, payload, "adaptiveBloom", component.adaptiveBloom) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "threshold", component.threshold) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "softKnee", component.softKnee) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "intensity", component.intensity) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "radiusPx", component.radiusPx) &&
                   JsonUtils::MutObjAddInt(doc, payload, "sampleCount", component.sampleCount) &&
                   JsonUtils::MutObjAddVec3(doc, payload, "tint", component.tint) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](BloomComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            JsonUtils::TryGetBool(payload, "enabled", component.enabled);
            component.adaptiveBloom = false;
            JsonUtils::TryGetBool(payload, "adaptiveBloom", component.adaptiveBloom);
            JsonUtils::TryGetFloat(payload, "threshold", component.threshold);
            JsonUtils::TryGetFloat(payload, "softKnee", component.softKnee);
            JsonUtils::TryGetFloat(payload, "intensity", component.intensity);
            JsonUtils::TryGetFloat(payload, "radiusPx", component.radiusPx);
            JsonUtils::TryGetInt(payload, "sampleCount", component.sampleCount);
            JsonUtils::TryGetVec3(payload, "tint", component.tint);
            component.runtimeEffect.reset();
            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultLensFlareSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<LensFlareComponent>(
        "LensFlareComponent",
        1,
        [](const LensFlareComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddBool(doc, payload, "enabled", component.enabled) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](LensFlareComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            JsonUtils::TryGetBool(payload, "enabled", component.enabled);
            component.runtimeEffect.reset();
            readEditorComponentStateFields(component, payload);
            return true;
        },
        {},
        outError
    );
}

bool registerDefaultAutoExposureSerializer(Serialization::ComponentSerializationRegistry& registry, std::string* outError){
    return registry.registerTypedSerializer<AutoExposureComponent>(
        "AutoExposureComponent",
        1,
        [](const AutoExposureComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddBool(doc, payload, "enabled", component.enabled) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "minExposure", component.minExposure) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "maxExposure", component.maxExposure) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "exposureCompensation", component.exposureCompensation) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "adaptationSpeedUp", component.adaptationSpeedUp) &&
                   JsonUtils::MutObjAddFloat(doc, payload, "adaptationSpeedDown", component.adaptationSpeedDown) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](AutoExposureComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            JsonUtils::TryGetBool(payload, "enabled", component.enabled);
            JsonUtils::TryGetFloat(payload, "minExposure", component.minExposure);
            JsonUtils::TryGetFloat(payload, "maxExposure", component.maxExposure);
            JsonUtils::TryGetFloat(payload, "exposureCompensation", component.exposureCompensation);
            JsonUtils::TryGetFloat(payload, "adaptationSpeedUp", component.adaptationSpeedUp);
            JsonUtils::TryGetFloat(payload, "adaptationSpeedDown", component.adaptationSpeedDown);
            component.minExposure = Math3D::Clamp(component.minExposure, 0.01f, 64.0f);
            component.maxExposure = Math3D::Clamp(component.maxExposure, component.minExposure, 64.0f);
            component.exposureCompensation = Math3D::Clamp(component.exposureCompensation, -8.0f, 8.0f);
            component.adaptationSpeedUp = Math3D::Clamp(component.adaptationSpeedUp, 0.01f, 20.0f);
            component.adaptationSpeedDown = Math3D::Clamp(component.adaptationSpeedDown, 0.01f, 20.0f);
            component.runtimeEffect.reset();
            readEditorComponentStateFields(component, payload);
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
        [](const AntiAliasingComponent& component, yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* error) -> bool {
            return JsonUtils::MutObjAddInt(doc, payload, "preset", static_cast<int>(component.preset)) &&
                   writeEditorComponentStateFields(component, doc, payload, error);
        },
        [](AntiAliasingComponent& component, JsonUtils::JsonVal* payload, int version, std::string* error) -> bool {
            (void)version;
            (void)error;
            int preset = static_cast<int>(component.preset);
            JsonUtils::TryGetInt(payload, "preset", preset);
            component.preset = enumFromIntClamped(preset, 0, 3, AntiAliasingPreset::FXAA_Medium);
            component.runtimeEffect.reset();
            readEditorComponentStateFields(component, payload);
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
    JsonUtils::JsonMutVal* payload = payloadDoc.setRootObject();
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
    JsonUtils::JsonVal* payload = nullptr;
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
    if(!registry.hasSerializer("EntityPropertiesComponent") && !registerDefaultEntityPropertiesSerializer(registry, outError)) return;
    if(!registry.hasSerializer("BoundsComponent") && !registerDefaultBoundsSerializer(registry, outError)) return;
    if(!registry.hasSerializer("MeshRendererComponent") && !registerDefaultMeshRendererSerializer(registry, outError)) return;
    if(!registry.hasSerializer("LightComponent") && !registerDefaultLightSerializer(registry, outError)) return;
    if(!registry.hasSerializer("CameraComponent") && !registerDefaultCameraSerializer(registry, outError)) return;
    if(!registry.hasSerializer("SkyboxComponent") && !registerDefaultSkyboxSerializer(registry, outError)) return;
    if(!registry.hasSerializer("EnvironmentComponent") && !registerDefaultEnvironmentSerializer(registry, outError)) return;
    if(!registry.hasSerializer("ColliderComponent") && !registerDefaultColliderSerializer(registry, outError)) return;
    if(!registry.hasSerializer("RigidBodyComponent") && !registerDefaultRigidBodySerializer(registry, outError)) return;
    if(!registry.hasSerializer("ScriptComponent") && !registerDefaultScriptSerializer(registry, outError)) return;
    if(!registry.hasSerializer("SSAOComponent") && !registerDefaultSsaoSerializer(registry, outError)) return;
    if(!registry.hasSerializer("PostProcessingStackComponent") && !registerDefaultPostProcessingStackSerializer(registry, outError)) return;
    if(!registry.hasSerializer("DepthOfFieldComponent") && !registerDefaultDepthOfFieldSerializer(registry, outError)) return;
    if(!registry.hasSerializer("BloomComponent") && !registerDefaultBloomSerializer(registry, outError)) return;
    if(!registry.hasSerializer("LensFlareComponent") && !registerDefaultLensFlareSerializer(registry, outError)) return;
    if(!registry.hasSerializer("AutoExposureComponent") && !registerDefaultAutoExposureSerializer(registry, outError)) return;
    if(!registry.hasSerializer("AntiAliasingComponent") && !registerDefaultAntiAliasingSerializer(registry, outError)) return;
}

ComponentSerializationRegistry& DefaultComponentSerializationRegistry(){
    static ComponentSerializationRegistry registry = ComponentSerializationRegistry::CreateDefault();
    return registry;
}

} // namespace Serialization
