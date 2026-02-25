#include "ECSComponents.h"
#include "imgui.h"
#include "GameEngine.h"
#include "PBRMaterial.h"
#include "ConstructedMaterial.h"
#include "ModelPartPrefabs.h"
#include "Asset.h"
#include "ShaderProgram.h"
#include "EditorAssetUI.h"
#include "ShaderAsset.h"
#include "MaterialAsset.h"
#include "MaterialRegistry.h"
#include "Logbot.h"

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

    struct ShaderPickerState{
        int selectedCacheIndex = -1;
        char cacheName[96] = "UserMaterialShader";
        char vertexPath[256] = "@assets/shader/Shader_Vert_Lit.vert";
        char fragmentPath[256] = "@assets/shader/Shader_Frag_LitColor.frag";
        char shaderAssetPath[256] = "";
        std::filesystem::file_time_type shaderAssetWriteTime{};
        bool hasAppliedShaderAsset = false;
    };

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
    struct MaterialSelectorState{
        int selectedRegistryIndex = -1;
        char materialAssetPath[256] = "";
    };
    std::unordered_map<std::string, MaterialSelectorState> g_materialSelectorStates;

    void drawMaterialAssetFields(const std::shared_ptr<Material>& material, const char* idSuffix);
    void drawShaderAssignmentUI(const std::shared_ptr<Material>& material, const char* idSuffix);

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

    std::shared_ptr<Texture> loadTextureFromAssetRef(const std::string& assetRef){
        if(assetRef.empty()){
            return nullptr;
        }
        auto asset = AssetManager::Instance.getOrLoad(assetRef);
        if(!asset){
            LogBot.Log(LOG_ERRO, "Failed to load texture asset: %s", assetRef.c_str());
            return nullptr;
        }
        auto tex = Texture::Load(asset);
        if(!tex){
            LogBot.Log(LOG_ERRO, "Failed to create texture from asset: %s", assetRef.c_str());
            return nullptr;
        }
        return tex;
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

    void drawMaterialValueFields(const std::shared_ptr<Material>& material, const char* idSuffix){
        if(!material){
            return;
        }

        std::string header = std::string("Material Parameters##") + idSuffix;
        if(!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)){
            return;
        }

        bool castsShadows = material->castsShadows();
        std::string castsLabel = std::string("Casts Shadows##") + idSuffix;
        if(ImGui::Checkbox(castsLabel.c_str(), &castsShadows)){
            material->setCastsShadows(castsShadows);
        }
        bool receivesShadows = material->receivesShadows();
        std::string receivesLabel = std::string("Receives Shadows##") + idSuffix;
        if(ImGui::Checkbox(receivesLabel.c_str(), &receivesShadows)){
            material->setReceivesShadows(receivesShadows);
        }

        if(auto pbr = Material::GetAs<PBRMaterial>(material)){
            Math3D::Vec4 baseColor = pbr->BaseColor.get();
            std::string baseColorLabel = std::string("Base Color##") + idSuffix;
            if(ImGui::ColorEdit4(baseColorLabel.c_str(), &baseColor.x)){
                pbr->BaseColor = baseColor;
            }

            float metallic = pbr->Metallic.get();
            std::string metallicLabel = std::string("Metallic##") + idSuffix;
            if(ImGui::SliderFloat(metallicLabel.c_str(), &metallic, 0.0f, 1.0f)){
                pbr->Metallic = metallic;
            }

            float roughness = pbr->Roughness.get();
            std::string roughnessLabel = std::string("Roughness##") + idSuffix;
            if(ImGui::SliderFloat(roughnessLabel.c_str(), &roughness, 0.0f, 1.0f)){
                pbr->Roughness = roughness;
            }

            float normalScale = pbr->NormalScale.get();
            std::string normalScaleLabel = std::string("Normal Scale##") + idSuffix;
            if(ImGui::DragFloat(normalScaleLabel.c_str(), &normalScale, 0.01f, 0.0f, 8.0f)){
                pbr->NormalScale = normalScale;
            }

            float heightScale = pbr->HeightScale.get();
            std::string heightScaleLabel = std::string("Height Scale##") + idSuffix;
            if(ImGui::DragFloat(heightScaleLabel.c_str(), &heightScale, 0.001f, 0.0f, 1.0f)){
                pbr->HeightScale = heightScale;
            }

            Math3D::Vec3 emissiveColor = pbr->EmissiveColor.get();
            std::string emissiveColorLabel = std::string("Emissive Color##") + idSuffix;
            if(ImGui::ColorEdit3(emissiveColorLabel.c_str(), &emissiveColor.x)){
                pbr->EmissiveColor = emissiveColor;
            }

            float emissiveStrength = pbr->EmissiveStrength.get();
            std::string emissiveStrengthLabel = std::string("Emissive Strength##") + idSuffix;
            if(ImGui::DragFloat(emissiveStrengthLabel.c_str(), &emissiveStrength, 0.01f, 0.0f, 32.0f)){
                pbr->EmissiveStrength = emissiveStrength;
            }

            float occlusionStrength = pbr->OcclusionStrength.get();
            std::string occlusionStrengthLabel = std::string("AO Strength##") + idSuffix;
            if(ImGui::SliderFloat(occlusionStrengthLabel.c_str(), &occlusionStrength, 0.0f, 4.0f)){
                pbr->OcclusionStrength = occlusionStrength;
            }

            Math3D::Vec2 uvScale = pbr->UVScale.get();
            std::string uvScaleLabel = std::string("UV Scale##") + idSuffix;
            if(ImGui::DragFloat2(uvScaleLabel.c_str(), &uvScale.x, 0.01f, -64.0f, 64.0f)){
                pbr->UVScale = uvScale;
            }

            Math3D::Vec2 uvOffset = pbr->UVOffset.get();
            std::string uvOffsetLabel = std::string("UV Offset##") + idSuffix;
            if(ImGui::DragFloat2(uvOffsetLabel.c_str(), &uvOffset.x, 0.01f, -64.0f, 64.0f)){
                pbr->UVOffset = uvOffset;
            }

            bool useAlphaClip = (pbr->UseAlphaClip.get() != 0);
            std::string useAlphaClipLabel = std::string("Use Alpha Clip##") + idSuffix;
            if(ImGui::Checkbox(useAlphaClipLabel.c_str(), &useAlphaClip)){
                pbr->UseAlphaClip = useAlphaClip ? 1 : 0;
            }
            if(useAlphaClip){
                float alphaCutoff = pbr->AlphaCutoff.get();
                std::string alphaCutoffLabel = std::string("Alpha Cutoff##") + idSuffix;
                if(ImGui::SliderFloat(alphaCutoffLabel.c_str(), &alphaCutoff, 0.0f, 1.0f)){
                    pbr->AlphaCutoff = alphaCutoff;
                }
            }

            bool useEnvMap = (pbr->UseEnvMap.get() != 0);
            std::string useEnvMapLabel = std::string("Use Env Map##") + idSuffix;
            if(ImGui::Checkbox(useEnvMapLabel.c_str(), &useEnvMap)){
                pbr->UseEnvMap = useEnvMap ? 1 : 0;
            }

            float envStrength = pbr->EnvStrength.get();
            std::string envStrengthLabel = std::string("Env Strength##") + idSuffix;
            if(ImGui::DragFloat(envStrengthLabel.c_str(), &envStrength, 0.01f, 0.0f, 8.0f)){
                pbr->EnvStrength = envStrength;
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
                        changedField |= ImGui::DragFloat(fieldLabel.c_str(), &field.floatValue, 0.01f);
                        break;
                    }
                    case ConstructedMaterial::FieldType::Int:{
                        changedField |= ImGui::DragInt(fieldLabel.c_str(), &field.intValue, 1.0f);
                        break;
                    }
                    case ConstructedMaterial::FieldType::Bool:{
                        changedField |= ImGui::Checkbox(fieldLabel.c_str(), &field.boolValue);
                        break;
                    }
                    case ConstructedMaterial::FieldType::Vec2:{
                        changedField |= ImGui::DragFloat2(fieldLabel.c_str(), &field.vec2Value.x, 0.01f);
                        break;
                    }
                    case ConstructedMaterial::FieldType::Vec3:{
                        if(looksLikeColorField(field)){
                            changedField |= ImGui::ColorEdit3(fieldLabel.c_str(), &field.vec3Value.x);
                        }else{
                            changedField |= ImGui::DragFloat3(fieldLabel.c_str(), &field.vec3Value.x, 0.01f);
                        }
                        break;
                    }
                    case ConstructedMaterial::FieldType::Vec4:{
                        if(looksLikeColorField(field)){
                            changedField |= ImGui::ColorEdit4(fieldLabel.c_str(), &field.vec4Value.x);
                        }else{
                            changedField |= ImGui::DragFloat4(fieldLabel.c_str(), &field.vec4Value.x, 0.01f);
                        }
                        break;
                    }
                    case ConstructedMaterial::FieldType::Texture2D:{
                        changedField |= EditorAssetUI::DrawAssetDropInput(fieldLabel.c_str(), field.textureAssetRef, {EditorAssetUI::AssetKind::Image});
                        if(field.textureAssetRef.empty()){
                            ImGui::TextDisabled("None");
                        }else if(field.texturePtr && field.texturePtr->getID() != 0){
                            ImGui::TextDisabled("Assigned");
                        }else{
                            ImGui::TextDisabled("Missing");
                        }
                        break;
                    }
                    default:
                        break;
                }

                if(changedField){
                    constructed->markFieldsDirty();
                    constructed->applyField(i);
                }
            }
        }else if(auto colorMat = Material::GetAs<MaterialDefaults::ColorMaterial>(material)){
            Math3D::Vec4 color = colorMat->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(ImGui::ColorEdit4(colorLabel.c_str(), &color.x)){
                colorMat->Color = color;
            }
        }else if(auto imageMat = Material::GetAs<MaterialDefaults::ImageMaterial>(material)){
            Math3D::Vec4 color = imageMat->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(ImGui::ColorEdit4(colorLabel.c_str(), &color.x)){
                imageMat->Color = color;
            }
            Math3D::Vec2 uv = imageMat->UV.get();
            std::string uvLabel = std::string("UV Offset##") + idSuffix;
            if(ImGui::DragFloat2(uvLabel.c_str(), &uv.x, 0.01f, -64.0f, 64.0f)){
                imageMat->UV = uv;
            }
        }else if(auto litColor = Material::GetAs<MaterialDefaults::LitColorMaterial>(material)){
            Math3D::Vec4 color = litColor->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(ImGui::ColorEdit4(colorLabel.c_str(), &color.x)){
                litColor->Color = color;
            }
        }else if(auto litImage = Material::GetAs<MaterialDefaults::LitImageMaterial>(material)){
            Math3D::Vec4 color = litImage->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(ImGui::ColorEdit4(colorLabel.c_str(), &color.x)){
                litImage->Color = color;
            }
        }else if(auto flatColor = Material::GetAs<MaterialDefaults::FlatColorMaterial>(material)){
            Math3D::Vec4 color = flatColor->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(ImGui::ColorEdit4(colorLabel.c_str(), &color.x)){
                flatColor->Color = color;
            }
        }else if(auto flatImage = Material::GetAs<MaterialDefaults::FlatImageMaterial>(material)){
            Math3D::Vec4 color = flatImage->Color.get();
            std::string colorLabel = std::string("Color##") + idSuffix;
            if(ImGui::ColorEdit4(colorLabel.c_str(), &color.x)){
                flatImage->Color = color;
            }
        }else{
            ImGui::TextDisabled("No editable material value fields for this type.");
        }

        ImGui::Separator();
        drawMaterialAssetFields(material, idSuffix);
        drawShaderAssignmentUI(material, idSuffix);
    }

    void drawMaterialSelectionUI(std::shared_ptr<Material>& materialRef, const char* idSuffix){
        MaterialRegistry::Instance().Refresh();
        const auto& entries = MaterialRegistry::Instance().GetEntries();
        MaterialSelectorState& picker = getMaterialSelectorState(idSuffix);
        if(picker.selectedRegistryIndex >= static_cast<int>(entries.size())){
            picker.selectedRegistryIndex = -1;
        }

        std::string comboLabel = std::string("Material Registry##") + idSuffix;
        const char* preview = (picker.selectedRegistryIndex >= 0 && picker.selectedRegistryIndex < static_cast<int>(entries.size()))
            ? entries[picker.selectedRegistryIndex].displayName.c_str()
            : "<select material>";

        if(ImGui::BeginCombo(comboLabel.c_str(), preview)){
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
                auto mat = MaterialAssetIO::InstantiateMaterialFromRef(assetRef, nullptr, &error);
                if(!mat){
                    LogBot.Log(LOG_ERRO, "Failed to instantiate material '%s': %s", assetRef.c_str(), error.c_str());
                }else{
                    materialRef = mat;
                }
            }
        }
    }

    void drawShaderAssignmentUI(const std::shared_ptr<Material>& material, const char* idSuffix){
        if(!material){
            ImGui::TextDisabled("No material assigned.");
            return;
        }

        std::string shaderHeader = std::string("Shader Assignment##") + idSuffix;
        if(!ImGui::TreeNodeEx(shaderHeader.c_str())){
            return;
        }

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
            applyShaderAssetToMaterial(material, picker, true);
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
        if(ImGui::BeginCombo(comboLabel.c_str(), preview)){
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
        ImGui::InputText(cacheNameLabel.c_str(), picker.cacheName, sizeof(picker.cacheName));
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
                        }
                    }
                }
            }
        }

        ImGui::TreePop();
    }

    void drawPartTransformEditor(Math3D::Transform& localTransform){
        Math3D::Vec3 pos = localTransform.position;
        Math3D::Vec3 rot = localTransform.rotation.ToEuler();
        Math3D::Vec3 scale = localTransform.scale;

        if(ImGui::DragFloat3("Local Position", &pos.x, 0.1f)){
            localTransform.position = pos;
        }
        if(ImGui::DragFloat3("Local Rotation", &rot.x, 0.5f)){
            localTransform.setRotation(rot);
        }
        if(ImGui::DragFloat3("Local Scale", &scale.x, 0.1f)){
            localTransform.scale = scale;
        }
    }

    void drawMaterialAssetFields(const std::shared_ptr<Material>& material, const char* idSuffix){
        if(!material){
            return;
        }

        MaterialAssetFieldState& fieldState = getMaterialAssetFieldState(material);

        auto drawTextureField = [&](const char* label,
                                    const char* key,
                                    char* pathBuffer,
                                    const std::function<void(const std::shared_ptr<Texture>&)>& applyTexture,
                                    const std::function<std::shared_ptr<Texture>()>& readTexture){
            std::string fieldLabel = std::string(label) + "##" + idSuffix;
            bool dropped = false;
            EditorAssetUI::DrawAssetDropInput(fieldLabel.c_str(), pathBuffer, 256, EditorAssetUI::AssetKind::Image, false, &dropped);

            const std::string applyId = std::string("Apply##") + idSuffix + "_" + key;
            const std::string clearId = std::string("Clear##") + idSuffix + "_" + key;
            bool applyClicked = ImGui::Button(applyId.c_str());
            bool clearClicked = ImGui::Button(clearId.c_str());

            if(clearClicked){
                pathBuffer[0] = '\0';
                applyTexture(nullptr);
            }else if(dropped || applyClicked){
                std::string assetRef = pathBuffer;
                if(assetRef.empty()){
                    applyTexture(nullptr);
                }else{
                    auto tex = loadTextureFromAssetRef(assetRef);
                    if(tex){
                        applyTexture(tex);
                    }
                }
            }

            std::shared_ptr<Texture> previewTex;
            if(readTexture){
                previewTex = readTexture();
            }

            std::string previewId = std::string("preview_") + idSuffix + "_" + key;
            ImGui::TextUnformatted("Preview");
            ImGui::PushID(previewId.c_str());
            if(previewTex && previewTex->getID() != 0){
                ImTextureID texId = (ImTextureID)(intptr_t)previewTex->getID();
                ImGui::Image(texId, ImVec2(44.0f, 44.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            }else{
                ImGui::BeginDisabled();
                ImGui::Button("None", ImVec2(44.0f, 44.0f));
                ImGui::EndDisabled();
            }
            ImGui::PopID();

            ImGui::Spacing();
        };

        if(auto pbr = Material::GetAs<PBRMaterial>(material)){
            std::string header = std::string("PBR Texture Slots##") + idSuffix;
            if(ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)){
                drawTextureField("Base Color Tex", "pbr_base", fieldState.baseColorTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->BaseColorTex = tex; },
                    [&](){ return pbr->BaseColorTex.get(); });
                drawTextureField("Roughness Tex", "pbr_rough", fieldState.roughnessTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->RoughnessTex = tex; },
                    [&](){ return pbr->RoughnessTex.get(); });
                drawTextureField("Metal/Rough Tex", "pbr_mr", fieldState.metallicRoughTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->MetallicRoughnessTex = tex; },
                    [&](){ return pbr->MetallicRoughnessTex.get(); });
                drawTextureField("Normal Tex", "pbr_normal", fieldState.normalTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->NormalTex = tex; },
                    [&](){ return pbr->NormalTex.get(); });
                drawTextureField("Height Tex", "pbr_height", fieldState.heightTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->HeightTex = tex; },
                    [&](){ return pbr->HeightTex.get(); });
                drawTextureField("Emissive Tex", "pbr_emissive", fieldState.emissiveTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->EmissiveTex = tex; },
                    [&](){ return pbr->EmissiveTex.get(); });
                drawTextureField("Occlusion Tex", "pbr_occ", fieldState.occlusionTex,
                    [&](const std::shared_ptr<Texture>& tex){ pbr->OcclusionTex = tex; },
                    [&](){ return pbr->OcclusionTex.get(); });
                ImGui::TreePop();
            }
            return;
        }

        if(auto imageMat = Material::GetAs<MaterialDefaults::ImageMaterial>(material)){
            std::string header = std::string("Image Texture Slot##") + idSuffix;
            if(ImGui::TreeNodeEx(header.c_str())){
                drawTextureField("Texture", "img_tex", fieldState.imageTex,
                    [&](const std::shared_ptr<Texture>& tex){ imageMat->Tex = tex; },
                    [&](){ return imageMat->Tex.get(); });
                ImGui::TreePop();
            }
            return;
        }

        if(auto litImage = Material::GetAs<MaterialDefaults::LitImageMaterial>(material)){
            std::string header = std::string("Lit Image Texture Slot##") + idSuffix;
            if(ImGui::TreeNodeEx(header.c_str())){
                drawTextureField("Texture", "lit_img_tex", fieldState.imageTex,
                    [&](const std::shared_ptr<Texture>& tex){ litImage->Tex = tex; },
                    [&](){ return litImage->Tex.get(); });
                ImGui::TreePop();
            }
            return;
        }

        if(auto flatImage = Material::GetAs<MaterialDefaults::FlatImageMaterial>(material)){
            std::string header = std::string("Flat Image Texture Slot##") + idSuffix;
            if(ImGui::TreeNodeEx(header.c_str())){
                drawTextureField("Texture", "flat_img_tex", fieldState.imageTex,
                    [&](const std::shared_ptr<Texture>& tex){ flatImage->Tex = tex; },
                    [&](){ return flatImage->Tex.get(); });
                ImGui::TreePop();
            }
            return;
        }
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

void TransformComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    if(ImGui::CollapsingHeader("Transform Component", ImGuiTreeNodeFlags_DefaultOpen)){

        auto transform = this;

        Math3D::Vec3 pos = transform->local.position;
        Math3D::Vec3 rot = transform->local.rotation.ToEuler();
        Math3D::Vec3 scale = transform->local.scale;

        if(ImGui::DragFloat3("Position", &pos.x, 0.1f)){
            transform->local.position = pos;
        }

        if(ImGui::DragFloat3("Rotation", &rot.x, 0.5f)){
            transform->local.setRotation(rot);
        }

        if(ImGui::DragFloat3("Scale", &scale.x, 0.1f)){
            transform->local.scale = scale;
        }
    }
}

void MeshRendererComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    if(ImGui::CollapsingHeader("Mesh Renderer Component", ImGuiTreeNodeFlags_DefaultOpen)){
        auto renderer = this;

        ImGui::Separator();
        ImGui::TextUnformatted("Mesh Renderer");
        ImGui::Checkbox("Visible", &renderer->visible);
        ImGui::Checkbox("Backface Cull", &renderer->enableBackfaceCulling);

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
                    auto newModel = Model::Create();
                    auto part = std::make_shared<ModelPart>();
                    part->mesh = renderer->mesh;
                    part->material = renderer->material;
                    newModel->addPart(part);
                    renderer->model = newModel;
                    renderer->mesh.reset();
                    renderer->material.reset();
                }
            }

            if(ImGui::Button("Create Empty Model")){
                renderer->model = Model::Create();
            }

            ImGui::Spacing();
            ImGui::TextUnformatted("Material");
            drawMaterialSelectionUI(renderer->material, "single_material_select");
            if(renderer->material){
                ImGui::PushID(renderer->material.get());
                drawMaterialValueFields(renderer->material, "single_material");
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

                ImGui::Checkbox("Visible", &part->visible);
                ImGui::Checkbox("Hide In ECS Tree", &part->hideInEditorTree);

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
                    char idSuffix[64] = {};
                    std::snprintf(idSuffix, sizeof(idSuffix), "part_%zu_%p", i, part.get());
                    std::string selectSuffix = std::string(idSuffix) + "_select";
                    drawMaterialSelectionUI(part->material, selectSuffix.c_str());
                    drawMaterialValueFields(part->material, idSuffix);
                }else{
                    ImGui::Separator();
                    std::string emptySelectSuffix = std::string("part_empty_") + std::to_string(i);
                    drawMaterialSelectionUI(part->material, emptySelectSuffix.c_str());
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }
    }
}

void LightComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    if(ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen)){
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
                return 200.0f;
            }
            return Math3D::Max(self->light.range, 1.0f);
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
                if(self->light.type == LightType::SPOT && self->light.spotAngle <= 0.0f){
                    self->light.spotAngle = 45.0f;
                }
                migratedLightDefaults.insert(key);
            }
        }
        syncLightFromTransform();
        const char* typeLabels[] = {"Point", "Directional", "Spot"};
        int typeIndex = static_cast<int>(self->light.type);
        if(ImGui::Combo("Type", &typeIndex, typeLabels, IM_ARRAYSIZE(typeLabels))){
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
                        self->light.shadowRange = 200.0f;
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
        ImGui::ColorEdit4("Color", &self->light.color.x);
        ImGui::DragFloat("Intensity", &self->light.intensity, 0.05f, 0.0f, 10.0f);
        bool prevSyncTransform = self->syncTransform;
        if(ImGui::Checkbox("Sync Transform", &self->syncTransform)){
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
            if(ImGui::DragFloat("Range", &range, 0.1f, 0.1f, 1000.0f)){
                self->light.range = range;
                ensurePointLightBounds(entity, range);
            }
            ImGui::DragFloat("Falloff", &self->light.falloff, 0.05f, 0.1f, 3.0f);
            ImGui::DragFloat("Shadow Range", &self->light.shadowRange, 0.1f, 0.0f, 2000.0f);
        }else if(self->light.type == LightType::SPOT){
            ImGui::DragFloat("Range", &self->light.range, 0.1f, 0.1f, 1000.0f);
            ImGui::DragFloat("Spot Angle", &self->light.spotAngle, 0.25f, 1.0f, 170.0f);
            ImGui::DragFloat("Falloff", &self->light.falloff, 0.05f, 0.1f, 3.0f);
            ImGui::DragFloat("Shadow Range", &self->light.shadowRange, 0.1f, 0.0f, 2000.0f);
        }else if(self->light.type == LightType::DIRECTIONAL){
            ImGui::DragFloat("Shadow Range", &self->light.shadowRange, 0.5f, 10.0f, 2000.0f);
        }
        if(self->light.type != LightType::POINT){
            bool prevSyncDirection = self->syncDirection;
            if(ImGui::Checkbox("Sync Direction", &self->syncDirection)){
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
            if(ImGui::DragFloat3("Direction", &self->light.direction.x, 0.02f, -1.0f, 1.0f)){
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
        ImGui::Checkbox("Cast Shadows", &self->light.castsShadows);
        const char* shadowLabels[] = {"Hard", "Standard", "Smooth"};
        int shadowIndex = static_cast<int>(self->light.shadowType);
        if(ImGui::Combo("Shadow Type", &shadowIndex, shadowLabels, IM_ARRAYSIZE(shadowLabels))){
            self->light.shadowType = static_cast<ShadowType>(shadowIndex);
        }
        ImGui::DragFloat("Shadow Bias", &self->light.shadowBias, 0.0005f, 0.0f, 0.01f, "%.6f");
        ImGui::DragFloat("Shadow Normal Bias", &self->light.shadowNormalBias, 0.0005f, 0.0f, 0.01f, "%.6f");
        ImGui::DragFloat("Shadow Strength", &self->light.shadowStrength, 0.01f, 0.0f, 1.0f);
    }
}

void BoundsComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    if (ImGui::CollapsingHeader("Bounds Component", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        // 1. Type Selector
        const char* typeNames[] = { "Box", "Sphere", "Capsule" };
        
        // Convert Enum to int for ImGui
        int currentItem = static_cast<int>(type);
        
        if (ImGui::Combo("Shape Type", &currentItem, typeNames, IM_ARRAYSIZE(typeNames))) {
            type = static_cast<BoundsType>(currentItem);
        }

        ImGui::Separator();

        // 2. Context-Sensitive Fields
        switch (type) {
            case BoundsType::Box:
                ImGui::TextDisabled("(Half-Size Extents)");
                // Clamp min to 0 to prevent inverted boxes
                ImGui::DragFloat3("Extents", &size.x, 0.05f, 0.0f, 0.0f, "%.2f");
                break;

            case BoundsType::Sphere:
                ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 0.0f, "%.2f");
                break;

            case BoundsType::Capsule:
                ImGui::DragFloat("Radius", &radius, 0.05f, 0.0f, 0.0f, "%.2f");
                ImGui::DragFloat("Total Height", &height, 0.05f, 0.0f, 0.0f, "%.2f");
                break;
        }

        ImGui::Separator();
        // Assuming you have a debug rendering flag somewhere
        static bool debugDraw = false; 
        ImGui::Checkbox("Show Debug Lines", &debugDraw);
        if(debugDraw && scene) { 
            // Call your debug drawer here / none yet will add.
        }
    }
};
void CameraComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    if(!ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen)){
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
    if(ImGui::Checkbox("Current Camera", &makeCurrent) && makeCurrent){
        if(scene){
            scene->setPreferredCamera(camera, false);
        }else if(mainScreen){
            mainScreen->setCamera(camera);
        }
    }

    CameraSettings& settings = camera->getSettings();
    bool isOrtho = settings.isOrtho;
    if(ImGui::Checkbox("Orthographic", &isOrtho)){
        settings.isOrtho = isOrtho;
    }

    float nearPlane = settings.nearPlane;
    float farPlane = settings.farPlane;
    if(ImGui::DragFloat("Near Plane", &nearPlane, 0.01f, 0.001f, 5000.0f, "%.3f")){
        settings.nearPlane = Math3D::Clamp(nearPlane, 0.001f, settings.farPlane - 0.001f);
    }
    if(ImGui::DragFloat("Far Plane", &farPlane, 0.1f, 0.01f, 100000.0f, "%.2f")){
        settings.farPlane = Math3D::Max(farPlane, settings.nearPlane + 0.001f);
    }

    if(settings.isOrtho){
        float rectPos[2] = { settings.viewPlane.position.x, settings.viewPlane.position.y };
        float rectSize[2] = { settings.viewPlane.size.x, settings.viewPlane.size.y };
        if(ImGui::DragFloat2("View Position", rectPos, 0.5f)){
            settings.viewPlane.position = Math3D::Vec2(rectPos[0], rectPos[1]);
        }
        if(ImGui::DragFloat2("View Size", rectSize, 0.5f, 1.0f, 20000.0f)){
            settings.viewPlane.size = Math3D::Vec2(Math3D::Max(rectSize[0], 1.0f), Math3D::Max(rectSize[1], 1.0f));
        }
    }else{
        float fov = settings.fov;
        if(ImGui::SliderFloat("FOV", &fov, 10.0f, 130.0f, "%.1f deg")){
            settings.fov = fov;
        }
        ImGui::TextDisabled("Aspect: %.3f", settings.aspect);
    }
}

