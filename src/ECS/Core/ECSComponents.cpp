/**
 * @file src/ECS/Core/ECSComponents.cpp
 * @brief Implementation for ECSComponents.
 */

#include "ECS/Core/ECSComponents.h"
#include "imgui.h"
#include "Engine/Core/GameEngine.h"
#include "Rendering/Materials/PBRMaterial.h"
#include "Rendering/Materials/ConstructedMaterial.h"
#include "Rendering/Geometry/ModelPartPrefabs.h"
#include "Assets/Core/Asset.h"
#include "Assets/Bundles/AssetBundleRegistry.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Editor/Core/EditorAssetUI.h"
#include "Editor/Core/EditorPropertyUI.h"
#include "Assets/Descriptors/ShaderAsset.h"
#include "Assets/Descriptors/MaterialAsset.h"
#include "Assets/Descriptors/ImageAsset.h"
#include "Assets/Descriptors/ModelAsset.h"
#include "Assets/Descriptors/SkyboxAsset.h"
#include "Assets/Descriptors/EnvironmentAsset.h"
#include "Assets/Descriptors/EffectAsset.h"
#include "Rendering/PostFX/LensFlareEffect.h"
#include "Rendering/Materials/MaterialRegistry.h"
#include "Rendering/Lighting/ShadowRenderer.h"
#include "Foundation/Logging/Logbot.h"
#include "Foundation/IO/File.h"
#include "Editor/Widgets/BoundsEditState.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace {
    bool looksLikeColorField(const ConstructedMaterial::Field& field){
        auto containsColorHint = [](const std::string& value) -> bool{
            if(value.empty()){
                return false;
            }
            const std::string lower = StringUtils::ToLowerCase(value);
            return lower.find("color") != std::string::npos ||
                   lower.find("albedo") != std::string::npos ||
                   lower.find("tint") != std::string::npos;
        };

        return containsColorHint(field.key) ||
               containsColorHint(field.displayName) ||
               containsColorHint(field.uniformName);
    }

    bool isDeferredCompatibleMaterial(const std::shared_ptr<Material>& material){
        if(!material){
            return false;
        }
        return (Material::GetAs<PBRMaterial>(material) != nullptr);
    }

    /// @brief Represents Shader Picker State data.
    struct ShaderPickerState{
        int selectedCacheIndex = -1;
        char cacheName[96] = "UserMaterialShader";
        char vertexPath[256] = "@assets/shader/Shader_Vert_Lit.vert";
        char fragmentPath[256] = "@assets/shader/Shader_Frag_LitColor.frag";
        char shaderAssetPath[256] = "";
        std::filesystem::file_time_type shaderAssetWriteTime{};
        bool hasAppliedShaderAsset = false;
    };

    /// @brief Represents Material Asset Field State data.
    struct MaterialAssetFieldState{
        char baseColorTex[256] = "";
        char roughnessTex[256] = "";
        char metallicRoughTex[256] = "";
        char normalTex[256] = "";
        char heightTex[256] = "";
        char emissiveTex[256] = "";
        char occlusionTex[256] = "";
        char imageTex[256] = "";
    };

    std::unordered_map<uintptr_t, ShaderPickerState> g_shaderPickerStates;
    std::unordered_map<uintptr_t, MaterialAssetFieldState> g_materialAssetStates;
    /// @brief Represents Material Selector State data.
    struct MaterialSelectorState{
        int selectedRegistryIndex = -1;
        char materialAssetPath[256] = "";
    };
    std::unordered_map<std::string, MaterialSelectorState> g_materialSelectorStates;
    /// @brief Represents Model Selector State data.
    struct ModelSelectorState{
        char modelAssetPath[256] = "";
    };
    std::unordered_map<std::string, ModelSelectorState> g_modelSelectorStates;

    bool drawMaterialAssetFields(const std::shared_ptr<Material>& material, const char* idSuffix);
    bool drawShaderAssignmentUI(const std::shared_ptr<Material>& material, const char* idSuffix);

    ShaderPickerState& getShaderPickerState(const std::shared_ptr<Material>& material){
        uintptr_t key = reinterpret_cast<uintptr_t>(material.get());
        return g_shaderPickerStates[key];
    }

    MaterialAssetFieldState& getMaterialAssetFieldState(const std::shared_ptr<Material>& material){
        uintptr_t key = reinterpret_cast<uintptr_t>(material.get());
        return g_materialAssetStates[key];
    }

    MaterialSelectorState& getMaterialSelectorState(const char* idSuffix){
        return g_materialSelectorStates[idSuffix ? idSuffix : "default"];
    }

    ModelSelectorState& getModelSelectorState(const char* idSuffix){
        return g_modelSelectorStates[idSuffix ? idSuffix : "default"];
    }

    void copyTextToBuffer(char* buffer, size_t bufferSize, const std::string& value){
        if(!buffer || bufferSize == 0){
            return;
        }
        std::snprintf(buffer, bufferSize, "%s", value.c_str());
        buffer[bufferSize - 1] = '\0';
    }

    void ensureModelPartMaterialRefCount(MeshRendererComponent* renderer){
        if(!renderer || !renderer->model){
            return;
        }
        const size_t partCount = renderer->model->getParts().size();
        if(renderer->modelPartMaterialAssetRefs.size() < partCount){
            renderer->modelPartMaterialAssetRefs.resize(partCount);
        }else if(renderer->modelPartMaterialAssetRefs.size() > partCount){
            renderer->modelPartMaterialAssetRefs.resize(partCount);
        }
        if(renderer->modelPartMaterialOverrides.size() < partCount){
            renderer->modelPartMaterialOverrides.resize(partCount, 0);
        }else if(renderer->modelPartMaterialOverrides.size() > partCount){
            renderer->modelPartMaterialOverrides.resize(partCount);
        }
    }

    void initializePartMaterialRefsFromModelAsset(MeshRendererComponent* renderer){
        if(!renderer || !renderer->model){
            return;
        }

        ensureModelPartMaterialRefCount(renderer);
        if(renderer->modelAssetRef.empty()){
            return;
        }

        ModelAssetData modelAssetData;
        std::string error;
        if(!ModelAssetIO::LoadFromAssetRef(renderer->modelAssetRef, modelAssetData, &error)){
            LogBot.Log(LOG_WARN, "Failed to read model asset metadata '%s': %s", renderer->modelAssetRef.c_str(), error.c_str());
            return;
        }

        if(modelAssetData.defaultMaterialRef.empty()){
            return;
        }

        for(size_t i = 0; i < renderer->modelPartMaterialAssetRefs.size(); ++i){
            renderer->modelPartMaterialAssetRefs[i] = modelAssetData.defaultMaterialRef;
        }
    }

    std::shared_ptr<Texture> loadTextureFromAssetRef(const std::string& assetRef){
        if(assetRef.empty()){
            return nullptr;
        }
        std::string error;
        auto tex = ImageAssetIO::InstantiateTextureFromRef(assetRef, nullptr, &error);
        if(!tex){
            if(error.empty()){
                error = "Unknown texture decode error.";
            }
            LogBot.Log(LOG_ERRO, "Failed to create texture from asset '%s': %s", assetRef.c_str(), error.c_str());
            return nullptr;
        }
        return tex;
    }

    void drawTextureOutputSmall(const std::shared_ptr<Texture>& texture, bool hasAssignedAsset, const char* uniqueId){
        const ImVec2 previewSize(44.0f, 44.0f);

        if(texture && texture->getID() != 0){
            ImTextureID texId = (ImTextureID)(intptr_t)texture->getID();
            ImGui::Image(texId, previewSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            return;
        }

        std::string buttonLabel = std::string(hasAssignedAsset ? "Missing##" : "None##") +
                                  (uniqueId ? uniqueId : "tex_preview");

        ImGui::BeginDisabled();
        ImGui::Button(buttonLabel.c_str(), previewSize);
        ImGui::EndDisabled();
    }

    std::string makeShaderAssetCacheName(const std::string& assetRef, const ShaderAssetData& data){
        std::string cacheName = data.cacheName;
        if(cacheName.empty()){
            cacheName = assetRef;
            for(char& c : cacheName){
                if((c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') ||
                   c == '_' || c == '-'){
                    continue;
                }
                c = '_';
            }
        }
        return cacheName;
    }

    bool applyShaderAssetToMaterial(const std::shared_ptr<Material>& material, ShaderPickerState& picker, bool forceRecompile){
        if(!material){
            return false;
        }

        std::string assetRef = picker.shaderAssetPath;
        if(assetRef.empty()){
            picker.hasAppliedShaderAsset = false;
            return false;
        }

        ShaderAssetData data;
        std::string error;
        if(!ShaderAssetIO::LoadFromAssetRef(assetRef, data, &error)){
            LogBot.Log(LOG_ERRO, "Failed to read shader asset '%s': %s", assetRef.c_str(), error.c_str());
            picker.hasAppliedShaderAsset = false;
            return false;
        }

        std::string cacheName = makeShaderAssetCacheName(assetRef, data);
        auto program = ShaderAssetIO::CompileProgram(data, cacheName, forceRecompile, &error);
        if(!program || program->getID() == 0){
            LogBot.Log(LOG_ERRO, "Failed to compile shader asset '%s': %s", assetRef.c_str(), error.c_str());
            picker.hasAppliedShaderAsset = false;
            return false;
        }

        std::shared_ptr<ShaderProgram> shaderPtr = material->getShader();
        if(EditorAssetUI::TryReplacePointer(shaderPtr, program)){
            material->setShader(shaderPtr);
        }

        std::error_code ec;
        std::filesystem::path absPath = ShaderAssetIO::AssetRefToAbsolutePath(assetRef);
        if(std::filesystem::exists(absPath, ec)){
            picker.shaderAssetWriteTime = std::filesystem::last_write_time(absPath, ec);
        }
        picker.hasAppliedShaderAsset = true;
        return true;
    }

    void refreshShaderAssetLive(const std::shared_ptr<Material>& material, ShaderPickerState& picker){
        std::string assetRef = picker.shaderAssetPath;
        if(assetRef.empty()){
            picker.hasAppliedShaderAsset = false;
            return;
        }
        if(!picker.hasAppliedShaderAsset){
            return;
        }

        std::error_code ec;
        std::filesystem::path absPath = ShaderAssetIO::AssetRefToAbsolutePath(assetRef);
        if(!std::filesystem::exists(absPath, ec)){
            return;
        }

        std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(absPath, ec);
        if(ec){
            return;
        }

        if(writeTime != picker.shaderAssetWriteTime){
            applyShaderAssetToMaterial(material, picker, true);
        }
    }

    std::vector<std::string> getShaderCacheNames(){
        std::vector<std::string> names;
        names.reserve(ShaderCacheManager::INSTANCE.programCache.size());
        for(const auto& kv : ShaderCacheManager::INSTANCE.programCache){
            names.push_back(kv.first);
        }
        return names;
    }

    bool drawMaterialValueFields(const std::shared_ptr<Material>& material, const char* idSuffix){
        if(!material){
            return false;
        }

        std::string header = std::string("Material Parameters##") + idSuffix;
        if(!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)){
            return false;
        }

        bool changed = false;

        bool castsShadows = material->castsShadows();
        std::string castsLabel = std::string("Casts Shadows##") + idSuffix;
        if(EditorPropertyUI::Checkbox(castsLabel.c_str(), &castsShadows)){
            material->setCastsShadows(castsShadows);
            changed = true;
        }
        bool receivesShadows = material->receivesShadows();
        std::string receivesLabel = std::string("Receives Shadows##") + idSuffix;
        if(EditorPropertyUI::Checkbox(receivesLabel.c_str(), &receivesShadows)){
            material->setReceivesShadows(receivesShadows);
            changed = true;
        }

        if(auto pbr = Material::GetAs<PBRMaterial>(material)){
            Math3D::Vec4 baseColor = pbr->BaseColor.get();
            std::string baseColorLabel = std::string("Base Color##") + idSuffix;
            if(EditorPropertyUI::ColorEdit4(baseColorLabel.c_str(), &baseColor.x)){
                pbr->BaseColor = baseColor;
                changed = true;
            }

            float metallic = pbr->Metallic.get();
            std::string metallicLabel = std::string("Metallic##") + idSuffix;
            if(EditorPropertyUI::SliderFloat(metallicLabel.c_str(), &metallic, 0.0f, 1.0f)){
                pbr->Metallic = metallic;
                changed = true;
            }

            float roughness = pbr->Roughness.get();
            std::string roughnessLabel = std::string("Roughness##") + idSuffix;
            if(EditorPropertyUI::SliderFloat(roughnessLabel.c_str(), &roughness, 0.0f, 1.0f)){
                pbr->Roughness = roughness;
                changed = true;
            }

            float normalScale = pbr->NormalScale.get();
            std::string normalScaleLabel = std::string("Normal Scale##") + idSuffix;
            if(EditorPropertyUI::DragFloat(normalScaleLabel.c_str(), &normalScale, 0.01f, 0.0f, 8.0f)){
                pbr->NormalScale = normalScale;
                changed = true;
            }

            float heightScale = pbr->HeightScale.get();
            std::string heightScaleLabel = std::string("Height Scale##") + idSuffix;
            if(EditorPropertyUI::DragFloat(heightScaleLabel.c_str(), &heightScale, 0.001f, 0.0f, 1.0f)){
                pbr->HeightScale = heightScale;
                changed = true;
            }

            Math3D::Vec3 emissiveColor = pbr->EmissiveColor.get();
            std::string emissiveColorLabel = std::string("Emissive Color##") + idSuffix;
            if(EditorPropertyUI::ColorEdit3(emissiveColorLabel.c_str(), &emissiveColor.x)){
                pbr->EmissiveColor = emissiveColor;
                changed = true;
            }

            float emissiveStrength = pbr->EmissiveStrength.get();
            std::string emissiveStrengthLabel = std::string("Emissive Strength##") + idSuffix;
            if(EditorPropertyUI::DragFloat(emissiveStrengthLabel.c_str(), &emissiveStrength, 0.01f, 0.0f, 32.0f)){
                pbr->EmissiveStrength = emissiveStrength;
                changed = true;
            }

            float occlusionStrength = pbr->OcclusionStrength.get();
            std::string occlusionStrengthLabel = std::string("AO Strength##") + idSuffix;
            if(EditorPropertyUI::SliderFloat(occlusionStrengthLabel.c_str(), &occlusionStrength, 0.0f, 4.0f)){
                pbr->OcclusionStrength = occlusionStrength;
                changed = true;
            }

            Math3D::Vec2 uvScale = pbr->UVScale.get();
            std::string uvScaleLabel = std::string("UV Scale##") + idSuffix;
            if(EditorPropertyUI::DragFloat2(uvScaleLabel.c_str(), &uvScale.x, 0.01f, -64.0f, 64.0f)){
                pbr->UVScale = uvScale;
                changed = true;
            }

            Math3D::Vec2 uvOffset = pbr->UVOffset.get();
            std::string uvOffsetLabel = std::string("UV Offset##") + idSuffix;
            if(EditorPropertyUI::DragFloat2(uvOffsetLabel.c_str(), &uvOffset.x, 0.01f, -64.0f, 64.0f)){
                pbr->UVOffset = uvOffset;
                changed = true;
            }

            bool useAlphaClip = (pbr->UseAlphaClip.get() != 0);
            std::string useAlphaClipLabel = std::string("Use Alpha Clip##") + idSuffix;
            if(EditorPropertyUI::Checkbox(useAlphaClipLabel.c_str(), &useAlphaClip)){
                pbr->UseAlphaClip = useAlphaClip ? 1 : 0;
                changed = true;
            }
            if(useAlphaClip){
                float alphaCutoff = pbr->AlphaCutoff.get();
                std::string alphaCutoffLabel = std::string("Alpha Cutoff##") + idSuffix;
                if(EditorPropertyUI::SliderFloat(alphaCutoffLabel.c_str(), &alphaCutoff, 0.0f, 1.0f)){
                    pbr->AlphaCutoff = alphaCutoff;
                    changed = true;
                }
            }

            bool useEnvMap = (pbr->UseEnvMap.get() != 0);
            std::string useEnvMapLabel = std::string("Use Env Map##") + idSuffix;
            if(EditorPropertyUI::Checkbox(useEnvMapLabel.c_str(), &useEnvMap)){
                pbr->UseEnvMap = useEnvMap ? 1 : 0;
                changed = true;
            }

            float envStrength = pbr->EnvStrength.get();
            std::string envStrengthLabel = std::string("Env Strength##") + idSuffix;
            if(EditorPropertyUI::DragFloat(envStrengthLabel.c_str(), &envStrength, 0.01f, 0.0f, 8.0f)){
                pbr->EnvStrength = envStrength;
                changed = true;
            }
        }else if(auto constructed = Material::GetAs<ConstructedMaterial>(material)){
            auto& fields = constructed->fields();
            if(fields.empty()){
                ImGui::TextDisabled("No editable constructed fields.");
            }

            for(size_t i = 0; i < fields.size(); ++i){
                auto& field = fields[i];
                std::string fieldName = field.displayName;
                if(fieldName.empty()){
                    fieldName = field.key;
                }
                if(fieldName.empty()){
                    fieldName = field.uniformName;
                }
                if(fieldName.empty()){
                    fieldName = "Field " + std::to_string(i + 1);
                }

                const std::string fieldLabel = fieldName + "##" + idSuffix + "_constructed_" + std::to_string(i);
                bool changedField = false;
                switch(field.type){
                    case ConstructedMaterial::FieldType::Float:{
                        changedField |= EditorPropertyUI::DragFloat(fieldLabel.c_str(), &field.floatValue, 0.01f);
                        break;
                    }
                    case ConstructedMaterial::FieldType::Int:{
                        changedField |= EditorPropertyUI::DragInt(fieldLabel.c_str(), &field.intValue, 1.0f);
                        break;
                    }
                    case ConstructedMaterial::FieldType::Bool:{
                        changedField |= EditorPropertyUI::Checkbox(fieldLabel.c_str(), &field.boolValue);
                        break;
                    }
                    case ConstructedMaterial::FieldType::Vec2:{
                        changedField |= EditorPropertyUI::DragFloat2(fieldLabel.c_str(), &field.vec2Value.x, 0.01f);
                        break;
                    }
                    case ConstructedMaterial::FieldType::Vec3:{
                        if(looksLikeColorField(field)){
                            changedField |= EditorPropertyUI::ColorEdit3(fieldLabel.c_str(), &field.vec3Value.x);
                        }else{
                            changedField |= EditorPropertyUI::DragFloat3(fieldLabel.c_str(), &field.vec3Value.x, 0.01f);
                        }
                        break;
                    }
                    case ConstructedMaterial::FieldType::Vec4:{
                        if(looksLikeColorField(field)){
                            changedField |= EditorPropertyUI::ColorEdit4(fieldLabel.c_str(), &field.vec4Value.x);
                        }else{
                            changedField |= EditorPropertyUI::DragFloat4(fieldLabel.c_str(), &field.vec4Value.x, 0.01f);
                        }
                        break;
                    }
                    case ConstructedMaterial::FieldType::Texture2D:{
                        changedField |= EditorAssetUI::DrawAssetDropInput(fieldLabel.c_str(), field.textureAssetRef, {EditorAssetUI::AssetKind::Image});
                        std::string previewId = std::string(idSuffix) + "_constructed_" + std::to_string(i) + "_preview";
                        drawTextureOutputSmall(field.texturePtr, !field.textureAssetRef.empty(), previewId.c_str());
                        break;
                    }
                    default:
                        break;
                }

                if(changedField){
                    constructed->markFieldsDirty();
                    constructed->applyField(i);
                    changed = true;
                }
            }
        }else if(auto colorMat = Material::GetAs<MaterialDefaults::ColorMaterial>(material)){
            Math3D::Vec4 color = colorMat->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(EditorPropertyUI::ColorEdit4(colorLabel.c_str(), &color.x)){
                colorMat->Color = color;
                changed = true;
            }
        }else if(auto imageMat = Material::GetAs<MaterialDefaults::ImageMaterial>(material)){
            Math3D::Vec4 color = imageMat->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(EditorPropertyUI::ColorEdit4(colorLabel.c_str(), &color.x)){
                imageMat->Color = color;
                changed = true;
            }
            Math3D::Vec2 uv = imageMat->UV.get();
            std::string uvLabel = std::string("UV Offset##") + idSuffix;
            if(EditorPropertyUI::DragFloat2(uvLabel.c_str(), &uv.x, 0.01f, -64.0f, 64.0f)){
                imageMat->UV = uv;
                changed = true;
            }
        }else if(auto litColor = Material::GetAs<MaterialDefaults::LitColorMaterial>(material)){
            Math3D::Vec4 color = litColor->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(EditorPropertyUI::ColorEdit4(colorLabel.c_str(), &color.x)){
                litColor->Color = color;
                changed = true;
            }
        }else if(auto litImage = Material::GetAs<MaterialDefaults::LitImageMaterial>(material)){
            Math3D::Vec4 color = litImage->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(EditorPropertyUI::ColorEdit4(colorLabel.c_str(), &color.x)){
                litImage->Color = color;
                changed = true;
            }
        }else if(auto flatColor = Material::GetAs<MaterialDefaults::FlatColorMaterial>(material)){
            Math3D::Vec4 color = flatColor->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(EditorPropertyUI::ColorEdit4(colorLabel.c_str(), &color.x)){
                flatColor->Color = color;
                changed = true;
            }
        }else if(auto flatImage = Material::GetAs<MaterialDefaults::FlatImageMaterial>(material)){
            Math3D::Vec4 color = flatImage->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(EditorPropertyUI::ColorEdit4(colorLabel.c_str(), &color.x)){
                flatImage->Color = color;
                changed = true;
            }
        }else{
            ImGui::TextDisabled("No editable material value fields for this type.");
        }

        ImGui::Separator();
        changed = drawMaterialAssetFields(material, idSuffix) || changed;
        changed = drawShaderAssignmentUI(material, idSuffix) || changed;
        return changed;
    }

    bool drawMaterialSelectionUI(std::shared_ptr<Material>& materialRef, const char* idSuffix, std::string* outMaterialAssetRef){
        MaterialRegistry::Instance().Refresh();
        const auto& entries = MaterialRegistry::Instance().GetEntries();
        MaterialSelectorState& picker = getMaterialSelectorState(idSuffix);
        bool changed = false;
        if(picker.selectedRegistryIndex >= static_cast<int>(entries.size())){
            picker.selectedRegistryIndex = -1;
        }

        std::string comboLabel = std::string("Material Registry##") + idSuffix;
        const char* preview = (picker.selectedRegistryIndex >= 0 && picker.selectedRegistryIndex < static_cast<int>(entries.size()))
            ? entries[picker.selectedRegistryIndex].displayName.c_str()
            : "<select material>";

        if(EditorPropertyUI::BeginCombo(comboLabel.c_str(), preview)){
            for(int i = 0; i < static_cast<int>(entries.size()); ++i){
                bool selected = (picker.selectedRegistryIndex == i);
                if(ImGui::Selectable(entries[i].displayName.c_str(), selected)){
                    picker.selectedRegistryIndex = i;
                }
                if(selected){
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        std::string applyRegistryLabel = std::string("Apply Registry Material##") + idSuffix;
        if(ImGui::Button(applyRegistryLabel.c_str())){
            if(picker.selectedRegistryIndex >= 0 && picker.selectedRegistryIndex < static_cast<int>(entries.size())){
                std::string error;
                auto created = MaterialRegistry::Instance().CreateById(entries[picker.selectedRegistryIndex].id, &error);
                if(created){
                    materialRef = created;
                    if(outMaterialAssetRef){
                        outMaterialAssetRef->clear();
                    }
                    changed = true;
                }else{
                    LogBot.Log(LOG_ERRO, "Failed to apply material registry entry: %s", error.c_str());
                }
            }
        }

        std::string assetFieldLabel = std::string("Material##") + idSuffix;
        bool dropped = false;
        EditorAssetUI::DrawAssetDropInput(assetFieldLabel.c_str(), picker.materialAssetPath, sizeof(picker.materialAssetPath), EditorAssetUI::AssetKind::Material, false, &dropped);

        std::string applyAssetLabel = std::string("Apply Material##") + idSuffix;
        std::string clearAssetLabel = std::string("Clear Material##") + idSuffix;
        bool applyAsset = ImGui::Button(applyAssetLabel.c_str());
        if(ImGui::Button(clearAssetLabel.c_str())){
            picker.materialAssetPath[0] = '\0';
        }

        if(dropped || applyAsset){
            std::string assetRef = picker.materialAssetPath;
            if(!assetRef.empty()){
                std::string error;
                std::string resolvedAssetRef;
                auto mat = MaterialAssetIO::InstantiateMaterialFromRef(assetRef, &resolvedAssetRef, &error);
                if(!mat){
                    LogBot.Log(LOG_ERRO, "Failed to instantiate material '%s': %s", assetRef.c_str(), error.c_str());
                }else{
                    materialRef = mat;
                    if(outMaterialAssetRef){
                        *outMaterialAssetRef = resolvedAssetRef;
                    }
                    copyTextToBuffer(picker.materialAssetPath, sizeof(picker.materialAssetPath), resolvedAssetRef);
                    changed = true;
                }
            }
        }
        return changed;
    }

    void drawModelSelectionUI(MeshRendererComponent* renderer, const char* idSuffix){
        if(!renderer){
            return;
        }

        ModelSelectorState& picker = getModelSelectorState(idSuffix);
        if(picker.modelAssetPath[0] == '\0' && !renderer->modelAssetRef.empty()){
            copyTextToBuffer(picker.modelAssetPath, sizeof(picker.modelAssetPath), renderer->modelAssetRef);
        }

        std::string assetFieldLabel = std::string("Model Asset##") + idSuffix;
        bool dropped = false;
        EditorAssetUI::DrawAssetDropInput(
            assetFieldLabel.c_str(),
            picker.modelAssetPath,
            sizeof(picker.modelAssetPath),
            EditorAssetUI::AssetKind::ModelAsset,
            false,
            &dropped
        );

        std::string applyModelLabel = std::string("Apply Model Asset##") + idSuffix;
        std::string clearModelRefLabel = std::string("Clear Model Ref##") + idSuffix;
        bool applyModel = ImGui::Button(applyModelLabel.c_str());
        ImGui::SameLine();
        if(ImGui::Button(clearModelRefLabel.c_str())){
            picker.modelAssetPath[0] = '\0';
            renderer->modelAssetRef.clear();
            renderer->modelSourceRef.clear();
            renderer->modelForceSmoothNormals = 0;
        }

        if(dropped || applyModel){
            std::string assetRef = picker.modelAssetPath;
            if(!assetRef.empty()){
                std::string error;
                std::string resolvedAssetRef;
                auto model = ModelAssetIO::InstantiateModelFromRef(assetRef, &resolvedAssetRef, &error);
                if(!model){
                    LogBot.Log(LOG_ERRO, "Failed to instantiate model '%s': %s", assetRef.c_str(), error.c_str());
                }else{
                    renderer->model = model;
                    renderer->modelAssetRef = resolvedAssetRef;
                    renderer->modelSourceRef = model->getSourceAssetRef();
                    renderer->modelForceSmoothNormals = model->getSourceForceSmoothNormals() ? 1 : 0;
                    renderer->mesh.reset();
                    renderer->material.reset();
                    renderer->materialAssetRef.clear();
                    renderer->materialOverridesSource = false;
                    renderer->modelPartMaterialAssetRefs.clear();
                    renderer->modelPartMaterialOverrides.clear();
                    initializePartMaterialRefsFromModelAsset(renderer);
                    copyTextToBuffer(picker.modelAssetPath, sizeof(picker.modelAssetPath), resolvedAssetRef);
                }
            }
        }

        if(!renderer->modelAssetRef.empty()){
            ImGui::TextDisabled("Model Source: %s", renderer->modelAssetRef.c_str());
        }
    }

    bool drawShaderAssignmentUI(const std::shared_ptr<Material>& material, const char* idSuffix){
        if(!material){
            ImGui::TextDisabled("No material assigned.");
            return false;
        }

        std::string shaderHeader = std::string("Shader Assignment##") + idSuffix;
        if(!ImGui::TreeNodeEx(shaderHeader.c_str())){
            return false;
        }

        bool changed = false;
        auto shader = material->getShader();
        ImGui::Text("Shader Program ID: %u", shader ? shader->getID() : 0);

        ShaderPickerState& picker = getShaderPickerState(material);
        refreshShaderAssetLive(material, picker);

        std::string shaderAssetLabel = std::string("Shader Asset##") + idSuffix;
        std::string applyShaderAssetLabel = std::string("Apply Shader Asset##") + idSuffix;
        std::string clearShaderAssetLabel = std::string("Clear Shader Asset##") + idSuffix;
        bool droppedShaderAsset = false;
        EditorAssetUI::DrawAssetDropInput(shaderAssetLabel.c_str(), picker.shaderAssetPath, sizeof(picker.shaderAssetPath), EditorAssetUI::AssetKind::ShaderAsset, false, &droppedShaderAsset);
        bool applyShaderAsset = ImGui::Button(applyShaderAssetLabel.c_str());
        if(ImGui::Button(clearShaderAssetLabel.c_str())){
            picker.shaderAssetPath[0] = '\0';
            picker.hasAppliedShaderAsset = false;
        }
        if(droppedShaderAsset || applyShaderAsset){
            changed = applyShaderAssetToMaterial(material, picker, true) || changed;
        }
        ImGui::Separator();

        const std::vector<std::string> shaderNames = getShaderCacheNames();
        std::vector<const char*> shaderNamePtrs;
        shaderNamePtrs.reserve(shaderNames.size());
        for(const std::string& name : shaderNames){
            shaderNamePtrs.push_back(name.c_str());
        }
        if(picker.selectedCacheIndex >= static_cast<int>(shaderNamePtrs.size())){
            picker.selectedCacheIndex = -1;
        }

        std::string comboLabel = std::string("Cached Shader##") + idSuffix;
        const char* preview = (picker.selectedCacheIndex >= 0 && picker.selectedCacheIndex < static_cast<int>(shaderNamePtrs.size()))
                                ? shaderNamePtrs[picker.selectedCacheIndex]
                                : "<select>";
        if(EditorPropertyUI::BeginCombo(comboLabel.c_str(), preview)){
            for(int i = 0; i < static_cast<int>(shaderNamePtrs.size()); ++i){
                bool selected = (picker.selectedCacheIndex == i);
                if(ImGui::Selectable(shaderNamePtrs[i], selected)){
                    picker.selectedCacheIndex = i;
                }
                if(selected){
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        std::string useCachedLabel = std::string("Use Cached Shader##") + idSuffix;
        if(ImGui::Button(useCachedLabel.c_str())){
            if(picker.selectedCacheIndex >= 0 && picker.selectedCacheIndex < static_cast<int>(shaderNames.size())){
                auto it = ShaderCacheManager::INSTANCE.programCache.find(shaderNames[picker.selectedCacheIndex]);
                if(it != ShaderCacheManager::INSTANCE.programCache.end() && it->second && it->second->getID() != 0){
                    std::shared_ptr<ShaderProgram> shaderPtr = material->getShader();
                    if(EditorAssetUI::TryReplacePointer(shaderPtr, it->second)){
                        material->setShader(shaderPtr);
                        picker.hasAppliedShaderAsset = false;
                        changed = true;
                    }
                }else{
                    LogBot.Log(LOG_ERRO, "Selected cached shader is invalid.");
                }
            }
        }

        std::string cacheNameLabel = std::string("Cache Name##") + idSuffix;
        std::string vertPathLabel = std::string("Vertex Asset##") + idSuffix;
        std::string fragPathLabel = std::string("Fragment Asset##") + idSuffix;
        std::string loadFilesLabel = std::string("Load Shader Files##") + idSuffix;
        EditorPropertyUI::InputText(cacheNameLabel.c_str(), picker.cacheName, sizeof(picker.cacheName));
        EditorAssetUI::DrawAssetDropInput(vertPathLabel.c_str(), picker.vertexPath, sizeof(picker.vertexPath), EditorAssetUI::AssetKind::ShaderVertex);
        EditorAssetUI::DrawAssetDropInput(fragPathLabel.c_str(), picker.fragmentPath, sizeof(picker.fragmentPath), EditorAssetUI::AssetKind::ShaderFragment);

        if(ImGui::Button(loadFilesLabel.c_str())){
            std::string vertPath = picker.vertexPath;
            std::string fragPath = picker.fragmentPath;
            if(vertPath.empty() || fragPath.empty()){
                LogBot.Log(LOG_ERRO, "Shader file paths cannot be empty.");
            }else{
                auto vertAsset = AssetManager::Instance.getOrLoad(vertPath);
                auto fragAsset = AssetManager::Instance.getOrLoad(fragPath);
                if(!vertAsset || !fragAsset){
                    LogBot.Log(LOG_ERRO, "Failed to load shader assets (%s, %s).", vertPath.c_str(), fragPath.c_str());
                }else{
                    std::string cacheName = picker.cacheName;
                    if(cacheName.empty()){
                        cacheName = "UserMaterialShader";
                    }
                    auto program = ShaderCacheManager::INSTANCE.getOrCompile(cacheName, vertAsset->asString(), fragAsset->asString());
                    if(!program || program->getID() == 0){
                        LogBot.Log(LOG_ERRO, "Failed to compile shader (%s).", cacheName.c_str());
                    }else{
                        std::shared_ptr<ShaderProgram> shaderPtr = material->getShader();
                        if(EditorAssetUI::TryReplacePointer(shaderPtr, program)){
                            material->setShader(shaderPtr);
                            picker.hasAppliedShaderAsset = false;
                            changed = true;
                        }
                    }
                }
            }
        }

        ImGui::TreePop();
        return changed;
    }

    void drawPartTransformEditor(Math3D::Transform& localTransform){
        Math3D::Vec3 pos = localTransform.position;
        Math3D::Vec3 rot = localTransform.rotation.ToEuler();
        Math3D::Vec3 scale = localTransform.scale;

        if(EditorPropertyUI::DragFloat3("Local Position", &pos.x, 0.1f)){
            localTransform.position = pos;
        }
        if(EditorPropertyUI::DragFloat3("Local Rotation", &rot.x, 0.5f)){
            localTransform.setRotation(rot);
        }
        if(EditorPropertyUI::DragFloat3("Local Scale", &scale.x, 0.1f)){
            localTransform.scale = scale;
        }
    }

    bool drawMaterialAssetFields(const std::shared_ptr<Material>& material, const char* idSuffix){
        if(!material){
            return false;
        }

        MaterialAssetFieldState& fieldState = getMaterialAssetFieldState(material);
        bool changed = false;

        auto drawTextureField = [&](const char* label,
                                    const char* key,
                                    char* pathBuffer,
                                    const std::function<void(const std::shared_ptr<Texture>&)>& applyTexture,
                                    const std::function<std::shared_ptr<Texture>()>& readTexture){
            const std::string fieldLabel = std::string(label) + "##" + idSuffix + "_" + key + "_asset";
            bool committed = false;
            EditorAssetUI::DrawAssetDropInput(
                fieldLabel.c_str(),
                pathBuffer,
                256,
                EditorAssetUI::AssetKind::Image,
                false,
                nullptr,
                &committed
            );

            std::shared_ptr<Texture> previewTex;
            if(readTexture){
                previewTex = readTexture();
            }

            std::string previewId = std::string(idSuffix) + "_" + key + "_preview";
            drawTextureOutputSmall(previewTex, pathBuffer[0] != '\0', previewId.c_str());

            if(committed){
                std::string assetRef = pathBuffer;
                if(assetRef.empty()){
                    applyTexture(nullptr);
                    changed = true;
                }else{
                    auto tex = loadTextureFromAssetRef(assetRef);
                    if(tex){
                        applyTexture(tex);
                        changed = true;
                    }
                }
            }

            ImGui::Spacing();
        };

        if(auto pbr = Material::GetAs<PBRMaterial>(material)){
            std::string header = std::string("PBR Texture Slots##") + idSuffix;
            if(ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)){
                drawTextureField("Base Color", "pbr_base", fieldState.baseColorTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->BaseColorTex = tex; },
                    [&](){ return pbr->BaseColorTex.get(); });
                drawTextureField("Roughness", "pbr_rough", fieldState.roughnessTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->RoughnessTex = tex; },
                    [&](){ return pbr->RoughnessTex.get(); });
                drawTextureField("Metal/Rough", "pbr_mr", fieldState.metallicRoughTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->MetallicRoughnessTex = tex; },
                    [&](){ return pbr->MetallicRoughnessTex.get(); });
                drawTextureField("Normal", "pbr_normal", fieldState.normalTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->NormalTex = tex; },
                    [&](){ return pbr->NormalTex.get(); });
                drawTextureField("Height", "pbr_height", fieldState.heightTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->HeightTex = tex; },
                    [&](){ return pbr->HeightTex.get(); });
                drawTextureField("Emissive", "pbr_emissive", fieldState.emissiveTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->EmissiveTex = tex; },
                    [&](){ return pbr->EmissiveTex.get(); });
                drawTextureField("Occlusion", "pbr_occ", fieldState.occlusionTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->OcclusionTex = tex; },
                    [&](){ return pbr->OcclusionTex.get(); });
                ImGui::TreePop();
            }
            return changed;
        }

        if(auto imageMat = Material::GetAs<MaterialDefaults::ImageMaterial>(material)){
            std::string header = std::string("Image Texture Slot##") + idSuffix;
            if(ImGui::TreeNodeEx(header.c_str())){
                drawTextureField("Texture", "img_tex", fieldState.imageTex,
                    [&](const std::shared_ptr<Texture>& tex){ imageMat->Tex = tex; },
                    [&](){ return imageMat->Tex.get(); });
                ImGui::TreePop();
            }
            return changed;
        }

        if(auto litImage = Material::GetAs<MaterialDefaults::LitImageMaterial>(material)){
            std::string header = std::string("Lit Image Texture Slot##") + idSuffix;
            if(ImGui::TreeNodeEx(header.c_str())){
                drawTextureField("Texture", "lit_img_tex", fieldState.imageTex,
                    [&](const std::shared_ptr<Texture>& tex){ litImage->Tex = tex; },
                    [&](){ return litImage->Tex.get(); });
                ImGui::TreePop();
            }
            return changed;
        }

        if(auto flatImage = Material::GetAs<MaterialDefaults::FlatImageMaterial>(material)){
            std::string header = std::string("Flat Image Texture Slot##") + idSuffix;
            if(ImGui::TreeNodeEx(header.c_str())){
                drawTextureField("Texture", "flat_img_tex", fieldState.imageTex,
                    [&](const std::shared_ptr<Texture>& tex){ flatImage->Tex = tex; },
                    [&](){ return flatImage->Tex.get(); });
                ImGui::TreePop();
            }
            return changed;
        }

        return changed;
    }
}

std::string BuildScriptDisplayNameFromPath(const std::string& scriptPath){
    std::string rawName = std::filesystem::path(scriptPath).stem().string();
    if(rawName.empty()){
        rawName = scriptPath;
    }
    if(rawName.empty()){
        return "Script";
    }

    std::string spaced;
    spaced.reserve(rawName.size() * 2);
    auto isSep = [](char c) -> bool {
        return c == '_' || c == '-' || c == '.' || c == ' ';
    };

    for(size_t i = 0; i < rawName.size(); ++i){
        unsigned char c = static_cast<unsigned char>(rawName[i]);
        if(isSep(static_cast<char>(c))){
            if(!spaced.empty() && spaced.back() != ' '){
                spaced.push_back(' ');
            }
            continue;
        }

        bool insertSpace = false;
        if(!spaced.empty() && spaced.back() != ' ' && i > 0){
            unsigned char prev = static_cast<unsigned char>(rawName[i - 1]);
            bool prevLower = std::islower(prev) != 0;
            bool prevUpper = std::isupper(prev) != 0;
            bool prevDigit = std::isdigit(prev) != 0;
            bool currUpper = std::isupper(c) != 0;
            bool currDigit = std::isdigit(c) != 0;
            bool nextLower = false;
            if(i + 1 < rawName.size()){
                nextLower = std::islower(static_cast<unsigned char>(rawName[i + 1])) != 0;
            }

            if((currUpper && prevLower) ||
               (currUpper && prevUpper && nextLower) ||
               (currDigit && !prevDigit) ||
               (!currDigit && prevDigit)){
                insertSpace = true;
            }
        }

        if(insertSpace){
            spaced.push_back(' ');
        }
        spaced.push_back(static_cast<char>(c));
    }

    std::string clean;
    clean.reserve(spaced.size());
    bool lastWasSpace = true;
    for(char c : spaced){
        if(c == ' '){
            if(!lastWasSpace){
                clean.push_back(' ');
                lastWasSpace = true;
            }
            continue;
        }
        clean.push_back(c);
        lastWasSpace = false;
    }
    if(!clean.empty() && clean.back() == ' '){
        clean.pop_back();
    }
    if(clean.empty()){
        return "Script";
    }

    // Title-case non-acronym words while preserving all-caps acronyms like FPS.
    std::string out;
    out.reserve(clean.size());
    size_t cursor = 0;
    while(cursor < clean.size()){
        size_t end = clean.find(' ', cursor);
        if(end == std::string::npos){
            end = clean.size();
        }
        std::string token = clean.substr(cursor, end - cursor);
        if(!token.empty()){
            bool hasUpper = false;
            bool hasLower = false;
            for(char ch : token){
                unsigned char uch = static_cast<unsigned char>(ch);
                hasUpper = hasUpper || (std::isupper(uch) != 0);
                hasLower = hasLower || (std::islower(uch) != 0);
            }
            if(hasLower || !hasUpper){
                token[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(token[0])));
                for(size_t i = 1; i < token.size(); ++i){
                    token[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(token[i])));
                }
            }
            if(!out.empty()){
                out.push_back(' ');
            }
            out += token;
        }
        cursor = end + 1;
    }

    return out.empty() ? std::string("Script") : out;
}