Graphics::PostProcessing::PPostProcessingEffect SSAOComponent::getEffectForCamera(const CameraSettings& settings){
    if(!enabled){
        return nullptr;
    }
    if(!runtimeEffect){
        runtimeEffect = SSAOEffect::New();
    }
    runtimeEffect->radiusPx = Math3D::Max(0.25f, radiusPx);
    runtimeEffect->depthRadius = Math3D::Max(0.00001f, depthRadius);
    runtimeEffect->bias = Math3D::Max(0.0f, bias);
    runtimeEffect->intensity = Math3D::Clamp(intensity, 0.0f, 2.0f);
    runtimeEffect->giBoost = Math3D::Clamp(giBoost, 0.0f, 0.6f);
    runtimeEffect->sampleCount = Math3D::Clamp(sampleCount, 1, 16);
    runtimeEffect->nearPlane = Math3D::Max(0.001f, settings.nearPlane);
    runtimeEffect->farPlane = Math3D::Max(runtimeEffect->nearPlane + 0.001f, settings.farPlane);
    return runtimeEffect;
}

void SSAOComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!ImGui::CollapsingHeader("SSAO / GI Effect", ImGuiTreeNodeFlags_DefaultOpen)){
        return;
    }

    ImGui::Checkbox("Enabled", &enabled);
    if(!enabled){
        ImGui::TextDisabled("Effect disabled.");
        return;
    }

    ImGui::SliderFloat("Radius (Px)", &radiusPx, 0.25f, 12.0f, "%.2f");
    ImGui::SliderFloat("Depth Radius", &depthRadius, 0.00001f, 0.2f, "%.5f");
    ImGui::SliderFloat("Bias", &bias, 0.0f, 0.05f, "%.4f");
    ImGui::SliderFloat("AO Strength", &intensity, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("GI Boost", &giBoost, 0.0f, 0.6f, "%.2f");
    ImGui::SliderInt("Samples", &sampleCount, 1, 16);
}