namespace {
    bool looksLikeColorField(const EffectPropertyData& field){
        auto containsColorHint = [](const std::string& value) -> bool{
            if(value.empty()){
                return false;
            }
            const std::string lower = StringUtils::ToLowerCase(value);
            return lower.find("color") != std::string::npos ||
                   lower.find("albedo") != std::string::npos ||
                   lower.find("tint") != std::string::npos;
        };

        return containsColorHint(field.key) ||
               containsColorHint(field.displayName) ||
               containsColorHint(field.uniformName);
    }

    const char* builtinEffectId(PostProcessingEffectKind kind){
        switch(kind){
            case PostProcessingEffectKind::DepthOfField: return "builtin/DepthOfField";
            case PostProcessingEffectKind::Bloom: return "builtin/Bloom";
            case PostProcessingEffectKind::LensFlare: return "builtin/LensFlare";
            case PostProcessingEffectKind::AutoExposure: return "builtin/AutoExposure";
            case PostProcessingEffectKind::AntiAliasing: return "builtin/AntiAliasing";
            case PostProcessingEffectKind::Loaded:
            default:
                return "";
        }
    }

    const char* builtinEffectDisplayName(PostProcessingEffectKind kind){
        switch(kind){
            case PostProcessingEffectKind::DepthOfField: return "Depth Of Field Effect";
            case PostProcessingEffectKind::Bloom: return "Bloom Effect";
            case PostProcessingEffectKind::LensFlare: return "Lens Flare Effect";
            case PostProcessingEffectKind::AutoExposure: return "Auto Exposure Effect";
            case PostProcessingEffectKind::AntiAliasing: return "Anti-Aliasing Effect";
            case PostProcessingEffectKind::Loaded:
            default:
                return "Loaded Effect";
        }
    }

    bool builtinEffectKindFromIdentifier(const std::string& identifier, PostProcessingEffectKind& outKind){
        const std::string lower = StringUtils::ToLowerCase(identifier);
        if(lower == "builtin/depthoffield"){
            outKind = PostProcessingEffectKind::DepthOfField;
            return true;
        }
        if(lower == "builtin/bloom"){
            outKind = PostProcessingEffectKind::Bloom;
            return true;
        }
        if(lower == "builtin/lensflare"){
            outKind = PostProcessingEffectKind::LensFlare;
            return true;
        }
        if(lower == "builtin/autoexposure"){
            outKind = PostProcessingEffectKind::AutoExposure;
            return true;
        }
        if(lower == "builtin/antialiasing"){
            outKind = PostProcessingEffectKind::AntiAliasing;
            return true;
        }
        return false;
    }

    struct AvailablePostEffectOption{
        PostProcessingEffectKind kind = PostProcessingEffectKind::Loaded;
        std::string identifier;
        std::string displayName;
        std::string sourceGroup;
        std::filesystem::path bundlePath;
    };

    std::string bundleAliasFromAssetRef(const std::string& assetRef){
        if(assetRef.empty() || !StringUtils::BeginsWith(assetRef, "@")){
            return std::string();
        }
        size_t slash = assetRef.find('/');
        if(slash == std::string::npos){
            return assetRef.substr(1);
        }
        if(slash <= 1){
            return std::string();
        }
        return assetRef.substr(1, slash - 1);
    }

    std::string effectAssetSourceGroup(const std::string& effectAssetRef){
        if(effectAssetRef.empty()){
            return "Assets";
        }
        if(StringUtils::BeginsWith(effectAssetRef, "@/") || effectAssetRef == "@"){
            return "Assets";
        }
        const std::string alias = bundleAliasFromAssetRef(effectAssetRef);
        return alias.empty() ? "Assets" : alias;
    }

    std::string effectNameFromPath(const std::filesystem::path& path){
        std::string name = path.filename().string();
        const std::string lower = StringUtils::ToLowerCase(name);
        static const std::string kSuffix = ".effect.asset";
        if(StringUtils::EndsWith(lower, kSuffix) && name.size() > kSuffix.size()){
            name = name.substr(0, name.size() - kSuffix.size());
        }
        return SanitizeEffectDisplayName(name);
    }

    std::vector<AvailablePostEffectOption> collectAvailablePostEffectOptions(){
        std::vector<AvailablePostEffectOption> options;

        auto addBuiltin = [&](PostProcessingEffectKind kind){
            AvailablePostEffectOption option;
            option.kind = kind;
            option.identifier = builtinEffectId(kind);
            option.displayName = builtinEffectDisplayName(kind);
            option.sourceGroup = "Builtin";
            options.push_back(std::move(option));
        };

        addBuiltin(PostProcessingEffectKind::DepthOfField);
        addBuiltin(PostProcessingEffectKind::Bloom);
        addBuiltin(PostProcessingEffectKind::LensFlare);
        addBuiltin(PostProcessingEffectKind::AutoExposure);
        addBuiltin(PostProcessingEffectKind::AntiAliasing);

        std::unordered_set<std::string> seenIdentifiers;
        for(const auto& option : options){
            seenIdentifiers.insert(StringUtils::ToLowerCase(option.identifier));
        }

        auto addLoadedOption = [&](const std::string& assetRef, const std::filesystem::path& bundlePath){
            const std::string key = StringUtils::ToLowerCase(assetRef);
            if(assetRef.empty() || seenIdentifiers.find(key) != seenIdentifiers.end()){
                return;
            }

            EffectAssetData assetData;
            if(!EffectAssetIO::LoadFromAssetRef(assetRef, assetData, nullptr)){
                return;
            }

            AvailablePostEffectOption option;
            option.kind = PostProcessingEffectKind::Loaded;
            option.identifier = assetRef;
            option.displayName = assetData.name.empty() ? effectNameFromPath(std::filesystem::path(assetRef)) : assetData.name;
            option.sourceGroup = effectAssetSourceGroup(assetRef);
            option.bundlePath = bundlePath;
            options.push_back(std::move(option));
            seenIdentifiers.insert(key);
        };

        const std::filesystem::path assetRoot = AssetDescriptorUtils::GetAssetRootPath();
        std::error_code ec;
        if(std::filesystem::exists(assetRoot, ec) && std::filesystem::is_directory(assetRoot, ec)){
            for(const auto& entry : std::filesystem::recursive_directory_iterator(
                    assetRoot,
                    std::filesystem::directory_options::skip_permission_denied,
                    ec)){
                if(ec){
                    break;
                }
                if(!entry.is_regular_file(ec) || !EffectAssetIO::IsEffectAssetPath(entry.path())){
                    continue;
                }
                addLoadedOption(AssetDescriptorUtils::AbsolutePathToAssetRef(entry.path()), {});
            }
        }

        std::unordered_set<std::string> seenBundlePaths;
        std::vector<std::filesystem::path> bundlePaths;
        auto collectBundles = [&](const std::filesystem::path& root, bool recursive){
            std::error_code walkEc;
            if(!std::filesystem::exists(root, walkEc) || !std::filesystem::is_directory(root, walkEc)){
                return;
            }

            if(recursive){
                for(const auto& entry : std::filesystem::recursive_directory_iterator(
                        root,
                        std::filesystem::directory_options::skip_permission_denied,
                        walkEc)){
                    if(walkEc){
                        break;
                    }
                    if(!entry.is_regular_file(walkEc) || !AssetBundle::IsBundlePath(entry.path())){
                        continue;
                    }
                    const std::string key = StringUtils::ToLowerCase(entry.path().lexically_normal().generic_string());
                    if(seenBundlePaths.insert(key).second){
                        bundlePaths.push_back(entry.path());
                    }
                }
            }else{
                for(const auto& entry : std::filesystem::directory_iterator(
                        root,
                        std::filesystem::directory_options::skip_permission_denied,
                        walkEc)){
                    if(walkEc){
                        break;
                    }
                    if(!entry.is_regular_file(walkEc) || !AssetBundle::IsBundlePath(entry.path())){
                        continue;
                    }
                    const std::string key = StringUtils::ToLowerCase(entry.path().lexically_normal().generic_string());
                    if(seenBundlePaths.insert(key).second){
                        bundlePaths.push_back(entry.path());
                    }
                }
            }
        };

        collectBundles(std::filesystem::path(File::GetCWD()), false);
        collectBundles(assetRoot, true);

        for(const auto& bundlePath : bundlePaths){
            auto bundle = std::make_shared<AssetBundle>();
            if(!bundle || !bundle->open(bundlePath, nullptr)){
                continue;
            }
            const std::string aliasToken = bundle->aliasToken();
            for(const auto& entry : bundle->getEntries()){
                if(entry.kind == "directory"){
                    continue;
                }
                const std::filesystem::path entryPath(entry.path);
                if(!EffectAssetIO::IsEffectAssetPath(entryPath)){
                    continue;
                }
                const std::string assetRef = aliasToken + "/" + entry.path;
                addLoadedOption(assetRef, bundlePath);
            }
        }

        std::sort(options.begin(), options.end(), [](const AvailablePostEffectOption& a, const AvailablePostEffectOption& b){
            if(a.sourceGroup == b.sourceGroup){
                return a.displayName < b.displayName;
            }
            if(a.sourceGroup == "Builtin"){
                return true;
            }
            if(b.sourceGroup == "Builtin"){
                return false;
            }
            return a.sourceGroup < b.sourceGroup;
        });
        return options;
    }

    template<typename TDof>
    Graphics::PostProcessing::PPostProcessingEffect buildDepthOfFieldRuntimeEffect(TDof& data,
                                                                                   bool enabled,
                                                                                   const CameraSettings& settings){
        if(!enabled){
            return nullptr;
        }
        if(!data.runtimeEffect){
            data.runtimeEffect = DepthOfFieldEffect::New();
        }
        data.runtimeEffect->adaptiveFocus = data.adaptiveFocus;
        data.runtimeEffect->focusUv = Math3D::Vec2(0.5f, 0.5f);
        data.runtimeEffect->focusDistance = Math3D::Max(0.01f, data.focusDistance);
        data.runtimeEffect->focusRange = Math3D::Max(0.001f, data.focusRange);
        data.runtimeEffect->focusBandWidth = Math3D::Clamp(data.focusBandWidth, 0.05f, 4.0f);
        data.runtimeEffect->blurRamp = Math3D::Clamp(data.blurRamp, 0.05f, 6.0f);
        data.runtimeEffect->blurDistanceLerp = Math3D::Clamp(data.blurDistanceLerp, 0.0f, 1.0f);
        data.runtimeEffect->fallbackFocusRange = Math3D::Max(0.001f, data.fallbackFocusRange);
        data.runtimeEffect->blurStrength = Math3D::Clamp(data.blurStrength, 0.0f, 1.5f);
        data.runtimeEffect->maxBlurPx = Math3D::Clamp(data.maxBlurPx, 0.0f, 16.0f);
        data.runtimeEffect->sampleCount = Math3D::Clamp(data.sampleCount, 1, 8);
        data.runtimeEffect->debugCocView = data.debugCocView;
        float dofNear = settings.nearPlane;
        float dofFar = settings.farPlane;
        Camera::SanitizePerspectivePlanes(dofNear, dofFar);
        data.runtimeEffect->nearPlane = dofNear;
        data.runtimeEffect->farPlane = Math3D::Max(data.runtimeEffect->nearPlane + 0.001f, dofFar);
        return data.runtimeEffect;
    }

    template<typename TDof>
    void drawDepthOfFieldEffectFields(TDof& data){
        EditorPropertyUI::Checkbox("Adaptive Focus", &data.adaptiveFocus);
        if(data.adaptiveFocus){
            ImGui::TextDisabled("Uses the center screen depth as the focus target.");
            EditorPropertyUI::Checkbox("Debug Adaptive Focus Ray", &data.adaptiveFocusDebugDraw);
            EditorPropertyUI::DragFloat("Fallback Focus Distance", &data.focusDistance, 0.1f, 0.01f, 10000.0f, "%.2f");
            EditorPropertyUI::DragFloat("Adaptive Focus Range", &data.focusRange, 0.05f, 0.001f, 10000.0f, "%.3f");
            EditorPropertyUI::DragFloat("Fallback Focus Range", &data.fallbackFocusRange, 0.05f, 0.001f, 10000.0f, "%.3f");
        }else{
            EditorPropertyUI::DragFloat("Focus Distance", &data.focusDistance, 0.1f, 0.01f, 10000.0f, "%.2f");
            EditorPropertyUI::DragFloat("Focus Range", &data.focusRange, 0.05f, 0.001f, 10000.0f, "%.3f");
        }
        EditorPropertyUI::SliderFloat("Focus Band Width", &data.focusBandWidth, 0.25f, 3.0f, "%.2f");
        EditorPropertyUI::SliderFloat("Blur Ramp", &data.blurRamp, 0.25f, 4.0f, "%.2f");
        EditorPropertyUI::SliderFloat("Blur Distance Lerp", &data.blurDistanceLerp, 0.0f, 1.0f, "%.2f");
        EditorPropertyUI::SliderFloat("Blur Strength", &data.blurStrength, 0.0f, 1.5f, "%.2f");
        EditorPropertyUI::SliderFloat("Max Blur (Px)", &data.maxBlurPx, 0.0f, 16.0f, "%.1f");
        EditorPropertyUI::SliderInt("Samples", &data.sampleCount, 1, 8);
        EditorPropertyUI::Checkbox("Debug CoC View", &data.debugCocView);
        if(data.adaptiveFocus && data.runtimeEffect){
            ImGui::TextDisabled("Live Focus Distance: %.2f", data.runtimeEffect->getResolvedFocusDistance());
            ImGui::TextDisabled("Live Focus Range: %.3f", data.runtimeEffect->getResolvedFocusRange());
        }
    }

    template<typename TBloom>
    Graphics::PostProcessing::PPostProcessingEffect buildBloomRuntimeEffect(TBloom& data,
                                                                            bool enabled,
                                                                            const CameraSettings& settings){
        (void)settings;
        if(!enabled){
            return nullptr;
        }
        if(!data.runtimeEffect){
            data.runtimeEffect = BloomEffect::New();
        }
        data.runtimeEffect->threshold = Math3D::Clamp(data.threshold, 0.0f, 2.0f);
        data.runtimeEffect->softKnee = Math3D::Clamp(data.softKnee, 0.01f, 1.0f);
        data.runtimeEffect->intensity = Math3D::Clamp(data.intensity, 0.0f, 4.0f);
        data.runtimeEffect->radiusPx = Math3D::Clamp(data.radiusPx, 0.5f, 24.0f);
        data.runtimeEffect->sampleCount = Math3D::Clamp(data.sampleCount, 4, 12);
        data.runtimeEffect->adaptiveBloom = data.adaptiveBloom;
        data.runtimeEffect->autoExposureIntensityScale = 1.0f;
        data.runtimeEffect->autoExposureThresholdScale = 1.0f;
        data.liveThreshold = data.runtimeEffect->threshold;
        data.liveIntensity = data.runtimeEffect->intensity;
        data.liveAutoExposureDriven = false;
        data.runtimeEffect->tint = Math3D::Vec3(
            Math3D::Clamp(data.tint.x, 0.0f, 4.0f),
            Math3D::Clamp(data.tint.y, 0.0f, 4.0f),
            Math3D::Clamp(data.tint.z, 0.0f, 4.0f)
        );
        return data.runtimeEffect;
    }

    template<typename TBloom>
    void drawBloomEffectFields(TBloom& data){
        EditorPropertyUI::Checkbox("Adaptive Bloom", &data.adaptiveBloom);
        if(data.adaptiveBloom){
            ImGui::TextDisabled("Adjusts bloom response only (separate from camera auto exposure).");
        }
        EditorPropertyUI::SliderFloat("Threshold", &data.threshold, 0.0f, 2.0f, "%.2f");
        EditorPropertyUI::SliderFloat("Soft Knee", &data.softKnee, 0.01f, 1.0f, "%.2f");
        EditorPropertyUI::SliderFloat("Intensity", &data.intensity, 0.0f, 4.0f, "%.2f");
        EditorPropertyUI::SliderFloat("Radius (Px)", &data.radiusPx, 0.5f, 24.0f, "%.1f");
        EditorPropertyUI::SliderInt("Samples", &data.sampleCount, 4, 12);
        EditorPropertyUI::ColorEdit3("Tint", &data.tint.x);
        if(data.liveAutoExposureDriven){
            ImGui::Separator();
            ImGui::TextDisabled("Live Threshold (AE): %.3f", data.liveThreshold);
            ImGui::TextDisabled("Live Intensity (AE): %.3f", data.liveIntensity);
        }
    }

    template<typename TLensFlare>
    Graphics::PostProcessing::PPostProcessingEffect buildLensFlareRuntimeEffect(TLensFlare& data,
                                                                                bool enabled,
                                                                                const CameraSettings& settings){
        (void)settings;
        if(!enabled){
            return nullptr;
        }
        if(!data.runtimeEffect){
            data.runtimeEffect = std::make_shared<LensFlareEffect>();
        }
        return data.runtimeEffect;
    }

    template<typename TLensFlare>
    void drawLensFlareEffectFields(TLensFlare& data){
        (void)data;
        ImGui::TextDisabled("Uses Flare Asset assignments from active Light Components.");
        ImGui::TextDisabled("Bright highlights inherit glare settings from those flare assets.");
    }

    template<typename TAutoExposure>
    Graphics::PostProcessing::PPostProcessingEffect buildAutoExposureRuntimeEffect(TAutoExposure& data,
                                                                                   bool enabled,
                                                                                   const CameraSettings& settings){
        (void)settings;
        if(!enabled){
            if(data.runtimeEffect){
                data.runtimeEffect->resetAdaptation();
            }
            return nullptr;
        }
        if(!data.runtimeEffect){
            data.runtimeEffect = AutoExposureEffect::New();
        }
        data.runtimeEffect->minExposure = Math3D::Clamp(data.minExposure, 0.01f, 64.0f);
        data.runtimeEffect->maxExposure = Math3D::Clamp(data.maxExposure, data.runtimeEffect->minExposure, 64.0f);
        data.runtimeEffect->exposureCompensation = Math3D::Clamp(data.exposureCompensation, -8.0f, 8.0f);
        data.runtimeEffect->adaptationSpeedUp = Math3D::Clamp(data.adaptationSpeedUp, 0.01f, 20.0f);
        data.runtimeEffect->adaptationSpeedDown = Math3D::Clamp(data.adaptationSpeedDown, 0.01f, 20.0f);
        return data.runtimeEffect;
    }

    template<typename TAutoExposure, typename TBloom>
    void applyAutoExposureBloomCoupling(TAutoExposure& autoExposure,
                                        bool autoExposureEnabled,
                                        TBloom& bloom,
                                        bool bloomEnabled){
        if(!autoExposureEnabled || !bloomEnabled){
            return;
        }
        if(!bloom.adaptiveBloom){
            bloom.adaptiveBloom = true;
        }
        if(!bloom.runtimeEffect){
            return;
        }

        float exposure = 1.0f;
        if(autoExposure.runtimeEffect){
            exposure = Math3D::Clamp(autoExposure.runtimeEffect->getCurrentExposure(), 0.01f, 64.0f);
        }

        float intensityBias = Math3D::Clamp(std::pow(1.0f / exposure, 0.35f), 0.78f, 1.28f);
        float thresholdBias = Math3D::Clamp(std::pow(exposure, 0.22f), 0.84f, 1.22f);

        bloom.runtimeEffect->adaptiveBloom = true;
        bloom.runtimeEffect->autoExposureIntensityScale = intensityBias;
        bloom.runtimeEffect->autoExposureThresholdScale = thresholdBias;
        bloom.liveIntensity = Math3D::Clamp(bloom.runtimeEffect->intensity * intensityBias, 0.0f, 6.0f);
        bloom.liveThreshold = Math3D::Clamp(bloom.runtimeEffect->threshold * thresholdBias, 0.0f, 4.0f);
        bloom.liveAutoExposureDriven = true;
    }

    template<typename TAutoExposure>
    void drawAutoExposureEffectFields(TAutoExposure& data){
        if(EditorPropertyUI::DragFloat("Min Exposure", &data.minExposure, 0.01f, 0.01f, 16.0f, "%.2f")){
            data.minExposure = Math3D::Clamp(data.minExposure, 0.01f, 16.0f);
            data.maxExposure = Math3D::Max(data.maxExposure, data.minExposure);
        }
        if(EditorPropertyUI::DragFloat("Max Exposure", &data.maxExposure, 0.01f, 0.01f, 16.0f, "%.2f")){
            data.maxExposure = Math3D::Clamp(data.maxExposure, data.minExposure, 16.0f);
        }
        EditorPropertyUI::SliderFloat("Exposure Compensation (EV)", &data.exposureCompensation, -4.0f, 4.0f, "%.2f");
        EditorPropertyUI::SliderFloat("Adaptation Speed Up", &data.adaptationSpeedUp, 0.05f, 8.0f, "%.2f");
        EditorPropertyUI::SliderFloat("Adaptation Speed Down", &data.adaptationSpeedDown, 0.05f, 8.0f, "%.2f");
        if(data.runtimeEffect){
            ImGui::TextDisabled("Current Exposure: %.3f", data.runtimeEffect->getCurrentExposure());
        }
    }

    template<typename TAntiAliasing>
    Graphics::PostProcessing::PPostProcessingEffect buildAntiAliasingRuntimeEffect(TAntiAliasing& data,
                                                                                   bool enabled,
                                                                                   const CameraSettings& settings){
        (void)settings;
        if(!enabled || data.preset == AntiAliasingPreset::Off){
            return nullptr;
        }
        if(!data.runtimeEffect){
            data.runtimeEffect = FXAAEffect::New();
        }
        data.runtimeEffect->preset = data.preset;
        return data.runtimeEffect;
    }

    template<typename TAntiAliasing>
    void drawAntiAliasingEffectFields(TAntiAliasing& data){
        const char* options[] = {
            "Off",
            "FXAA - Low",
            "FXAA - Medium",
            "FXAA - High"
        };
        int item = static_cast<int>(data.preset);
        if(EditorPropertyUI::Combo("Preset", &item, options, IM_ARRAYSIZE(options))){
            data.preset = static_cast<AntiAliasingPreset>(item);
        }
    }

    bool drawLoadedEffectPropertyEditor(EffectPropertyData& property, const std::string& idSuffix){
        std::string fieldName = property.displayName;
        if(fieldName.empty()){
            fieldName = !property.key.empty() ? property.key : property.uniformName;
        }
        if(fieldName.empty()){
            fieldName = "Property";
        }
        const std::string label = fieldName + "##" + idSuffix + "_" + property.key;

        switch(property.type){
            case EffectPropertyType::Float:
                return EditorPropertyUI::DragFloat(label.c_str(), &property.floatValue, 0.01f);
            case EffectPropertyType::Int:
                return EditorPropertyUI::DragInt(label.c_str(), &property.intValue, 1.0f);
            case EffectPropertyType::Bool:
                return EditorPropertyUI::Checkbox(label.c_str(), &property.boolValue);
            case EffectPropertyType::Vec2:
                return EditorPropertyUI::DragFloat2(label.c_str(), &property.vec2Value.x, 0.01f);
            case EffectPropertyType::Vec3:
                if(looksLikeColorField(property)){
                    return EditorPropertyUI::ColorEdit3(label.c_str(), &property.vec3Value.x);
                }
                return EditorPropertyUI::DragFloat3(label.c_str(), &property.vec3Value.x, 0.01f);
            case EffectPropertyType::Vec4:
                if(looksLikeColorField(property)){
                    return EditorPropertyUI::ColorEdit4(label.c_str(), &property.vec4Value.x);
                }
                return EditorPropertyUI::DragFloat4(label.c_str(), &property.vec4Value.x, 0.01f);
            case EffectPropertyType::Texture2D:{
                bool changed = EditorAssetUI::DrawAssetDropInput(
                    label.c_str(),
                    property.textureAssetRef,
                    {EditorAssetUI::AssetKind::Image}
                );
                std::string previewId = idSuffix + "_effect_" + property.key + "_preview";
                drawTextureOutputSmall(property.texturePtr, !property.textureAssetRef.empty(), previewId.c_str());
                return changed;
            }
            default:
                break;
        }
        return false;
    }