Graphics::PostProcessing::PPostProcessingEffect DepthOfFieldComponent::getEffectForCamera(const CameraSettings& settings){
    if(!enabled){
        return nullptr;
    }
    if(!runtimeEffect){
        runtimeEffect = DepthOfFieldEffect::New();
    }
    runtimeEffect->focusDistance = Math3D::Max(0.01f, focusDistance);
    runtimeEffect->focusRange = Math3D::Max(0.001f, focusRange);
    runtimeEffect->blurStrength = Math3D::Clamp(blurStrength, 0.0f, 1.5f);
    runtimeEffect->maxBlurPx = Math3D::Clamp(maxBlurPx, 0.0f, 16.0f);
    runtimeEffect->sampleCount = Math3D::Clamp(sampleCount, 1, 8);
    runtimeEffect->nearPlane = Math3D::Max(0.001f, settings.nearPlane);
    runtimeEffect->farPlane = Math3D::Max(runtimeEffect->nearPlane + 0.001f, settings.farPlane);
    return runtimeEffect;
}

void DepthOfFieldComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!ImGui::CollapsingHeader("Depth Of Field Effect", ImGuiTreeNodeFlags_DefaultOpen)){
        return;
    }

    ImGui::Checkbox("Enabled", &enabled);
    if(!enabled){
        ImGui::TextDisabled("Effect disabled.");
        return;
    }

    ImGui::DragFloat("Focus Distance", &focusDistance, 0.1f, 0.01f, 10000.0f, "%.2f");
    ImGui::DragFloat("Focus Range", &focusRange, 0.05f, 0.001f, 10000.0f, "%.3f");
    ImGui::SliderFloat("Blur Strength", &blurStrength, 0.0f, 1.5f, "%.2f");
    ImGui::SliderFloat("Max Blur (Px)", &maxBlurPx, 0.0f, 16.0f, "%.1f");
    ImGui::SliderInt("Samples", &sampleCount, 1, 8);
}

Graphics::PostProcessing::PPostProcessingEffect AntiAliasingComponent::getEffectForCamera(const CameraSettings& settings){
    (void)settings;
    if(preset == AntiAliasingPreset::Off){
        return nullptr;
    }
    if(!runtimeEffect){
        runtimeEffect = FXAAEffect::New();
    }
    runtimeEffect->preset = preset;
    return runtimeEffect;
}

void AntiAliasingComponent::drawPropertyWidget(NeoECS::NeoECS* ecsPtr, PScene scene){
    (void)ecsPtr;
    (void)scene;
    if(!ImGui::CollapsingHeader("Anti-Aliasing Effect", ImGuiTreeNodeFlags_DefaultOpen)){
        return;
    }

    const char* options[] = {
        "Off",
        "FXAA - Low",
        "FXAA - Medium",
        "FXAA - High"
    };
    int item = static_cast<int>(preset);
    if(ImGui::Combo("Preset", &item, options, IM_ARRAYSIZE(options))){
        preset = static_cast<AntiAliasingPreset>(item);
    }
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
    if(!ImGui::CollapsingHeader("Script Component", ImGuiTreeNodeFlags_DefaultOpen)){
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