    void drawSsaoSettingsFields(SSAOComponent& data){
        EditorPropertyUI::Checkbox("Enabled", &data.enabled);
        EditorPropertyUI::SliderFloat("Sample Radius (Px)", &data.radiusPx, 0.25f, 8.0f, "%.2f");
        EditorPropertyUI::SliderFloat("Sample Radius (World)", &data.depthRadius, 0.00001f, 0.5f, "%.5f");
        EditorPropertyUI::SliderFloat("Bias", &data.bias, 0.0f, 0.02f, "%.4f");
        EditorPropertyUI::SliderFloat("AO Strength", &data.intensity, 0.0f, 10.0f, "%.2f");
        EditorPropertyUI::SliderFloat("GI Boost", &data.giBoost, 0.0f, 1.0f, "%.2f");
        EditorPropertyUI::SliderInt("Samples", &data.sampleCount, 4, 64);
        EditorPropertyUI::SliderFloat("Blur Radius (Px)", &data.blurRadiusPx, 0.5f, 6.0f, "%.2f");
        EditorPropertyUI::SliderFloat("Blur Sharpness", &data.blurSharpness, 0.25f, 4.0f, "%.2f");
        static const char* kSsaoDebugViews[] = { "Composite", "Combined AO", "SSAO Raw", "Material AO", "GI" };
        EditorPropertyUI::Combo("Debug View", &data.debugView, kSsaoDebugViews, IM_ARRAYSIZE(kSsaoDebugViews));
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

    void copyEnvironmentDataToAsset(const EnvironmentComponent& component, EnvironmentAssetData& data){
        data.name = component.environmentAssetRef.empty()
            ? std::string()
            : std::filesystem::path(component.environmentAssetRef).filename().string();
        data.skyboxAssetRef = StringUtils::Trim(component.skyboxAssetRef);
        data.settings = component.settings;
        sanitizeEnvironmentSettings(data.settings);
    }

    void applyEnvironmentAssetToComponent(const EnvironmentAssetData& data, EnvironmentComponent& component){
        component.settings = data.settings;
        sanitizeEnvironmentSettings(component.settings);
        component.skyboxAssetRef = StringUtils::Trim(data.skyboxAssetRef);
        component.loadedSkyboxAssetRef.clear();
        component.runtimeSkyBox.reset();
    }

    void drawEnvironmentSettingsFields(EnvironmentSettings& settings){
        EditorPropertyUI::ColorEdit3("Ambient Color", &settings.ambientColor.x);
        EditorPropertyUI::SliderFloat("Ambient Intensity", &settings.ambientIntensity, 0.0f, 8.0f, "%.2f");

        ImGui::Separator();
        EditorPropertyUI::Checkbox("Fog Enabled", &settings.fogEnabled);
        if(settings.fogEnabled){
            EditorPropertyUI::ColorEdit3("Fog Color", &settings.fogColor.x);
            EditorPropertyUI::DragFloat("Fog Start", &settings.fogStart, 0.1f, 0.0f, 20000.0f, "%.2f");
            EditorPropertyUI::DragFloat("Fog Stop", &settings.fogStop, 0.1f, 0.0f, 20000.0f, "%.2f");
            EditorPropertyUI::DragFloat("Fog End", &settings.fogEnd, 0.1f, 0.0f, 20000.0f, "%.2f");
        }

        ImGui::Separator();
        EditorPropertyUI::Checkbox("Use Procedural Sky", &settings.useProceduralSky);
        if(settings.useProceduralSky){
            float sunDir[3] = {settings.sunDirection.x, settings.sunDirection.y, settings.sunDirection.z};
            if(EditorPropertyUI::DragFloat3("Sun Direction", sunDir, 0.01f, -1.0f, 1.0f, "%.3f")){
                settings.sunDirection = Math3D::Vec3(sunDir[0], sunDir[1], sunDir[2]);
            }
            EditorPropertyUI::SliderFloat("Rayleigh Strength", &settings.rayleighStrength, 0.0f, 8.0f, "%.3f");
            EditorPropertyUI::SliderFloat("Mie Strength", &settings.mieStrength, 0.0f, 8.0f, "%.3f");
            EditorPropertyUI::SliderFloat("Mie Anisotropy", &settings.mieAnisotropy, 0.0f, 0.99f, "%.3f");
        }
    }

    bool canRemoveEditorComponent(const IEditorCompatibleComponent* component){
        if(!component){
            return false;
        }
        if(dynamic_cast<const TransformComponent*>(component)){
            return false;
        }
        if(dynamic_cast<const EntityPropertiesComponent*>(component)){
            return false;
        }
        return true;
    }

    bool removeEditorComponent(IEditorCompatibleComponent* component, NeoECS::NeoECS* ecsPtr){
        if(!component || !ecsPtr || !canRemoveEditorComponent(component)){
            return false;
        }
        auto* manager = ecsPtr->getComponentManager();
        NeoECS::ECSEntity* entity = component->getParentEntity();
        if(!manager || !entity){
            return false;
        }

        if(dynamic_cast<MeshRendererComponent*>(component)){ manager->removeECSComponent<MeshRendererComponent>(entity); return true; }
        if(dynamic_cast<LightComponent*>(component)){ manager->removeECSComponent<LightComponent>(entity); return true; }
        if(dynamic_cast<BoundsComponent*>(component)){ manager->removeECSComponent<BoundsComponent>(entity); return true; }
        if(dynamic_cast<ColliderComponent*>(component)){ manager->removeECSComponent<ColliderComponent>(entity); return true; }
        if(dynamic_cast<RigidBodyComponent*>(component)){ manager->removeECSComponent<RigidBodyComponent>(entity); return true; }
        if(dynamic_cast<CameraComponent*>(component)){ manager->removeECSComponent<CameraComponent>(entity); return true; }
        if(dynamic_cast<SkyboxComponent*>(component)){ manager->removeECSComponent<SkyboxComponent>(entity); return true; }
        if(dynamic_cast<EnvironmentComponent*>(component)){ manager->removeECSComponent<EnvironmentComponent>(entity); return true; }
        if(dynamic_cast<SSAOComponent*>(component)){ manager->removeECSComponent<SSAOComponent>(entity); return true; }
        if(dynamic_cast<PostProcessingStackComponent*>(component)){ manager->removeECSComponent<PostProcessingStackComponent>(entity); return true; }
        if(dynamic_cast<DepthOfFieldComponent*>(component)){ manager->removeECSComponent<DepthOfFieldComponent>(entity); return true; }
        if(dynamic_cast<BloomComponent*>(component)){ manager->removeECSComponent<BloomComponent>(entity); return true; }
        if(dynamic_cast<LensFlareComponent*>(component)){ manager->removeECSComponent<LensFlareComponent>(entity); return true; }
        if(dynamic_cast<AutoExposureComponent*>(component)){ manager->removeECSComponent<AutoExposureComponent>(entity); return true; }
        if(dynamic_cast<AntiAliasingComponent*>(component)){ manager->removeECSComponent<AntiAliasingComponent>(entity); return true; }
        if(dynamic_cast<ScriptComponent*>(component)){ manager->removeECSComponent<ScriptComponent>(entity); return true; }
        return false;
    }

    bool beginEditorComponentSection(IEditorCompatibleComponent* component,
                                     const char* label,
                                     ImGuiTreeNodeFlags flags = 0,
                                     NeoECS::NeoECS* ecsPtr = nullptr){
        if(!component || !label){
            return false;
        }

        ImGui::PushID(component);
        bool* enabledState = component->getEditorEnabledState();
        bool* hiddenState = component->getEditorHiddenState();
        const bool isEnabled = enabledState ? *enabledState : true;
        const bool isHidden = hiddenState ? *hiddenState : false;

        std::string headerLabel = label;
        if(!isEnabled){
            headerLabel += " (Disabled)";
        }
        if(isHidden){
            headerLabel += " (Hidden)";
        }

        if(!isEnabled){
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        bool open = ImGui::CollapsingHeader(headerLabel.c_str(), flags);
        const ImGuiID headerId = ImGui::GetItemID();
        if(!isEnabled){
            ImGui::PopStyleColor();
        }

        bool removeRequested = false;
        const bool allowRemove = canRemoveEditorComponent(component);
        if(ImGui::BeginPopupContextItem("##ComponentHeaderContext")){
            if(ImGui::MenuItem("Expand", nullptr, false, !open)){
                ImGui::GetStateStorage()->SetInt(headerId, 1);
                open = true;
            }
            if(ImGui::MenuItem("Collapse", nullptr, false, open)){
                ImGui::GetStateStorage()->SetInt(headerId, 0);
                open = false;
            }
            ImGui::Separator();
            if(hiddenState){
                const char* hideLabel = isHidden ? "Show" : "Hide";
                if(ImGui::MenuItem(hideLabel)){
                    *hiddenState = !(*hiddenState);
                }
            }
            if(enabledState){
                const char* activeLabel = isEnabled ? "Deactivate" : "Activate";
                if(ImGui::MenuItem(activeLabel)){
                    *enabledState = !(*enabledState);
                }
            }
            if(!hiddenState && !enabledState){
                ImGui::TextDisabled("No toggle options");
            }
            ImGui::Separator();
            if(ImGui::MenuItem("Remove Component", nullptr, false, allowRemove)){
                removeRequested = true;
            }
            ImGui::EndPopup();
        }

        if(removeRequested){
            if(!removeEditorComponent(component, ecsPtr)){
                LogBot.Log(LOG_WARN, "Failed to remove component '%s' from entity.", label);
            }
            ImGui::PopID();
            return false;
        }

        if(open){
            bool drewStateControls = false;
            if(enabledState){
                ImGui::Checkbox("Enabled##ComponentEnabled", enabledState);
                drewStateControls = true;
            }
            if(hiddenState){
                if(drewStateControls){
                    ImGui::SameLine();
                }
                ImGui::Checkbox("Hidden##ComponentHidden", hiddenState);
                drewStateControls = true;
            }
            if(drewStateControls){
                ImGui::Separator();
            }
        }

        ImGui::PopID();
        return open;
    }
}

void EntityPropertiesComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Entity Properties Component", 0, ecsPtr)){
        return;
    }

    EditorPropertyUI::Checkbox("Ignore Raycast Hit", &ignoreRaycastHit);
}

void TransformComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Transform Component", ImGuiTreeNodeFlags_DefaultOpen, ecsPtr)){
        return;
    }

    auto transform = this;

    Math3D::Vec3 pos = transform->local.position;
    Math3D::Vec3 rot = transform->local.rotation.ToEuler();
    Math3D::Vec3 scale = transform->local.scale;

    if(EditorPropertyUI::DragFloat3("Position", &pos.x, 0.1f)){
        transform->local.position = pos;
    }

    if(EditorPropertyUI::DragFloat3("Rotation", &rot.x, 0.5f)){
        transform->local.setRotation(rot);
    }

    if(EditorPropertyUI::DragFloat3("Scale", &scale.x, 0.1f)){
        transform->local.scale = scale;
    }
}

void MeshRendererComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Mesh Renderer Component", ImGuiTreeNodeFlags_DefaultOpen, ecsPtr)){
        return;
    }
    auto renderer = this;

        ImGui::Separator();
        ImGui::TextUnformatted("Mesh Renderer");
        EditorPropertyUI::Checkbox("Visible", &renderer->visible);
        EditorPropertyUI::Checkbox("Backface Cull", &renderer->enableBackfaceCulling);
        drawModelSelectionUI(renderer, "mesh_renderer_model_asset");

        const bool deferredActive = (GameEngine::Engine &&
                                     GameEngine::Engine->getRenderStrategy() == EngineRenderStrategy::Deferred);
        int incompatibleCount = 0;
        int materialCount = 0;
        if(renderer->model){
            const auto& parts = renderer->model->getParts();
            for(const auto& part : parts){
                if(!part || !part->material) continue;
                materialCount++;
                if(!isDeferredCompatibleMaterial(part->material)){
                    incompatibleCount++;
                }
            }
        }else if(renderer->material){
            materialCount = 1;
            if(!isDeferredCompatibleMaterial(renderer->material)){
                incompatibleCount = 1;
            }
        }

        if(deferredActive && incompatibleCount > 0){
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "[!] Deferred Compatibility Warning");
            ImGui::TextWrapped("%d/%d material(s) are incompatible with deferred and will render as MAGENTA.", incompatibleCount, materialCount);
        }

        ImGui::Separator();
        if(!renderer->model){
            ImGui::TextDisabled("Render Mode: Single Mesh + Material");
            if(renderer->mesh && renderer->material){
                if(ImGui::Button("Convert To Model (Part 0)")){
                    const std::string singleMaterialAssetRef = renderer->materialAssetRef;
                    const bool singleMaterialOverride = renderer->materialOverridesSource;
                    auto newModel = Model::Create();
                    auto part = std::make_shared<ModelPart>();
                    part->mesh = renderer->mesh;
                    part->material = renderer->material;
                    newModel->addPart(part);
                    renderer->model = newModel;
                    renderer->mesh.reset();
                    renderer->material.reset();
                    renderer->modelAssetRef.clear();
                    renderer->modelSourceRef.clear();
                    renderer->modelForceSmoothNormals = 0;
                    renderer->materialAssetRef.clear();
                    renderer->materialOverridesSource = false;
                    renderer->modelPartMaterialAssetRefs.clear();
                    renderer->modelPartMaterialAssetRefs.resize(1);
                    renderer->modelPartMaterialAssetRefs[0] = singleMaterialAssetRef;
                    renderer->modelPartMaterialOverrides.clear();
                    renderer->modelPartMaterialOverrides.resize(1, 0);
                    renderer->modelPartMaterialOverrides[0] = singleMaterialOverride ? 1 : 0;
                }
            }

            if(ImGui::Button("Create Empty Model")){
                renderer->model = Model::Create();
                renderer->mesh.reset();
                renderer->material.reset();
                renderer->modelAssetRef.clear();
                renderer->modelSourceRef.clear();
                renderer->modelForceSmoothNormals = 0;
                renderer->materialAssetRef.clear();
                renderer->materialOverridesSource = false;
                renderer->modelPartMaterialAssetRefs.clear();
                renderer->modelPartMaterialOverrides.clear();
            }

            ImGui::Spacing();
            ImGui::TextUnformatted("Material");
            if(!renderer->materialAssetRef.empty()){
                ImGui::TextDisabled("Material Source: %s", renderer->materialAssetRef.c_str());
            }
            if(drawMaterialSelectionUI(renderer->material, "single_material_select", &renderer->materialAssetRef)){
                renderer->materialOverridesSource = false;
            }
            if(renderer->material){
                ImGui::PushID(renderer->material.get());
                if(drawMaterialValueFields(renderer->material, "single_material") &&
                   !renderer->materialAssetRef.empty()){
                    renderer->materialOverridesSource = true;
                }
                ImGui::PopID();
            }else{
                ImGui::TextDisabled("No material assigned.");
            }
            return;
        }

        ImGui::Text("Render Mode: Model (%zu parts)", renderer->model->getParts().size());
        if(ImGui::Button("Add Model Part")){
            auto defaultMat = MaterialDefaults::ColorMaterial::Create(Color::MAGENTA);
            auto newPart = ModelPartPrefabs::MakeBox(0.5f, 0.5f, 0.5f, defaultMat);
            if(newPart){
                renderer->model->addPart(newPart);
            }
        }
        ensureModelPartMaterialRefCount(renderer);

        const auto& parts = renderer->model->getParts();
        for(size_t i = 0; i < parts.size(); ++i){
            const auto& part = parts[i];
            ImGui::PushID(static_cast<int>(i));

            std::string partLabel = "Part " + std::to_string(i);
            bool open = ImGui::TreeNodeEx(partLabel.c_str(), ImGuiTreeNodeFlags_None);
            if(open){
                if(!part){
                    ImGui::TextDisabled("Part pointer is null.");
                    ImGui::TreePop();
                    ImGui::PopID();
                    continue;
                }

                EditorPropertyUI::Checkbox("Visible", &part->visible);
                EditorPropertyUI::Checkbox("Hide In ECS Tree", &part->hideInEditorTree);

                ImGui::Text("Mesh: %s", part->mesh ? "Assigned" : "None");
                if(!part->material){
                    ImGui::TextDisabled("Material: None");
                }else{
                    ImGui::Text("Material: Assigned");
                    if(deferredActive && !isDeferredCompatibleMaterial(part->material)){
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "[!] Incompatible With Deferred");
                    }
                }

                ImGui::Separator();
                drawPartTransformEditor(part->localTransform);

                if(part->material){
                    ImGui::Separator();
                    ImGui::TextUnformatted("Part Material");
                    if(i < renderer->modelPartMaterialAssetRefs.size() &&
                       !renderer->modelPartMaterialAssetRefs[i].empty()){
                        ImGui::TextDisabled("Source: %s", renderer->modelPartMaterialAssetRefs[i].c_str());
                    }
                    char idSuffix[64] = {};
                    std::snprintf(idSuffix, sizeof(idSuffix), "part_%zu_%p", i, part.get());
                    std::string selectSuffix = std::string(idSuffix) + "_select";
                    if(drawMaterialSelectionUI(
                        part->material,
                        selectSuffix.c_str(),
                        (i < renderer->modelPartMaterialAssetRefs.size())
                            ? &renderer->modelPartMaterialAssetRefs[i]
                            : nullptr
                    )){
                        ensureModelPartMaterialRefCount(renderer);
                        renderer->modelPartMaterialOverrides[i] =
                            (i < renderer->modelPartMaterialAssetRefs.size() &&
                             !renderer->modelPartMaterialAssetRefs[i].empty()) ? 0 : 1;
                    }
                    if(drawMaterialValueFields(part->material, idSuffix)){
                        ensureModelPartMaterialRefCount(renderer);
                        renderer->modelPartMaterialOverrides[i] = 1;
                    }
                }else{
                    ImGui::Separator();
                    std::string emptySelectSuffix = std::string("part_empty_") + std::to_string(i);
                    if(drawMaterialSelectionUI(
                        part->material,
                        emptySelectSuffix.c_str(),
                        (i < renderer->modelPartMaterialAssetRefs.size())
                            ? &renderer->modelPartMaterialAssetRefs[i]
                            : nullptr
                    )){
                        ensureModelPartMaterialRefCount(renderer);
                        renderer->modelPartMaterialOverrides[i] =
                            (i < renderer->modelPartMaterialAssetRefs.size() &&
                             !renderer->modelPartMaterialAssetRefs[i].empty()) ? 0 : 1;
                    }
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }
}

void LightComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    if(!beginEditorComponentSection(this, "Light Component", ImGuiTreeNodeFlags_DefaultOpen, ecsPtr)){
        return;
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Light");

    auto self = this;
    auto entity = self->getParentEntity();
    if(!ecsPtr || !scene || !entity){
        return;
    }
    auto components = ecsPtr->getComponentManager();

        static std::unordered_set<std::string> migratedLightSyncTransform;
        static std::unordered_set<std::string> migratedLightDefaults;
        auto makeMigrationKey = [&](const std::string& id) -> std::string {
            uintptr_t sceneId = reinterpret_cast<uintptr_t>(scene.get());
            return std::to_string(sceneId) + ":" + id;
        };

        auto getWorldLightBasis = [&](Math3D::Vec3& outPos, Math3D::Vec3& outForward) -> bool {
            auto* tx = components->getECSComponent<TransformComponent>(entity);
            if(!tx){
                return false;
            }
            Math3D::Mat4 world = scene->getWorldMatrix(entity, components);
            outPos = world.getPosition();
            Math3D::Vec3 forward = Math3D::Transform::transformPoint(world, Math3D::Vec3(0,0,1)) - outPos;
            if(!std::isfinite(forward.x) || !std::isfinite(forward.y) || !std::isfinite(forward.z) ||
               forward.length() <= 0.0001f){
                outForward = Math3D::Vec3(0, -1, 0);
            }else{
                outForward = forward.normalize();
            }
            return true;
        };
        auto syncLightFromTransform = [&]() {
            Math3D::Vec3 worldPos;
            Math3D::Vec3 worldForward;
            if(!getWorldLightBasis(worldPos, worldForward)){
                return;
            }
            if(self->syncTransform){
                self->light.position = worldPos;
            }
            if(self->syncDirection && self->light.type != LightType::POINT){
                self->light.direction = worldForward;
            }
        };
        auto ensurePointLightBounds = [&](NeoECS::ECSEntity* target, float radius){
            if(!target){
                return;
            }
            auto* manager = ecsPtr->getComponentManager();
            if(auto* bounds = manager->getECSComponent<BoundsComponent>(target)){
                bounds->type = BoundsType::Sphere;
                bounds->radius = radius;
            }else{
                auto* ctx = ecsPtr->getContext();
                std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(ctx, target));
                if(wrapper && wrapper->addComponent<BoundsComponent>()){
                    if(auto* newBounds = manager->getECSComponent<BoundsComponent>(target)){
                        newBounds->type = BoundsType::Sphere;
                        newBounds->radius = radius;
                    }
                }
            }
        };
        auto defaultShadowRangeForType = [&]() -> float {
            if(self->light.type == LightType::DIRECTIONAL){
                return 300.0f;
            }
            return Math3D::Max(self->light.range, 1.0f);
        };
        auto sanitizeCascadeLambda = [&]() {
            if(!std::isfinite(self->light.cascadeLambda)){
                self->light.cascadeLambda = 0.82f;
            }
            self->light.cascadeLambda = Math3D::Clamp(self->light.cascadeLambda, 0.0f, 1.0f);
        };

        const std::string entityId = entity->getNodeUniqueID();
        if(!entityId.empty()){
            const std::string key = makeMigrationKey(entityId);
            if(migratedLightSyncTransform.find(key) == migratedLightSyncTransform.end()){
                if(!self->syncTransform){
                    self->syncTransform = true;
                }
                if(self->light.shadowRange <= 0.0f){
                    self->light.shadowRange = defaultShadowRangeForType();
                }
                migratedLightSyncTransform.insert(key);
            }
            if(migratedLightDefaults.find(key) == migratedLightDefaults.end()){
                if(self->light.range <= 0.0f){
                    self->light.range = 20.0f;
                }
                if(self->light.shadowRange <= 0.0f){
                    self->light.shadowRange = defaultShadowRangeForType();
                }
                if(self->light.intensity <= 0.0f){
                    self->light.intensity = 4.0f;
                }
                if(self->light.falloff <= 0.0f){
                    self->light.falloff = 2.0f;
                }
                sanitizeCascadeLambda();
                if(self->light.type == LightType::SPOT && self->light.spotAngle <= 0.0f){
                    self->light.spotAngle = 45.0f;
                }
                migratedLightDefaults.insert(key);
            }
        }
        sanitizeCascadeLambda();
        syncLightFromTransform();
        const char* typeLabels[] = {"Point", "Directional", "Spot"};
        int typeIndex = static_cast<int>(self->light.type);
        if(EditorPropertyUI::Combo("Type", &typeIndex, typeLabels, IM_ARRAYSIZE(typeLabels))){
            LightType newType = static_cast<LightType>(typeIndex);
            if(newType != self->light.type){
                LightType prevType = self->light.type;
                self->light.type = newType;
                self->syncDirection = (newType != LightType::POINT);
                self->syncTransform = true;
                if(!std::isfinite(self->light.direction.x) || !std::isfinite(self->light.direction.y) || !std::isfinite(self->light.direction.z)){
                    self->light.direction = Math3D::Vec3(0,-1,0);
                }
                if(newType == LightType::POINT){
                    if(prevType == LightType::DIRECTIONAL || self->light.range <= 0.1f){
                        self->light.range = 20.0f;
                    }
                    ensurePointLightBounds(entity, self->light.range);
                    if(self->light.shadowRange <= 0.0f){
                        self->light.shadowRange = defaultShadowRangeForType();
                    }
                }else if(newType == LightType::DIRECTIONAL){
                    if(self->light.direction.length() < Math3D::EPSILON){
                        self->light.direction = Math3D::Vec3(0, -1, 0);
                    }
                    if(prevType != LightType::DIRECTIONAL){
                        self->light.range = 20.0f;
                        self->light.shadowRange = 300.0f;
                    }
                }else if(newType == LightType::SPOT){
                    if(self->light.direction.length() < Math3D::EPSILON){
                        self->light.direction = Math3D::Vec3(0, -1, 0);
                    }
                    if(prevType == LightType::DIRECTIONAL || self->light.range <= 0.1f){
                        self->light.range = 20.0f;
                    }
                    if(self->light.spotAngle <= 0.1f || prevType == LightType::DIRECTIONAL){
                        self->light.spotAngle = 45.0f;
                    }
                    if(self->light.shadowRange <= 0.0f){
                        self->light.shadowRange = defaultShadowRangeForType();
                    }
                }
            }
            syncLightFromTransform();
        }
        EditorPropertyUI::ColorEdit4("Color", &self->light.color.x);
        EditorPropertyUI::DragFloat("Intensity", &self->light.intensity, 0.05f, 0.0f, 10.0f);
        if(EditorAssetUI::DrawAssetDropInput("Flare Asset", self->flareAssetRef, {EditorAssetUI::AssetKind::LensFlareAsset})){
            self->flareAssetRef = StringUtils::Trim(self->flareAssetRef);
        }
        if(self->flareAssetRef.empty()){
            ImGui::TextDisabled("Assign a flare asset to render lens flares and glare streaks for this light.");
        }else{
            ImGui::TextDisabled("Requires an active Lens Flare Effect on the camera to render.");
        }
        bool prevSyncTransform = self->syncTransform;
        if(EditorPropertyUI::Checkbox("Sync Transform", &self->syncTransform)){
            if(prevSyncTransform && !self->syncTransform){
                Math3D::Vec3 worldPos;
                Math3D::Vec3 worldForward;
                if(getWorldLightBasis(worldPos, worldForward)){
                    self->light.position = worldPos;
                }
            }
            if(self->syncTransform && self->light.type == LightType::POINT){
                ensurePointLightBounds(entity, self->light.range);
            }
            syncLightFromTransform();
        }
        if(self->light.type == LightType::POINT){
            float range = self->light.range;
            if(EditorPropertyUI::DragFloat("Range", &range, 0.1f, 0.1f, 1000.0f)){
                self->light.range = range;
                ensurePointLightBounds(entity, range);
            }
            EditorPropertyUI::DragFloat("Falloff", &self->light.falloff, 0.05f, 0.1f, 3.0f);
            EditorPropertyUI::DragFloat("Shadow Range", &self->light.shadowRange, 0.1f, 0.0f, 2000.0f);
        }else if(self->light.type == LightType::SPOT){
            EditorPropertyUI::DragFloat("Range", &self->light.range, 0.1f, 0.1f, 1000.0f);
            EditorPropertyUI::DragFloat("Spot Angle", &self->light.spotAngle, 0.25f, 1.0f, 170.0f);
            EditorPropertyUI::DragFloat("Falloff", &self->light.falloff, 0.05f, 0.1f, 3.0f);
            EditorPropertyUI::DragFloat("Shadow Range", &self->light.shadowRange, 0.1f, 0.0f, 2000.0f);
        }else if(self->light.type == LightType::DIRECTIONAL){
            EditorPropertyUI::DragFloat("Shadow Range", &self->light.shadowRange, 0.5f, 10.0f, 2000.0f);
            if(EditorPropertyUI::SliderFloat("Cascade Lambda", &self->light.cascadeLambda, 0.0f, 1.0f, "%.3f")){
                self->light.cascadeLambda = Math3D::Clamp(self->light.cascadeLambda, 0.0f, 1.0f);
            }
            ImGui::TextDisabled("Lower values push cascade splits farther from camera.");
        }
        if(self->light.type != LightType::POINT){
            bool prevSyncDirection = self->syncDirection;
            if(EditorPropertyUI::Checkbox("Sync Direction", &self->syncDirection)){
                Math3D::Vec3 worldPos;
                Math3D::Vec3 worldForward;
                if(getWorldLightBasis(worldPos, worldForward)){
                    if(self->syncDirection){
                        self->light.direction = worldForward;
                    }else if(prevSyncDirection){
                        self->light.direction = worldForward;
                    }
                }
            }
            bool dirEditable = !self->syncDirection;
            if(!dirEditable){
                ImGui::BeginDisabled();
            }
            if(EditorPropertyUI::DragFloat3("Direction", &self->light.direction.x, 0.02f, -1.0f, 1.0f)){
                if(self->light.direction.length() < Math3D::EPSILON){
                    self->light.direction = Math3D::Vec3(0,-1,0);
                }else{
                    self->light.direction = self->light.direction.normalize();
                }
            }
            if(!dirEditable){
                ImGui::EndDisabled();
            }
        }
        EditorPropertyUI::Checkbox("Cast Shadows", &self->light.castsShadows);
        const char* shadowLabels[] = {"Hard", "Standard", "Smooth"};
        int shadowIndex = static_cast<int>(self->light.shadowType);
        if(EditorPropertyUI::Combo("Shadow Type", &shadowIndex, shadowLabels, IM_ARRAYSIZE(shadowLabels))){
            self->light.shadowType = static_cast<ShadowType>(shadowIndex);
        }
        EditorPropertyUI::DragFloat("Shadow Bias", &self->light.shadowBias, 0.0005f, 0.0f, 0.01f, "%.6f");
        EditorPropertyUI::DragFloat("Shadow Normal Bias", &self->light.shadowNormalBias, 0.0005f, 0.0f, 0.01f, "%.6f");
        EditorPropertyUI::DragFloat("Shadow Strength", &self->light.shadowStrength, 0.01f, 0.0f, 1.0f);

        const char* shadowDebugModes[] = {
            "Off",
            "Visibility",
            "Cascade Index",
            "Projection Bounds"
        };
        self->light.shadowDebugMode = Math3D::Clamp(self->light.shadowDebugMode, 0, IM_ARRAYSIZE(shadowDebugModes) - 1);
        int shadowDebugMode = self->light.shadowDebugMode;
        if(EditorPropertyUI::Combo("Shadow Debug View", &shadowDebugMode, shadowDebugModes, IM_ARRAYSIZE(shadowDebugModes))){
            self->light.shadowDebugMode = Math3D::Clamp(shadowDebugMode, 0, IM_ARRAYSIZE(shadowDebugModes) - 1);
        }
        if(ShadowRenderer::GetGlobalDebugOverrideEnabled()){
            const char* globalModes[] = {"Visibility", "Cascade Index", "Projection Bounds"};
            int globalIndex = Math3D::Clamp(ShadowRenderer::GetGlobalDebugOverrideMode() - 1, 0, IM_ARRAYSIZE(globalModes) - 1);
            ImGui::TextDisabled("Overridden by Global Shadow Debug: %s", globalModes[globalIndex]);
        }
}

void BoundsComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Bounds Component", ImGuiTreeNodeFlags_DefaultOpen, ecsPtr)){
        return;
    }
    const char* typeNames[] = { "Box", "Sphere", "Capsule" };
    int currentItem = static_cast<int>(type);
    if(EditorPropertyUI::Combo("Shape Type", &currentItem, typeNames, IM_ARRAYSIZE(typeNames))){
        type = static_cast<BoundsType>(currentItem);
    }

        if(EditorPropertyUI::DragFloat3("Offset", &offset.x, 0.05f)){
            if(!std::isfinite(offset.x)) offset.x = 0.0f;
            if(!std::isfinite(offset.y)) offset.y = 0.0f;
            if(!std::isfinite(offset.z)) offset.z = 0.0f;
        }

        ImGui::Separator();

        switch(type){
            case BoundsType::Box:
                if(EditorPropertyUI::DragFloat3("Half Extents", &size.x, 0.05f, 0.01f, 1000.0f, "%.2f")){
                    size.x = Math3D::Max(0.01f, size.x);
                    size.y = Math3D::Max(0.01f, size.y);
                    size.z = Math3D::Max(0.01f, size.z);
                }
                break;
            case BoundsType::Sphere:
                if(EditorPropertyUI::DragFloat("Radius", &radius, 0.05f, 0.01f, 1000.0f, "%.2f")){
                    radius = Math3D::Max(0.01f, radius);
                }
                break;
            case BoundsType::Capsule:
                if(EditorPropertyUI::DragFloat("Radius", &radius, 0.05f, 0.01f, 1000.0f, "%.2f")){
                    radius = Math3D::Max(0.01f, radius);
                }
                if(EditorPropertyUI::DragFloat("Height", &height, 0.05f, 0.01f, 1000.0f, "%.2f")){
                    height = Math3D::Max(0.01f, height);
                }
                break;
            default:
                break;
        }

        auto* owner = getParentEntity();
        const std::string ownerId = owner ? owner->getNodeUniqueID() : std::string();
        const bool canToggleGizmo = !ownerId.empty();
        const bool gizmoActive = canToggleGizmo && BoundsEditState::IsActiveForEntity(ownerId);
        if(!canToggleGizmo){
            ImGui::BeginDisabled();
        }
        if(ImGui::Button(gizmoActive ? "Stop Bounds Gizmo" : "Edit Bounds Gizmo")){
            if(gizmoActive){
                BoundsEditState::Deactivate();
            }else{
                BoundsEditState::ActivateForEntity(ownerId);
            }
        }
        if(!canToggleGizmo){
            ImGui::EndDisabled();
        }
    ImGui::TextDisabled("Viewport handles edit size and offset.");
}

void ColliderComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Collider Component", ImGuiTreeNodeFlags_DefaultOpen, ecsPtr)){
        return;
    }

    const char* shapeNames[] = { "Box", "Sphere", "Capsule" };
    int shapeIndex = static_cast<int>(shape);
    shapeIndex = Math3D::Clamp(shapeIndex, 0, IM_ARRAYSIZE(shapeNames) - 1);
    if(EditorPropertyUI::Combo("Shape", &shapeIndex, shapeNames, IM_ARRAYSIZE(shapeNames))){
        shape = static_cast<PhysicsColliderShape>(shapeIndex);
    }

    switch(shape){
        case PhysicsColliderShape::Box:
            if(EditorPropertyUI::DragFloat3("Half Extents", &boxHalfExtents.x, 0.02f, 0.01f, 1000.0f, "%.3f")){
                boxHalfExtents.x = Math3D::Max(0.01f, boxHalfExtents.x);
                boxHalfExtents.y = Math3D::Max(0.01f, boxHalfExtents.y);
                boxHalfExtents.z = Math3D::Max(0.01f, boxHalfExtents.z);
            }
            break;
        case PhysicsColliderShape::Sphere:
            if(EditorPropertyUI::DragFloat("Radius", &sphereRadius, 0.01f, 0.01f, 1000.0f, "%.3f")){
                sphereRadius = Math3D::Max(0.01f, sphereRadius);
            }
            break;
        case PhysicsColliderShape::Capsule:
            if(EditorPropertyUI::DragFloat("Radius", &capsuleRadius, 0.01f, 0.01f, 1000.0f, "%.3f")){
                capsuleRadius = Math3D::Max(0.01f, capsuleRadius);
            }
            if(EditorPropertyUI::DragFloat("Height", &capsuleHeight, 0.01f, 0.01f, 1000.0f, "%.3f")){
                capsuleHeight = Math3D::Max(0.01f, capsuleHeight);
            }
            break;
        default:
            break;
    }

    Math3D::Vec3 offsetPos = localOffset.position;
    if(EditorPropertyUI::DragFloat3("Offset Position", &offsetPos.x, 0.05f)){
        localOffset.position = offsetPos;
    }

    Math3D::Vec3 offsetRot = localOffset.rotation.ToEuler();
    if(EditorPropertyUI::DragFloat3("Offset Rotation", &offsetRot.x, 0.25f)){
        localOffset.setRotation(offsetRot);
    }

    const char* layerNames[] = { "Default", "Static World", "Dynamic Body", "Character", "Trigger" };
    int layerIndex = static_cast<int>(layer);
    layerIndex = Math3D::Clamp(layerIndex, 0, IM_ARRAYSIZE(layerNames) - 1);
    if(EditorPropertyUI::Combo("Layer", &layerIndex, layerNames, IM_ARRAYSIZE(layerNames))){
        layer = static_cast<PhysicsLayer>(layerIndex);
    }

    ImGui::TextUnformatted("Collides With");
    PhysicsLayerMask mask = collisionMask;
    auto drawMaskBit = [&](const char* label, PhysicsLayer targetLayer){
        const PhysicsLayerMask bit = PhysicsLayerBit(targetLayer);
        bool enabled = ((mask & bit) != 0u);
        if(EditorPropertyUI::Checkbox(label, &enabled)){
            if(enabled){
                mask |= bit;
            }else{
                mask &= ~bit;
            }
        }
    };
    drawMaskBit("Default##ColliderMaskDefault", PhysicsLayer::Default);
    drawMaskBit("Static World##ColliderMaskStatic", PhysicsLayer::StaticWorld);
    drawMaskBit("Dynamic Body##ColliderMaskDynamic", PhysicsLayer::DynamicBody);
    drawMaskBit("Character##ColliderMaskCharacter", PhysicsLayer::Character);
    drawMaskBit("Trigger##ColliderMaskTrigger", PhysicsLayer::Trigger);
    collisionMask = mask;

    EditorPropertyUI::Checkbox("Is Trigger", &isTrigger);
    EditorPropertyUI::DragFloat("Static Friction", &material.staticFriction, 0.01f, 0.0f, 4.0f, "%.2f");
    EditorPropertyUI::DragFloat("Dynamic Friction", &material.dynamicFriction, 0.01f, 0.0f, 4.0f, "%.2f");
    EditorPropertyUI::DragFloat("Restitution", &material.restitution, 0.01f, 0.0f, 1.0f, "%.2f");
    EditorPropertyUI::DragFloat("Density", &material.density, 0.01f, 0.001f, 1000.0f, "%.3f");
    material.staticFriction = Math3D::Max(0.0f, material.staticFriction);
    material.dynamicFriction = Math3D::Max(0.0f, material.dynamicFriction);
    material.restitution = Math3D::Clamp(material.restitution, 0.0f, 1.0f);
    material.density = Math3D::Max(0.001f, material.density);

    ImGui::TextDisabled("Runtime Shape Handle: %s", runtimeShapeHandle ? "bound" : "null");
}

void RigidBodyComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Rigid Body Component", ImGuiTreeNodeFlags_DefaultOpen, ecsPtr)){
        return;
    }

    const char* bodyTypeNames[] = { "Static", "Dynamic", "Kinematic" };
    int typeIndex = static_cast<int>(bodyType);
    typeIndex = Math3D::Clamp(typeIndex, 0, IM_ARRAYSIZE(bodyTypeNames) - 1);
    if(EditorPropertyUI::Combo("Body Type", &typeIndex, bodyTypeNames, IM_ARRAYSIZE(bodyTypeNames))){
        bodyType = static_cast<PhysicsBodyType>(typeIndex);
    }

    const bool isDynamic = (bodyType == PhysicsBodyType::Dynamic);
    if(!isDynamic){
        ImGui::BeginDisabled();
    }
    if(EditorPropertyUI::DragFloat("Mass", &mass, 0.05f, 0.001f, 100000.0f, "%.3f")){
        mass = Math3D::Max(0.001f, mass);
    }
    if(!isDynamic){
        ImGui::EndDisabled();
    }

    EditorPropertyUI::DragFloat("Gravity Scale", &gravityScale, 0.05f, -20.0f, 20.0f, "%.2f");
    if(EditorPropertyUI::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, 20.0f, "%.2f")){
        linearDamping = Math3D::Max(0.0f, linearDamping);
    }
    if(EditorPropertyUI::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, 20.0f, "%.2f")){
        angularDamping = Math3D::Max(0.0f, angularDamping);
    }

    if(bodyType != PhysicsBodyType::Static){
        EditorPropertyUI::DragFloat3("Linear Velocity", &linearVelocity.x, 0.05f);
        EditorPropertyUI::DragFloat3("Angular Velocity", &angularVelocity.x, 0.05f);
    }

    ImGui::TextUnformatted("Linear Axis Lock");
    ImGui::Checkbox("X##RigidLockLinearX", &lockLinearX);
    ImGui::SameLine();
    ImGui::Checkbox("Y##RigidLockLinearY", &lockLinearY);
    ImGui::SameLine();
    ImGui::Checkbox("Z##RigidLockLinearZ", &lockLinearZ);

    ImGui::TextUnformatted("Angular Axis Lock");
    ImGui::Checkbox("X##RigidLockAngularX", &lockAngularX);
    ImGui::SameLine();
    ImGui::Checkbox("Y##RigidLockAngularY", &lockAngularY);
    ImGui::SameLine();
    ImGui::Checkbox("Z##RigidLockAngularZ", &lockAngularZ);

    EditorPropertyUI::Checkbox("Use Continuous Collision", &useContinuousCollision);
    EditorPropertyUI::Checkbox("Can Sleep", &canSleep);
    if(!canSleep){
        startAwake = true;
        ImGui::BeginDisabled();
    }
    EditorPropertyUI::Checkbox("Start Awake", &startAwake);
    if(!canSleep){
        ImGui::EndDisabled();
    }

    ImGui::TextDisabled("Runtime Body Handle: %s", runtimeBodyHandle ? "bound" : "null");
}

void CameraComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    if(!beginEditorComponentSection(this, "Camera Component", ImGuiTreeNodeFlags_DefaultOpen, ecsPtr)){
        return;
    }

    if(!camera){
        if(ImGui::Button("Create Perspective Camera")){
            float width = 1280.0f;
            float height = 720.0f;
            if(scene && scene->getWindow()){
                width = (float)scene->getWindow()->getWindowWidth();
                height = (float)scene->getWindow()->getWindowHeight();
            }
            camera = Camera::CreatePerspective(45.0f, Math3D::Vec2(width, height), 0.1f, 1000.0f);
            if(scene){
                scene->setPreferredCamera(camera, false);
            }
        }
        return;
    }

    auto* mainScreen = scene ? scene->getMainScreen().get() : nullptr;
    PCamera preferred = scene ? scene->getPreferredCamera() : nullptr;
    bool isCurrent = preferred && (preferred == camera);
    if(!scene && mainScreen){
        isCurrent = (mainScreen->getCamera() == camera);
    }

    bool makeCurrent = isCurrent;
    if(EditorPropertyUI::Checkbox("Current Camera", &makeCurrent) && makeCurrent){
        if(scene){
            scene->setPreferredCamera(camera, false);
        }else if(mainScreen){
            mainScreen->setCamera(camera);
        }
    }

    CameraSettings& settings = camera->getSettings();
    Camera::SanitizePerspectivePlanes(settings.nearPlane, settings.farPlane);
    bool isOrtho = settings.isOrtho;
    if(EditorPropertyUI::Checkbox("Orthographic", &isOrtho)){
        settings.isOrtho = isOrtho;
    }

    float nearPlane = settings.nearPlane;
    float farPlane = settings.farPlane;
    if(EditorPropertyUI::DragFloat("Near Plane", &nearPlane, 0.01f, 0.001f, 5000.0f, "%.3f")){
        settings.nearPlane = nearPlane;
        Camera::SanitizePerspectivePlanes(settings.nearPlane, settings.farPlane);
    }
    if(EditorPropertyUI::DragFloat("Far Plane", &farPlane, 0.1f, 0.01f, 100000.0f, "%.2f")){
        settings.farPlane = farPlane;
        Camera::SanitizePerspectivePlanes(settings.nearPlane, settings.farPlane);
    }
    ImGui::TextDisabled("Near plane is precision-clamped for deferred depth reconstruction.");

    if(settings.isOrtho){
        float rectPos[2] = { settings.viewPlane.position.x, settings.viewPlane.position.y };
        float rectSize[2] = { settings.viewPlane.size.x, settings.viewPlane.size.y };
        if(EditorPropertyUI::DragFloat2("View Position", rectPos, 0.5f)){
            settings.viewPlane.position = Math3D::Vec2(rectPos[0], rectPos[1]);
        }
        if(EditorPropertyUI::DragFloat2("View Size", rectSize, 0.5f, 1.0f, 20000.0f)){
            settings.viewPlane.size = Math3D::Vec2(Math3D::Max(rectSize[0], 1.0f), Math3D::Max(rectSize[1], 1.0f));
        }
    }else{
        float fov = settings.fov;
        if(EditorPropertyUI::SliderFloat("FOV", &fov, 10.0f, 130.0f, "%.1f deg")){
            settings.fov = fov;
        }
        ImGui::TextDisabled("Aspect: %.3f", settings.aspect);
    }

    const bool deferredActive = (GameEngine::Engine &&
                                 GameEngine::Engine->getRenderStrategy() == EngineRenderStrategy::Deferred);
    if(deferredActive){
        ImGui::Separator();
        if(ImGui::TreeNode("SSAO / GI")){
            SSAOComponent* ssao = nullptr;
            NeoECS::ECSEntity* parentEntity = getParentEntity();
            NeoECS::ECSComponentManager* componentManager = ecsPtr ? ecsPtr->getComponentManager() : nullptr;
            if(componentManager && parentEntity){
                ssao = componentManager->getECSComponent<SSAOComponent>(parentEntity);
            }

            if(!ssao && ecsPtr && parentEntity){
                std::unique_ptr<NeoECS::GameObject> wrapper(
                    NeoECS::GameObject::CreateFromECSEntity(ecsPtr->getContext(), parentEntity)
                );
                if(wrapper && wrapper->addComponent<SSAOComponent>() && componentManager){
                    ssao = componentManager->getECSComponent<SSAOComponent>(parentEntity);
                    if(ssao){
                        ssao->enabled = false;
                    }
                }
            }

            if(ssao){
                ImGui::PushID("CameraSSAO");
                drawSsaoSettingsFields(*ssao);
                ImGui::PopID();
            }else{
                ImGui::TextDisabled("SSAO / GI settings could not be initialized for this camera.");
            }
            ImGui::TreePop();
        }
    }else{
        ImGui::Separator();
        ImGui::TextDisabled("SSAO / GI is available only in Deferred mode.");
    }
}

void SkyboxComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Skybox Component (Legacy)", ImGuiTreeNodeFlags_DefaultOpen, ecsPtr)){
        return;
    }

    if(EditorAssetUI::DrawAssetDropInput("Skybox Asset", skyboxAssetRef, {EditorAssetUI::AssetKind::SkyboxAsset})){
        skyboxAssetRef = StringUtils::Trim(skyboxAssetRef);
        loadedSkyboxAssetRef.clear();
        runtimeSkyBox.reset();
    }

    if(skyboxAssetRef.empty()){
        ImGui::TextDisabled("No skybox assigned.");
        return;
    }

    if(ImGui::Button("Reload Skybox")){
        loadedSkyboxAssetRef.clear();
        runtimeSkyBox.reset();
    }

    ImGui::SameLine();
    if(runtimeSkyBox && loadedSkyboxAssetRef == StringUtils::Trim(skyboxAssetRef)){
        ImGui::TextDisabled("Loaded.");
    }else{
        ImGui::TextDisabled("Legacy component. Use Environment Component.");
    }
}

bool EnvironmentComponent::loadFromAsset(std::string* outError){
    environmentAssetRef = StringUtils::Trim(environmentAssetRef);
    if(environmentAssetRef.empty()){
        if(outError){
            *outError = "Environment Asset is empty.";
        }
        return false;
    }

    EnvironmentAssetData data;
    if(!EnvironmentAssetIO::LoadFromAssetRef(environmentAssetRef, data, outError)){
        return false;
    }

    applyEnvironmentAssetToComponent(data, *this);
    loadedEnvironmentAssetRef = environmentAssetRef;
    return true;
}

bool EnvironmentComponent::saveToAsset(std::string* outError) const{
    const std::string trimmedRef = StringUtils::Trim(environmentAssetRef);
    if(trimmedRef.empty()){
        if(outError){
            *outError = "Environment Asset is empty.";
        }
        return false;
    }

    EnvironmentAssetData data;
    copyEnvironmentDataToAsset(*this, data);
    return EnvironmentAssetIO::SaveToAssetRef(trimmedRef, data, outError);
}

void EnvironmentComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)scene;
    if(!beginEditorComponentSection(this, "Environment Component", ImGuiTreeNodeFlags_DefaultOpen, ecsPtr)){
        return;
    }

    bool environmentAssetDropped = false;
    bool environmentAssetCommitted = false;
    if(EditorAssetUI::DrawAssetDropInput(
            "Environment Asset",
            environmentAssetRef,
            {EditorAssetUI::AssetKind::EnvironmentAsset},
            false,
            &environmentAssetDropped,
            &environmentAssetCommitted)){
        environmentAssetRef = StringUtils::Trim(environmentAssetRef);
        loadedEnvironmentAssetRef.clear();
        if(!environmentAssetRef.empty() && (environmentAssetDropped || environmentAssetCommitted)){
            std::string error;
            if(!loadFromAsset(&error)){
                LogBot.Log(LOG_ERRO, "Failed to load environment asset '%s': %s", environmentAssetRef.c_str(), error.c_str());
            }
        }
    }

    if(!loadedEnvironmentAssetRef.empty()){
        ImGui::TextDisabled("Loaded from: %s", loadedEnvironmentAssetRef.c_str());
    }

    if(EditorAssetUI::DrawAssetDropInput("Skybox Asset", skyboxAssetRef, {EditorAssetUI::AssetKind::SkyboxAsset})){
        skyboxAssetRef = StringUtils::Trim(skyboxAssetRef);
        loadedSkyboxAssetRef.clear();
        runtimeSkyBox.reset();
    }

    if(!skyboxAssetRef.empty()){
        if(ImGui::Button("Reload Skybox")){
            loadedSkyboxAssetRef.clear();
            runtimeSkyBox.reset();
        }
        ImGui::SameLine();
        if(runtimeSkyBox && loadedSkyboxAssetRef == StringUtils::Trim(skyboxAssetRef)){
            ImGui::TextDisabled("Skybox loaded.");
        }else{
            ImGui::TextDisabled("Skybox loads when this environment is active.");
        }
    }else{
        ImGui::TextDisabled("No skybox assigned.");
    }

    ImGui::Separator();
    drawEnvironmentSettingsFields(settings);
    sanitizeEnvironmentSettings(settings);
}

DeferredSSAOSettings SSAOComponent::buildDeferredSsaoSettings() const{
    DeferredSSAOSettings settings;
    settings.radiusPx = Math3D::Clamp(radiusPx, 0.25f, 8.0f);
    settings.depthRadius = Math3D::Clamp(depthRadius, 0.00001f, 0.5f);
    settings.bias = Math3D::Clamp(bias, 0.0f, 0.02f);
    settings.intensity = Math3D::Clamp(intensity, 0.0f, 10.0f);
    settings.giBoost = Math3D::Clamp(giBoost, 0.0f, 1.0f);
    settings.blurRadiusPx = Math3D::Clamp(blurRadiusPx, 0.5f, 6.0f);
    settings.blurSharpness = Math3D::Clamp(blurSharpness, 0.25f, 4.0f);
    settings.sampleCount = Math3D::Clamp(sampleCount, 4, 64);
    settings.debugView = Math3D::Clamp(debugView, 0, 4);
    return settings;
}

void SSAOComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "SSAO / GI", 0, ecsPtr)){
        return;
    }
    drawSsaoSettingsFields(*this);
}

bool PostProcessingStackComponent::hasEffectIdentifier(const std::string& identifier) const{
    if(identifier.empty()){
        return false;
    }
    const std::string needle = StringUtils::ToLowerCase(identifier);
    for(const auto& effect : effects){
        if(StringUtils::ToLowerCase(getEffectIdentifier(effect)) == needle){
            return true;
        }
    }
    return false;
}

std::string PostProcessingStackComponent::getEffectIdentifier(const PostProcessingEffectEntry& effect) const{
    if(effect.kind == PostProcessingEffectKind::Loaded){
        return effect.effectAssetRef;
    }
    return builtinEffectId(effect.kind);
}

std::string PostProcessingStackComponent::getEffectDisplayName(const PostProcessingEffectEntry& effect) const{
    if(effect.kind == PostProcessingEffectKind::Loaded){
        if(!effect.cachedDisplayName.empty()){
            return effect.cachedDisplayName;
        }
        if(!effect.effectAssetRef.empty()){
            return effectNameFromPath(std::filesystem::path(effect.effectAssetRef));
        }
    }
    return builtinEffectDisplayName(effect.kind);
}

bool PostProcessingStackComponent::addBuiltinEffect(PostProcessingEffectKind kind){
    if(kind == PostProcessingEffectKind::Loaded){
        return false;
    }
    const std::string identifier = builtinEffectId(kind);
    if(hasEffectIdentifier(identifier)){
        return false;
    }

    if(kind == PostProcessingEffectKind::AutoExposure && !hasEffectIdentifier(builtinEffectId(PostProcessingEffectKind::Bloom))){
        addBuiltinEffect(PostProcessingEffectKind::Bloom);
    }

    PostProcessingEffectEntry entry;
    entry.kind = kind;
    entry.enabled =
        (kind == PostProcessingEffectKind::LensFlare) ||
        (kind == PostProcessingEffectKind::AutoExposure) ||
        (kind == PostProcessingEffectKind::AntiAliasing);
    entry.hidden = false;
    entry.editorExpanded = false;
    entry.cachedDisplayName = builtinEffectDisplayName(kind);
    effects.push_back(std::move(entry));
    return true;
}

bool PostProcessingStackComponent::refreshLoadedEffect(PostProcessingEffectEntry& effect, std::string* outError){
    if(effect.kind != PostProcessingEffectKind::Loaded){
        return true;
    }
    if(effect.effectAssetRef.empty()){
        if(outError){
            *outError = "Loaded effect entry is missing an effect asset reference.";
        }
        return false;
    }

    const std::uint64_t revision = AssetManager::Instance.getRevision(effect.effectAssetRef);
    const bool needsReload =
        (effect.loadedAssetRevision != revision) ||
        effect.cachedDisplayName.empty() ||
        (effect.runtimeLoadedEffect == nullptr);

    EffectAssetData assetData;
    if(needsReload){
        if(!EffectAssetIO::LoadFromAssetRef(effect.effectAssetRef, assetData, outError)){
            return false;
        }

        std::vector<EffectPropertyData> mergedProperties = assetData.properties;
        for(auto& property : mergedProperties){
            auto it = std::find_if(
                effect.loadedProperties.begin(),
                effect.loadedProperties.end(),
                [&](const EffectPropertyData& existing){
                    if(!property.key.empty() && !existing.key.empty() && existing.key == property.key){
                        return true;
                    }
                    return !property.uniformName.empty() &&
                           !existing.uniformName.empty() &&
                           existing.uniformName == property.uniformName;
                }
            );
            if(it == effect.loadedProperties.end()){
                continue;
            }

            property.floatValue = it->floatValue;
            property.intValue = it->intValue;
            property.boolValue = it->boolValue;
            property.vec2Value = it->vec2Value;
            property.vec3Value = it->vec3Value;
            property.vec4Value = it->vec4Value;
            property.textureAssetRef = it->textureAssetRef;
            property.texturePtr = it->texturePtr;
            property.loadedTextureRef = it->loadedTextureRef;
            property.loadedTextureRevision = it->loadedTextureRevision;
        }

        effect.loadedInputs = assetData.inputs;
        effect.loadedProperties = std::move(mergedProperties);
        effect.loadedRequiredEffects = assetData.requiredEffects;
        effect.cachedDisplayName = assetData.name.empty()
            ? effectNameFromPath(std::filesystem::path(effect.effectAssetRef))
            : assetData.name;
        effect.loadedAssetRevision = revision;
    }

    if(!effect.runtimeLoadedEffect){
        effect.runtimeLoadedEffect = std::make_shared<LoadedEffect>();
    }
    if(effect.runtimeLoadedEffect){
        if(!needsReload){
            assetData = effect.runtimeLoadedEffect->effectData();
            if(!assetData.isComplete()){
                if(!EffectAssetIO::LoadFromAssetRef(effect.effectAssetRef, assetData, outError)){
                    return false;
                }
            }
        }
        effect.runtimeLoadedEffect->setSourceEffectRef(effect.effectAssetRef);
        assetData.inputs = effect.loadedInputs;
        assetData.properties = effect.loadedProperties;
        effect.runtimeLoadedEffect->setEffectData(assetData);
    }
    return true;
}

bool PostProcessingStackComponent::addLoadedEffect(const std::string& effectAssetRef, std::string* outError){
    if(effectAssetRef.empty()){
        if(outError){
            *outError = "Effect asset reference is empty.";
        }
        return false;
    }
    if(hasEffectIdentifier(effectAssetRef)){
        return false;
    }

    EffectAssetData assetData;
    if(!EffectAssetIO::LoadFromAssetRef(effectAssetRef, assetData, outError)){
        return false;
    }

    for(const std::string& required : assetData.requiredEffects){
        if(required.empty() || hasEffectIdentifier(required)){
            continue;
        }
        PostProcessingEffectKind builtinKind = PostProcessingEffectKind::Loaded;
        if(builtinEffectKindFromIdentifier(required, builtinKind)){
            addBuiltinEffect(builtinKind);
        }else if(StringUtils::ToLowerCase(required) != StringUtils::ToLowerCase(effectAssetRef)){
            addLoadedEffect(required, nullptr);
        }
    }

    PostProcessingEffectEntry entry;
    entry.kind = PostProcessingEffectKind::Loaded;
    entry.enabled = true;
    entry.hidden = false;
    entry.editorExpanded = false;
    entry.effectAssetRef = effectAssetRef;
    entry.loadedInputs = assetData.inputs;
    entry.loadedProperties = assetData.properties;
    entry.loadedRequiredEffects = assetData.requiredEffects;
    entry.loadedAssetRevision = AssetManager::Instance.getRevision(effectAssetRef);
    entry.cachedDisplayName = assetData.name.empty()
        ? effectNameFromPath(std::filesystem::path(effectAssetRef))
        : assetData.name;
    entry.runtimeLoadedEffect = std::make_shared<LoadedEffect>();
    if(entry.runtimeLoadedEffect){
        entry.runtimeLoadedEffect->setSourceEffectRef(effectAssetRef);
        entry.runtimeLoadedEffect->setEffectData(assetData);
    }
    effects.push_back(std::move(entry));
    return true;
}

bool PostProcessingStackComponent::moveEffect(size_t index, int delta){
    if(index >= effects.size() || delta == 0){
        return false;
    }
    const int destination = static_cast<int>(index) + delta;
    if(destination < 0 || destination >= static_cast<int>(effects.size())){
        return false;
    }
    std::swap(effects[index], effects[static_cast<size_t>(destination)]);
    return true;
}

void PostProcessingStackComponent::clearEffects(){
    effects.clear();
}

void PostProcessingStackComponent::ensureDependenciesForEffect(const PostProcessingEffectEntry& effect){
    if(effect.kind == PostProcessingEffectKind::AutoExposure){
        addBuiltinEffect(PostProcessingEffectKind::Bloom);
        return;
    }

    if(effect.kind != PostProcessingEffectKind::Loaded){
        return;
    }
    for(const std::string& required : effect.loadedRequiredEffects){
        if(required.empty() || hasEffectIdentifier(required)){
            continue;
        }
        PostProcessingEffectKind builtinKind = PostProcessingEffectKind::Loaded;
        if(builtinEffectKindFromIdentifier(required, builtinKind)){
            addBuiltinEffect(builtinKind);
        }else{
            addLoadedEffect(required, nullptr);
        }
    }
}

Graphics::PostProcessing::PPostProcessingEffect PostProcessingStackComponent::buildRuntimeEffect(PostProcessingEffectEntry& effect,
                                                                                                 const CameraSettings& settings,
                                                                                                 std::string* outError){
    switch(effect.kind){
        case PostProcessingEffectKind::DepthOfField:
            return buildDepthOfFieldRuntimeEffect(effect.depthOfField, effect.enabled, settings);
        case PostProcessingEffectKind::Bloom:
            return buildBloomRuntimeEffect(effect.bloom, effect.enabled, settings);
        case PostProcessingEffectKind::LensFlare:
            return buildLensFlareRuntimeEffect(effect.lensFlare, effect.enabled, settings);
        case PostProcessingEffectKind::AutoExposure:
            return buildAutoExposureRuntimeEffect(effect.autoExposure, effect.enabled, settings);
        case PostProcessingEffectKind::AntiAliasing:
            return buildAntiAliasingRuntimeEffect(effect.antiAliasing, effect.enabled, settings);
        case PostProcessingEffectKind::Loaded:
            if(!effect.enabled){
                return nullptr;
            }
            if(!refreshLoadedEffect(effect, outError)){
                return nullptr;
            }
            return effect.runtimeLoadedEffect;
        default:
            break;
    }
    return nullptr;
}

void PostProcessingStackComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)scene;
    if(!beginEditorComponentSection(this, "Post Processing Effect", 0, ecsPtr)){
        return;
    }

    if(effects.empty()){
        ImGui::TextDisabled("No effects in the stack.");
    }else{
        ImGui::TextUnformatted("Effect List");
        ImGui::Separator();

        for(size_t i = 0; i < effects.size();){
            bool removed = false;
            PostProcessingEffectEntry& effect = effects[i];
            const std::string baseLabel = getEffectDisplayName(effect);
            std::string rowLabel = baseLabel;
            if(!effect.enabled){
                rowLabel += " (Disabled)";
            }

            ImGui::PushID(static_cast<int>(i));
            if(ImGui::ArrowButton("##ExpandEffect", effect.editorExpanded ? ImGuiDir_Down : ImGuiDir_Right)){
                effect.editorExpanded = !effect.editorExpanded;
            }
            ImGui::SameLine();

            const float buttonWidth = (ImGui::GetFrameHeight() * 3.0f) + (ImGui::GetStyle().ItemSpacing.x * 2.0f) + 28.0f;
            const float labelWidth = Math3D::Max(ImGui::GetContentRegionAvail().x - buttonWidth, 1.0f);
            if(!effect.enabled){
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            if(ImGui::Button(rowLabel.c_str(), ImVec2(labelWidth, 0.0f))){
                effect.editorExpanded = !effect.editorExpanded;
            }
            if(!effect.enabled){
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            if(ImGui::ArrowButton("##MoveUp", ImGuiDir_Up)){
                moveEffect(i, -1);
            }
            ImGui::SameLine();
            if(ImGui::ArrowButton("##MoveDown", ImGuiDir_Down)){
                moveEffect(i, 1);
            }
            ImGui::SameLine();
            if(ImGui::SmallButton("X")){
                effects.erase(effects.begin() + static_cast<int>(i));
                removed = true;
            }

            if(!removed && effect.editorExpanded){
                ImGui::Indent();
                EditorPropertyUI::Checkbox("Enabled", &effect.enabled);
                ImGui::Separator();

                switch(effect.kind){
                    case PostProcessingEffectKind::DepthOfField:
                        drawDepthOfFieldEffectFields(effect.depthOfField);
                        break;
                    case PostProcessingEffectKind::Bloom:
                        drawBloomEffectFields(effect.bloom);
                        break;
                    case PostProcessingEffectKind::LensFlare:
                        drawLensFlareEffectFields(effect.lensFlare);
                        break;
                    case PostProcessingEffectKind::AutoExposure:
                        drawAutoExposureEffectFields(effect.autoExposure);
                        break;
                    case PostProcessingEffectKind::AntiAliasing:
                        drawAntiAliasingEffectFields(effect.antiAliasing);
                        break;
                    case PostProcessingEffectKind::Loaded:{
                        std::string error;
                        if(!refreshLoadedEffect(effect, &error)){
                            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", error.c_str());
                        }else{
                            ImGui::TextDisabled("%s", effect.effectAssetRef.c_str());
                            const std::string idSuffix = "loaded_effect_" + std::to_string(i);
                            for(auto& property : effect.loadedProperties){
                                drawLoadedEffectPropertyEditor(property, idSuffix);
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }

                ImGui::Unindent();
                ImGui::Separator();
            }

            ImGui::PopID();
            if(!removed){
                ++i;
            }
        }
    }

    if(ImGui::Button("Add Effect", ImVec2(-FLT_MIN, 30.0f))){
        ImGui::OpenPopup("Add Post Processing Effect");
    }
    if(ImGui::BeginPopup("Add Post Processing Effect")){
        std::vector<AvailablePostEffectOption> options = collectAvailablePostEffectOptions();
        std::string currentGroup;
        for(const auto& option : options){
            if(option.sourceGroup != currentGroup){
                if(!currentGroup.empty()){
                    ImGui::Separator();
                }
                currentGroup = option.sourceGroup;
                ImGui::TextDisabled("%s", currentGroup.c_str());
            }

            const bool alreadyPresent = hasEffectIdentifier(option.identifier);
            if(alreadyPresent){
                ImGui::BeginDisabled();
            }
            if(ImGui::MenuItem(option.displayName.c_str())){
                if(!option.bundlePath.empty()){
                    std::string mountError;
                    AssetBundleRegistry::Instance.mountBundle(option.bundlePath, &mountError);
                    if(!mountError.empty()){
                        LogBot.Log(LOG_WARN,
                                   "Failed to mount bundle for effect '%s': %s",
                                   option.displayName.c_str(),
                                   mountError.c_str());
                    }
                }

                if(option.kind == PostProcessingEffectKind::Loaded){
                    std::string error;
                    if(!addLoadedEffect(option.identifier, &error) && !error.empty()){
                        LogBot.Log(LOG_WARN, "Failed to add effect '%s': %s", option.identifier.c_str(), error.c_str());
                    }
                }else{
                    addBuiltinEffect(option.kind);
                }
                ImGui::CloseCurrentPopup();
            }
            if(alreadyPresent){
                ImGui::EndDisabled();
            }
        }
        ImGui::EndPopup();
    }

    if(ImGui::Button("Clear Effects", ImVec2(-FLT_MIN, 30.0f))){
        clearEffects();
    }
}

Graphics::PostProcessing::PPostProcessingEffect DepthOfFieldComponent::getEffectForCamera(const CameraSettings& settings){
    return buildDepthOfFieldRuntimeEffect(*this, IsComponentActive(this), settings);
}

void DepthOfFieldComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Depth Of Field Effect", 0, ecsPtr)){
        return;
    }
    drawDepthOfFieldEffectFields(*this);
}

Graphics::PostProcessing::PPostProcessingEffect BloomComponent::getEffectForCamera(const CameraSettings& settings){
    return buildBloomRuntimeEffect(*this, IsComponentActive(this), settings);
}

void BloomComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Bloom Effect", 0, ecsPtr)){
        return;
    }
    drawBloomEffectFields(*this);
}

Graphics::PostProcessing::PPostProcessingEffect LensFlareComponent::getEffectForCamera(const CameraSettings& settings){
    return buildLensFlareRuntimeEffect(*this, IsComponentActive(this), settings);
}

void LensFlareComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Lens Flare Effect", 0, ecsPtr)){
        return;
    }
    drawLensFlareEffectFields(*this);
}

Graphics::PostProcessing::PPostProcessingEffect AutoExposureComponent::getEffectForCamera(const CameraSettings& settings){
    return buildAutoExposureRuntimeEffect(*this, IsComponentActive(this), settings);
}

void AutoExposureComponent::applyBloomCoupling(BloomComponent* bloom){
    if(!bloom){
        return;
    }
    applyAutoExposureBloomCoupling(*this, IsComponentActive(this), *bloom, IsComponentActive(bloom));
}

void AutoExposureComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Auto Exposure Effect", 0, ecsPtr)){
        return;
    }
    drawAutoExposureEffectFields(*this);
}

Graphics::PostProcessing::PPostProcessingEffect AntiAliasingComponent::getEffectForCamera(const CameraSettings& settings){
    return buildAntiAliasingRuntimeEffect(*this, IsComponentActive(this), settings);
}

void AntiAliasingComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Anti-Aliasing Effect", 0, ecsPtr)){
        return;
    }
    drawAntiAliasingEffectFields(*this);
}

bool EnsurePostProcessingStackMigration(NeoECS::NeoECS* ecsPtr,
                                        NeoECS::ECSEntity* entity,
                                        std::string* outError){
    if(!ecsPtr || !entity){
        if(outError){
            *outError = "Invalid ECS/entity for post-processing migration.";
        }
        return false;
    }

    auto* manager = ecsPtr->getComponentManager();
    auto* context = ecsPtr->getContext();
    if(!manager || !context){
        if(outError){
            *outError = "Missing ECS context for post-processing migration.";
        }
        return false;
    }

    auto* legacyDof = manager->getECSComponent<DepthOfFieldComponent>(entity);
    auto* legacyBloom = manager->getECSComponent<BloomComponent>(entity);
    auto* legacyLensFlare = manager->getECSComponent<LensFlareComponent>(entity);
    auto* legacyAutoExposure = manager->getECSComponent<AutoExposureComponent>(entity);
    auto* legacyAa = manager->getECSComponent<AntiAliasingComponent>(entity);
    const bool hasLegacy =
        legacyDof || legacyBloom || legacyLensFlare || legacyAutoExposure || legacyAa;
    if(!hasLegacy){
        return true;
    }

    auto* stack = manager->getECSComponent<PostProcessingStackComponent>(entity);
    if(!stack){
        std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(context, entity));
        if(!wrapper || !wrapper->addComponent<PostProcessingStackComponent>()){
            if(outError){
                *outError = "Failed to add PostProcessingStackComponent during legacy migration.";
            }
            return false;
        }
        stack = manager->getECSComponent<PostProcessingStackComponent>(entity);
        if(!stack){
            if(outError){
                *outError = "PostProcessingStackComponent was unavailable after add.";
            }
            return false;
        }
    }

    auto addLegacyBuiltin = [&](PostProcessingEffectKind kind,
                                bool enabled,
                                bool hidden,
                                auto&& copySettings){
        if(stack->hasEffectIdentifier(builtinEffectId(kind))){
            return;
        }
        PostProcessingEffectEntry entry;
        entry.kind = kind;
        entry.enabled = enabled;
        entry.hidden = hidden;
        entry.editorExpanded = false;
        entry.cachedDisplayName = builtinEffectDisplayName(kind);
        copySettings(entry);
        stack->effects.push_back(std::move(entry));
    };

    if(legacyBloom){
        addLegacyBuiltin(
            PostProcessingEffectKind::Bloom,
            legacyBloom->enabled,
            legacyBloom->editorPanelHidden,
            [&](PostProcessingEffectEntry& entry){
                entry.bloom.adaptiveBloom = legacyBloom->adaptiveBloom;
                entry.bloom.threshold = legacyBloom->threshold;
                entry.bloom.softKnee = legacyBloom->softKnee;
                entry.bloom.intensity = legacyBloom->intensity;
                entry.bloom.radiusPx = legacyBloom->radiusPx;
                entry.bloom.sampleCount = legacyBloom->sampleCount;
                entry.bloom.tint = legacyBloom->tint;
            }
        );
        manager->removeECSComponent<BloomComponent>(entity);
    }

    if(legacyDof){
        addLegacyBuiltin(
            PostProcessingEffectKind::DepthOfField,
            legacyDof->enabled,
            legacyDof->editorPanelHidden,
            [&](PostProcessingEffectEntry& entry){
                entry.depthOfField.adaptiveFocus = legacyDof->adaptiveFocus;
                entry.depthOfField.adaptiveFocusDebugDraw = legacyDof->adaptiveFocusDebugDraw;
                entry.depthOfField.focusDistance = legacyDof->focusDistance;
                entry.depthOfField.focusRange = legacyDof->focusRange;
                entry.depthOfField.focusBandWidth = legacyDof->focusBandWidth;
                entry.depthOfField.blurRamp = legacyDof->blurRamp;
                entry.depthOfField.blurDistanceLerp = legacyDof->blurDistanceLerp;
                entry.depthOfField.fallbackFocusRange = legacyDof->fallbackFocusRange;
                entry.depthOfField.blurStrength = legacyDof->blurStrength;
                entry.depthOfField.maxBlurPx = legacyDof->maxBlurPx;
                entry.depthOfField.sampleCount = legacyDof->sampleCount;
                entry.depthOfField.debugCocView = legacyDof->debugCocView;
            }
        );
        manager->removeECSComponent<DepthOfFieldComponent>(entity);
    }

    if(legacyLensFlare){
        addLegacyBuiltin(
            PostProcessingEffectKind::LensFlare,
            legacyLensFlare->enabled,
            legacyLensFlare->editorPanelHidden,
            [&](PostProcessingEffectEntry& entry){
                (void)entry;
            }
        );
        manager->removeECSComponent<LensFlareComponent>(entity);
    }

    if(legacyAutoExposure){
        addLegacyBuiltin(
            PostProcessingEffectKind::AutoExposure,
            legacyAutoExposure->enabled,
            legacyAutoExposure->editorPanelHidden,
            [&](PostProcessingEffectEntry& entry){
                entry.autoExposure.minExposure = legacyAutoExposure->minExposure;
                entry.autoExposure.maxExposure = legacyAutoExposure->maxExposure;
                entry.autoExposure.exposureCompensation = legacyAutoExposure->exposureCompensation;
                entry.autoExposure.adaptationSpeedUp = legacyAutoExposure->adaptationSpeedUp;
                entry.autoExposure.adaptationSpeedDown = legacyAutoExposure->adaptationSpeedDown;
            }
        );
        stack->addBuiltinEffect(PostProcessingEffectKind::Bloom);
        manager->removeECSComponent<AutoExposureComponent>(entity);
    }

    if(legacyAa){
        addLegacyBuiltin(
            PostProcessingEffectKind::AntiAliasing,
            IsComponentActive(legacyAa),
            legacyAa->editorPanelHidden,
            [&](PostProcessingEffectEntry& entry){
                entry.antiAliasing.preset = legacyAa->preset;
            }
        );
        manager->removeECSComponent<AntiAliasingComponent>(entity);
    }

    return true;
}

bool ScriptComponent::hasScriptAsset(const std::string& scriptAssetRef) const{
    if(scriptAssetRef.empty()){
        return false;
    }
    const std::string needle = StringUtils::ToLowerCase(StringUtils::ReplaceAll(scriptAssetRef, "\\", "/"));
    for(const auto& existing : scriptAssetRefs){
        const std::string current = StringUtils::ToLowerCase(StringUtils::ReplaceAll(existing, "\\", "/"));
        if(current == needle){
            return true;
        }
    }
    return false;
}

bool ScriptComponent::addScriptAsset(const std::string& scriptAssetRef){
    if(scriptAssetRef.empty()){
        return false;
    }
    std::string normalized = StringUtils::ReplaceAll(scriptAssetRef, "\\", "/");
    if(hasScriptAsset(normalized)){
        return false;
    }
    scriptAssetRefs.push_back(normalized);
    std::sort(scriptAssetRefs.begin(), scriptAssetRefs.end());
    return true;
}

void ScriptComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!beginEditorComponentSection(this, "Script Component", ImGuiTreeNodeFlags_DefaultOpen, ecsPtr)){
        return;
    }

    if(scriptAssetRefs.empty()){
        ImGui::TextDisabled("No scripts assigned.");
        return;
    }

    int removeIndex = -1;
    for(size_t i = 0; i < scriptAssetRefs.size(); ++i){
        const std::string& path = scriptAssetRefs[i];
        const std::string displayName = BuildScriptDisplayNameFromPath(path);

        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(displayName.c_str());
        ImGui::SameLine();
        if(ImGui::SmallButton("Remove")){
            removeIndex = static_cast<int>(i);
        }
        ImGui::TextDisabled("%s", path.c_str());
        ImGui::Separator();
        ImGui::PopID();
    }

    if(removeIndex >= 0 && removeIndex < static_cast<int>(scriptAssetRefs.size())){
        scriptAssetRefs.erase(scriptAssetRefs.begin() + removeIndex);
    }
}
