#include "Widgets/FilePreviewWidget.h"

#include "EditorAssetUI.h"
#include "Asset.h"
#include "Environment.h"
#include "FrameBuffer.h"
#include "MaterialDefaults.h"
#include "ModelPartPrefabs.h"
#include "Model.h"
#include "OBJLoader.h"
#include "PBRMaterial.h"
#include "Screen.h"
#include "ShaderAsset.h"
#include "SkyBox.h"
#include "StringUtils.h"
#include "Texture.h"
#include "imgui.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
    constexpr bool kEnableRaytraceShaderStage = false;
    constexpr float kMaterialPreviewMinDistance = 2.2f;
    std::shared_ptr<Material> g_previewFallbackMaterial;

    std::shared_ptr<Material> buildPreviewFallbackMaterial(){
        auto isRenderable = [](const std::shared_ptr<Material>& mat) -> bool{
            return mat && mat->getShader() && mat->getShader()->getID() != 0;
        };

        std::shared_ptr<Material> fallback = MaterialDefaults::ColorMaterial::Create(Color::WHITE);
        if(isRenderable(fallback)){
            return fallback;
        }

        fallback = MaterialDefaults::LitColorMaterial::Create(Color::WHITE);
        if(isRenderable(fallback)){
            return fallback;
        }

        fallback = PBRMaterial::Create(Color::WHITE);
        if(isRenderable(fallback)){
            return fallback;
        }

        return nullptr;
    }

    std::shared_ptr<Material> getRenderablePreviewMaterial(const std::shared_ptr<Material>& candidate){
        if(candidate){
            auto shader = candidate->getShader();
            if(shader && shader->getID() != 0){
                return candidate;
            }
        }

        if(!g_previewFallbackMaterial ||
           !g_previewFallbackMaterial->getShader() ||
           g_previewFallbackMaterial->getShader()->getID() == 0){
            g_previewFallbackMaterial = buildPreviewFallbackMaterial();
        }

        if(g_previewFallbackMaterial &&
           g_previewFallbackMaterial->getShader() &&
           g_previewFallbackMaterial->getShader()->getID() != 0){
            return g_previewFallbackMaterial;
        }

        return nullptr;
    }

    void copyBuffer(char* dst, size_t dstSize, const std::string& src){
        if(!dst || dstSize == 0){
            return;
        }
        std::memset(dst, 0, dstSize);
        if(src.empty()){
            return;
        }
        std::strncpy(dst, src.c_str(), dstSize - 1);
        dst[dstSize - 1] = '\0';
    }

    std::string toAssetRef(const std::filesystem::path& absolutePath, const std::filesystem::path& assetRoot){
        if(absolutePath.empty()){
            return "";
        }
        if(assetRoot.empty()){
            return absolutePath.generic_string();
        }

        std::error_code ec;
        std::filesystem::path rel = std::filesystem::weakly_canonical(absolutePath, ec).lexically_relative(std::filesystem::weakly_canonical(assetRoot, ec));
        if(!rel.empty() && !StringUtils::BeginsWith(rel.generic_string(), "..")){
            return std::string(ASSET_DELIMITER) + "/" + rel.generic_string();
        }
        return absolutePath.generic_string();
    }

    std::shared_ptr<Texture> loadTextureAsset(const std::string& assetRef){
        if(assetRef.empty()){
            return nullptr;
        }
        auto asset = AssetManager::Instance.getOrLoad(assetRef);
        if(!asset){
            return nullptr;
        }
        return Texture::Load(asset);
    }

    void drawTexturePreviewSmall(const std::string& assetRef){
        if(assetRef.empty()){
            ImGui::BeginDisabled();
            ImGui::Button("None", ImVec2(44.0f, 44.0f));
            ImGui::EndDisabled();
            return;
        }

        auto tex = loadTextureAsset(assetRef);
        if(tex && tex->getID() != 0){
            ImTextureID texId = (ImTextureID)(intptr_t)tex->getID();
            ImGui::Image(texId, ImVec2(44.0f, 44.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            return;
        }

        ImGui::BeginDisabled();
        ImGui::Button("Missing", ImVec2(44.0f, 44.0f));
        ImGui::EndDisabled();
    }

    bool isModelPath(const std::filesystem::path& path){
        const std::string ext = StringUtils::ToLowerCase(path.extension().string());
        return (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb");
    }

    bool computeModelBounds(const std::shared_ptr<Model>& model, Math3D::Vec3& outCenter, float& outRadius){
        if(!model){
            return false;
        }

        bool hasBounds = false;
        Math3D::Vec3 minV(0.0f, 0.0f, 0.0f);
        Math3D::Vec3 maxV(0.0f, 0.0f, 0.0f);
        const auto& parts = model->getParts();
        for(const auto& part : parts){
            if(!part || !part->mesh){
                continue;
            }

            Math3D::Vec3 localMin;
            Math3D::Vec3 localMax;
            if(!part->mesh->getLocalBounds(localMin, localMax)){
                continue;
            }

            const Math3D::Mat4 partMat = part->localTransform.toMat4();
            const Math3D::Vec3 corners[8] = {
                Math3D::Vec3(localMin.x, localMin.y, localMin.z),
                Math3D::Vec3(localMin.x, localMin.y, localMax.z),
                Math3D::Vec3(localMin.x, localMax.y, localMin.z),
                Math3D::Vec3(localMin.x, localMax.y, localMax.z),
                Math3D::Vec3(localMax.x, localMin.y, localMin.z),
                Math3D::Vec3(localMax.x, localMin.y, localMax.z),
                Math3D::Vec3(localMax.x, localMax.y, localMin.z),
                Math3D::Vec3(localMax.x, localMax.y, localMax.z)
            };

            for(const Math3D::Vec3& corner : corners){
                Math3D::Vec3 transformed = Math3D::Transform::transformPoint(partMat, corner);
                if(!hasBounds){
                    minV = transformed;
                    maxV = transformed;
                    hasBounds = true;
                    continue;
                }
                minV.x = Math3D::Min(minV.x, transformed.x);
                minV.y = Math3D::Min(minV.y, transformed.y);
                minV.z = Math3D::Min(minV.z, transformed.z);
                maxV.x = Math3D::Max(maxV.x, transformed.x);
                maxV.y = Math3D::Max(maxV.y, transformed.y);
                maxV.z = Math3D::Max(maxV.z, transformed.z);
            }
        }

        if(!hasBounds){
            return false;
        }

        outCenter = Math3D::Vec3(
            (minV.x + maxV.x) * 0.5f,
            (minV.y + maxV.y) * 0.5f,
            (minV.z + maxV.z) * 0.5f
        );
        outRadius = (maxV - minV).length() * 0.5f;
        if(outRadius <= Math3D::EPSILON){
            outRadius = 1.0f;
        }
        return true;
    }
}

void FilePreviewWidget::setAssetRoot(const std::filesystem::path& rootPath){
    assetRoot = rootPath;
}

void FilePreviewWidget::setFilePath(const std::filesystem::path& path){
    if(filePath == path){
        return;
    }
    filePath = path;
    hasLoadedData = false;
    statusMessage.clear();
    statusIsError = false;
    previewMaterial.reset();
    previewModel.reset();
    previewMaterialDirty = true;
    previewModelDirty = true;
    previewOrbitYaw = 45.0f;
    previewOrbitPitch = 22.5f;
    previewOrbitDistance = 4.25f;
    reloadFromDisk(true);
}

void FilePreviewWidget::reloadFromDisk(bool force){
    if(filePath.empty()){
        return;
    }
    if(!force && hasLoadedData){
        return;
    }

    hasLoadedData = true;
    statusMessage.clear();
    statusIsError = false;
    isShaderAssetFile = ShaderAssetIO::IsShaderAssetPath(filePath);
    isMaterialAssetFile = MaterialAssetIO::IsMaterialAssetPath(filePath);
    isMaterialObjectFile = MaterialAssetIO::IsMaterialObjectPath(filePath);
    isModelFile = isModelPath(filePath);

    std::error_code ec;
    if(std::filesystem::exists(filePath, ec)){
        lastWriteTime = std::filesystem::last_write_time(filePath, ec);
    }

    if(isShaderAssetFile){
        ShaderAssetData data;
        std::string error;
        if(!ShaderAssetIO::LoadFromAbsolutePath(filePath, data, &error)){
            statusIsError = true;
            statusMessage = error;
            return;
        }

        bundledShaderData = data;
        copyBuffer(cacheName, sizeof(cacheName), data.cacheName);
        return;
    }

    if(isMaterialAssetFile){
        std::string error;
        if(!MaterialAssetIO::LoadFromAbsolutePath(filePath, materialData, &error)){
            statusIsError = true;
            statusMessage = error;
            return;
        }
        copyBuffer(materialName, sizeof(materialName), materialData.name);
        copyBuffer(materialShaderAsset, sizeof(materialShaderAsset), materialData.shaderAssetRef);
        copyBuffer(materialTexture, sizeof(materialTexture), materialData.textureRef);
        copyBuffer(materialBaseColorTex, sizeof(materialBaseColorTex), materialData.baseColorTexRef);
        copyBuffer(materialRoughnessTex, sizeof(materialRoughnessTex), materialData.roughnessTexRef);
        copyBuffer(materialMetalRoughTex, sizeof(materialMetalRoughTex), materialData.metallicRoughnessTexRef);
        copyBuffer(materialNormalTex, sizeof(materialNormalTex), materialData.normalTexRef);
        copyBuffer(materialHeightTex, sizeof(materialHeightTex), materialData.heightTexRef);
        copyBuffer(materialEmissiveTex, sizeof(materialEmissiveTex), materialData.emissiveTexRef);
        copyBuffer(materialOcclusionTex, sizeof(materialOcclusionTex), materialData.occlusionTexRef);
        previewMaterialDirty = true;
        return;
    }

    if(isMaterialObjectFile){
        std::string error;
        if(!MaterialAssetIO::LoadMaterialObjectFromAbsolutePath(filePath, materialObjectData, &error)){
            statusIsError = true;
            statusMessage = error;
            return;
        }
        copyBuffer(materialObjectName, sizeof(materialObjectName), materialObjectData.name);
        copyBuffer(materialObjectAssetRef, sizeof(materialObjectAssetRef), materialObjectData.materialAssetRef);
        previewMaterialDirty = true;
        return;
    }

    if(isModelFile){
        previewModelDirty = true;
        return;
    }
}

void FilePreviewWidget::draw(){
    if(filePath.empty()){
        ImGui::TextUnformatted("No file selected.");
        return;
    }

    std::error_code ec;
    if(std::filesystem::exists(filePath, ec)){
        const auto onDiskWriteTime = std::filesystem::last_write_time(filePath, ec);
        if(!ec && hasLoadedData && onDiskWriteTime != lastWriteTime){
            hasLoadedData = false;
        }
    }
    reloadFromDisk(false);

    ImGui::Text("File: %s", filePath.filename().string().c_str());
    ImGui::TextDisabled("%s", toAssetRef(filePath, assetRoot).c_str());
    ImGui::Separator();

    if(isShaderAssetFile){
        drawShaderAssetEditor();
        return;
    }
    if(isMaterialAssetFile){
        drawMaterialAssetEditor();
        return;
    }
    if(isMaterialObjectFile){
        drawMaterialObjectEditor();
        return;
    }
    if(isModelFile){
        drawModelFilePreview();
        return;
    }
    drawGenericInfo();
}

void FilePreviewWidget::drawShaderAssetEditor(){
    bool changed = false;
    changed |= ImGui::InputText("Cache Name", cacheName, sizeof(cacheName));
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Vertex Shader",
        bundledShaderData.vertexAssetRef,
        {EditorAssetUI::AssetKind::ShaderVertex, EditorAssetUI::AssetKind::ShaderGeneric}
    );
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Fragment Shader",
        bundledShaderData.fragmentAssetRef,
        {EditorAssetUI::AssetKind::ShaderFragment, EditorAssetUI::AssetKind::ShaderGeneric}
    );
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Geometry Shader",
        bundledShaderData.geometryAssetRef,
        {EditorAssetUI::AssetKind::ShaderGrometry, EditorAssetUI::AssetKind::ShaderGeneric}
    );
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Tesselation Shader",
        bundledShaderData.tesselationAssetRef,
        {EditorAssetUI::AssetKind::ShaderTesselation, EditorAssetUI::AssetKind::ShaderGeneric}
    );
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Compute Shader",
        bundledShaderData.computeAssetRef,
        {EditorAssetUI::AssetKind::ShaderCompute, EditorAssetUI::AssetKind::ShaderGeneric}
    );
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Task Shader",
        bundledShaderData.taskAssetRef,
        {EditorAssetUI::AssetKind::ShaderTask, EditorAssetUI::AssetKind::ShaderGeneric}
    );

    if(kEnableRaytraceShaderStage){
        changed |= EditorAssetUI::DrawAssetDropInput(
            "Raytrace Shader",
            bundledShaderData.rtAssetRef,
            {EditorAssetUI::AssetKind::ShaderRaytrace, EditorAssetUI::AssetKind::ShaderGeneric}
        );
    }else{
        ImGui::BeginDisabled();
        EditorAssetUI::DrawAssetDropInput(
            "Raytrace Shader (Disabled)",
            bundledShaderData.rtAssetRef,
            {EditorAssetUI::AssetKind::ShaderRaytrace, EditorAssetUI::AssetKind::ShaderGeneric},
            true
        );
        ImGui::EndDisabled();
    }

    if(changed){
        bundledShaderData.cacheName = cacheName;

        std::string error;
        if(ShaderAssetIO::SaveToAbsolutePath(filePath, bundledShaderData, &error)){
            statusIsError = false;
            statusMessage = "Saved.";
            std::error_code ec;
            lastWriteTime = std::filesystem::last_write_time(filePath, ec);
        }else{
            statusIsError = true;
            statusMessage = error;
        }
    }

    if(statusMessage.empty()){
        ImGui::TextDisabled("Edits save automatically.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }
}

void FilePreviewWidget::drawMaterialAssetEditor(){
    static constexpr std::array<const char*, 7> kTypeNames = {
        "PBR", "Color", "Image", "LitColor", "LitImage", "FlatColor", "FlatImage"
    };

    bool changed = false;
    changed |= ImGui::InputText("Material Name", materialName, sizeof(materialName));

    int typeIndex = static_cast<int>(materialData.type);
    if(typeIndex < 0 || typeIndex >= static_cast<int>(kTypeNames.size())){
        typeIndex = 0;
    }
    if(ImGui::Combo("Material Type", &typeIndex, kTypeNames.data(), static_cast<int>(kTypeNames.size()))){
        materialData.type = static_cast<MaterialAssetType>(typeIndex);
        changed = true;
    }

    changed |= EditorAssetUI::DrawAssetDropInput("Shader Asset", materialShaderAsset, sizeof(materialShaderAsset), EditorAssetUI::AssetKind::ShaderAsset);

    bool castsShadows = materialData.castsShadows;
    if(ImGui::Checkbox("Casts Shadows", &castsShadows)){
        materialData.castsShadows = castsShadows;
        changed = true;
    }

    bool receivesShadows = materialData.receivesShadows;
    if(ImGui::Checkbox("Receives Shadows", &receivesShadows)){
        materialData.receivesShadows = receivesShadows;
        changed = true;
    }

    auto drawTextureField = [&](const char* label, char* pathBuffer, size_t bufferSize){
        bool localChanged = false;
        localChanged |= EditorAssetUI::DrawAssetDropInput(label, pathBuffer, bufferSize, EditorAssetUI::AssetKind::Image);
        ImGui::PushID(label);
        drawTexturePreviewSmall(pathBuffer);
        ImGui::PopID();
        ImGui::Spacing();
        return localChanged;
    };

    switch(materialData.type){
        case MaterialAssetType::PBR:{
            Math3D::Vec4 color = materialData.color;
            if(ImGui::ColorEdit4("Base Color", &color.x)){
                materialData.color = color;
                changed = true;
            }
            changed |= ImGui::SliderFloat("Metallic", &materialData.metallic, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Roughness", &materialData.roughness, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("Normal Scale", &materialData.normalScale, 0.01f, 0.0f, 8.0f);
            changed |= ImGui::DragFloat("Height Scale", &materialData.heightScale, 0.001f, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("Emissive Strength", &materialData.emissiveStrength, 0.01f, 0.0f, 32.0f);
            changed |= ImGui::SliderFloat("AO Strength", &materialData.occlusionStrength, 0.0f, 4.0f);
            changed |= ImGui::DragFloat("Env Strength", &materialData.envStrength, 0.01f, 0.0f, 8.0f);

            bool useEnvMap = (materialData.useEnvMap != 0);
            if(ImGui::Checkbox("Use Env Map", &useEnvMap)){
                materialData.useEnvMap = useEnvMap ? 1 : 0;
                changed = true;
            }

            bool useAlphaClip = (materialData.useAlphaClip != 0);
            if(ImGui::Checkbox("Use Alpha Clip", &useAlphaClip)){
                materialData.useAlphaClip = useAlphaClip ? 1 : 0;
                changed = true;
            }
            if(materialData.useAlphaClip != 0){
                changed |= ImGui::SliderFloat("Alpha Cutoff", &materialData.alphaCutoff, 0.0f, 1.0f);
            }

            changed |= drawTextureField("Base Color Tex", materialBaseColorTex, sizeof(materialBaseColorTex));
            changed |= drawTextureField("Roughness Tex", materialRoughnessTex, sizeof(materialRoughnessTex));
            changed |= drawTextureField("Metal/Rough Tex", materialMetalRoughTex, sizeof(materialMetalRoughTex));
            changed |= drawTextureField("Normal Tex", materialNormalTex, sizeof(materialNormalTex));
            changed |= drawTextureField("Height Tex", materialHeightTex, sizeof(materialHeightTex));
            changed |= drawTextureField("Emissive Tex", materialEmissiveTex, sizeof(materialEmissiveTex));
            changed |= drawTextureField("Occlusion Tex", materialOcclusionTex, sizeof(materialOcclusionTex));
            break;
        }
        case MaterialAssetType::Image:
        case MaterialAssetType::LitImage:
        case MaterialAssetType::FlatImage:{
            Math3D::Vec4 color = materialData.color;
            if(ImGui::ColorEdit4("Color", &color.x)){
                materialData.color = color;
                changed = true;
            }
            changed |= drawTextureField("Texture", materialTexture, sizeof(materialTexture));
            if(materialData.type == MaterialAssetType::Image){
                changed |= ImGui::DragFloat2("UV", &materialData.uv.x, 0.01f, -64.0f, 64.0f);
            }
            break;
        }
        case MaterialAssetType::Color:
        case MaterialAssetType::LitColor:
        case MaterialAssetType::FlatColor:
        default:{
            Math3D::Vec4 color = materialData.color;
            if(ImGui::ColorEdit4("Color", &color.x)){
                materialData.color = color;
                changed = true;
            }
            break;
        }
    }

    if(changed){
        materialData.name = materialName;
        materialData.type = static_cast<MaterialAssetType>(typeIndex);
        materialData.shaderAssetRef = materialShaderAsset;
        materialData.textureRef = materialTexture;
        materialData.baseColorTexRef = materialBaseColorTex;
        materialData.roughnessTexRef = materialRoughnessTex;
        materialData.metallicRoughnessTexRef = materialMetalRoughTex;
        materialData.normalTexRef = materialNormalTex;
        materialData.heightTexRef = materialHeightTex;
        materialData.emissiveTexRef = materialEmissiveTex;
        materialData.occlusionTexRef = materialOcclusionTex;

        std::string error;
        if(MaterialAssetIO::SaveToAbsolutePath(filePath, materialData, &error)){
            statusIsError = false;
            statusMessage = "Saved.";
            std::error_code ec;
            lastWriteTime = std::filesystem::last_write_time(filePath, ec);
            previewMaterialDirty = true;
        }else{
            statusIsError = true;
            statusMessage = error;
        }
    }

    ensureMaterialPreviewResources(previewSize);
    bool materialNeedsRender = false;
    if(previewMaterialDirty || !previewMaterial){
        std::string error;
        previewMaterial = MaterialAssetIO::InstantiateMaterial(materialData, &error);
        if(!previewMaterial){
            if(error.empty()){
                error = "Failed to instantiate preview material.";
            }
            statusIsError = true;
            statusMessage = error;
        }else{
            if(auto pbr = Material::GetAs<PBRMaterial>(previewMaterial)){
                if(previewSkyBox && previewSkyBox->getCubeMap()){
                    pbr->EnvMap = previewSkyBox->getCubeMap();
                    if(pbr->UseEnvMap.get() == 0){
                        pbr->UseEnvMap = 1;
                    }
                }
            }
            materialNeedsRender = true;
        }
        previewMaterialDirty = false;
    }

    if(!previewTexture || previewTexture->getID() == 0){
        materialNeedsRender = true;
    }
    if(materialNeedsRender){
        renderMaterialPreview(previewMaterial);
    }

    bool previewInteractionChanged = false;
    if(ImGui::TreeNodeEx("Preview", ImGuiTreeNodeFlags_DefaultOpen)){
        if(previewTexture && previewTexture->getID() != 0){
            ImTextureID texId = (ImTextureID)(intptr_t)previewTexture->getID();
            ImGui::Image(texId, ImVec2((float)previewSize, (float)previewSize), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            previewInteractionChanged = handlePreviewOrbitInput() || previewInteractionChanged;
        }else{
            ImGui::BeginDisabled();
            ImGui::Button("Preview Unavailable", ImVec2((float)previewSize, (float)previewSize));
            ImGui::EndDisabled();
        }
        ImGui::TreePop();
    }
    if(previewInteractionChanged){
        renderMaterialPreview(previewMaterial);
    }

    if(statusMessage.empty()){
        ImGui::TextDisabled("Edits save automatically.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }
}

void FilePreviewWidget::drawMaterialObjectEditor(){
    bool changed = false;
    changed |= ImGui::InputText("Material Name", materialObjectName, sizeof(materialObjectName));
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Material Asset",
        materialObjectAssetRef,
        sizeof(materialObjectAssetRef),
        EditorAssetUI::AssetKind::Material
    );

    if(changed){
        materialObjectData.name = materialObjectName;
        materialObjectData.materialAssetRef = materialObjectAssetRef;

        std::string error;
        if(MaterialAssetIO::SaveMaterialObjectToAbsolutePath(filePath, materialObjectData, &error)){
            statusIsError = false;
            statusMessage = "Saved.";
            std::error_code ec;
            lastWriteTime = std::filesystem::last_write_time(filePath, ec);
            previewMaterialDirty = true;
        }else{
            statusIsError = true;
            statusMessage = error;
        }
    }

    std::string resolvedAssetRef;
    std::string resolveError;
    bool hasResolvedAsset = MaterialAssetIO::ResolveMaterialAssetRef(materialObjectAssetRef, resolvedAssetRef, &resolveError);
    if(hasResolvedAsset){
        ImGui::TextDisabled("Resolved: %s", resolvedAssetRef.c_str());
    }else{
        ImGui::TextDisabled("Resolved: <none>");
    }

    ensureMaterialPreviewResources(previewSize);
    bool materialNeedsRender = false;
    if(previewMaterialDirty || !previewMaterial){
        std::string error;
        previewMaterial = MaterialAssetIO::InstantiateMaterialFromRef(materialObjectAssetRef, &resolvedAssetRef, &error);
        if(!previewMaterial){
            if(materialObjectAssetRef[0] != '\0'){
                if(error.empty()){
                    error = "Failed to instantiate material object preview.";
                }
                statusIsError = true;
                statusMessage = error;
            }
        }else{
            if(auto pbr = Material::GetAs<PBRMaterial>(previewMaterial)){
                if(previewSkyBox && previewSkyBox->getCubeMap()){
                    pbr->EnvMap = previewSkyBox->getCubeMap();
                    if(pbr->UseEnvMap.get() == 0){
                        pbr->UseEnvMap = 1;
                    }
                }
            }
            materialNeedsRender = true;
        }
        previewMaterialDirty = false;
    }

    if(!previewTexture || previewTexture->getID() == 0){
        materialNeedsRender = true;
    }
    if(materialNeedsRender){
        renderMaterialPreview(previewMaterial);
    }

    bool previewInteractionChanged = false;
    if(ImGui::TreeNodeEx("Preview", ImGuiTreeNodeFlags_DefaultOpen)){
        if(previewTexture && previewTexture->getID() != 0){
            ImTextureID texId = (ImTextureID)(intptr_t)previewTexture->getID();
            ImGui::Image(texId, ImVec2((float)previewSize, (float)previewSize), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            previewInteractionChanged = handlePreviewOrbitInput() || previewInteractionChanged;
        }else{
            ImGui::BeginDisabled();
            ImGui::Button("Preview Unavailable", ImVec2((float)previewSize, (float)previewSize));
            ImGui::EndDisabled();
        }
        ImGui::TreePop();
    }
    if(previewInteractionChanged){
        renderMaterialPreview(previewMaterial);
    }

    if(statusMessage.empty()){
        ImGui::TextDisabled("Edits save automatically.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }
}

void FilePreviewWidget::drawModelFilePreview(){
    const std::string ext = StringUtils::ToLowerCase(filePath.extension().string());
    bool needsRender = false;

    if(previewModelDirty){
        previewModel.reset();
        statusMessage.clear();
        statusIsError = false;

        if(ext == ".obj"){
            std::string assetRef = toAssetRef(filePath, assetRoot);
            auto asset = AssetManager::Instance.getOrLoad(assetRef);
            if(!asset){
                asset = std::make_shared<Asset>(filePath.string());
                if(asset && !asset->load()){
                    asset.reset();
                }
            }

            auto baseMat = MaterialDefaults::LitColorMaterial::Create(Color::WHITE);
            previewModel = OBJLoader::LoadFromAsset(asset, baseMat, true);
            if(!previewModel){
                statusIsError = true;
                statusMessage = "Failed to load OBJ preview.";
            }
        }else{
            statusMessage = "Preview currently supports .obj models.";
        }

        previewModelDirty = false;
        needsRender = true;
    }

    ensureMaterialPreviewResources(previewSize);
    if(previewModel && (!previewTexture || previewTexture->getID() == 0)){
        needsRender = true;
    }
    if(previewModel && needsRender){
        renderModelPreview(previewModel);
    }

    bool previewInteractionChanged = false;
    if(previewTexture && previewTexture->getID() != 0 && previewModel){
        ImTextureID texId = (ImTextureID)(intptr_t)previewTexture->getID();
        ImGui::Image(texId, ImVec2((float)previewSize, (float)previewSize), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        previewInteractionChanged = handlePreviewOrbitInput() || previewInteractionChanged;
    }else{
        ImGui::BeginDisabled();
        ImGui::Button("Preview Unavailable", ImVec2((float)previewSize, (float)previewSize));
        ImGui::EndDisabled();
    }
    if(previewInteractionChanged && previewModel){
        renderModelPreview(previewModel);
    }

    if(statusMessage.empty()){
        ImGui::TextDisabled("Model preview rendered from source mesh.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextDisabled("%s", statusMessage.c_str());
    }
}

void FilePreviewWidget::drawGenericInfo() const{
    std::error_code ec;
    if(!std::filesystem::exists(filePath, ec)){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "File does not exist.");
        return;
    }
    if(std::filesystem::is_directory(filePath, ec)){
        ImGui::TextUnformatted("Directory selected.");
        return;
    }

    auto fileSize = std::filesystem::file_size(filePath, ec);
    if(!ec){
        ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(fileSize));
    }
    ImGui::TextDisabled("No preview widget for this file type.");
}

void FilePreviewWidget::ensureMaterialPreviewResources(int size){
    if(size < 32){
        size = 32;
    }

    const bool needsResize =
        (previewSize != size) ||
        !previewFrameBuffer ||
        !previewTexture ||
        (previewFrameBuffer->getWidth() != size) ||
        (previewFrameBuffer->getHeight() != size);

    if(needsResize){
        previewSize = size;
        previewFrameBuffer = FrameBuffer::Create(previewSize, previewSize);
        previewTexture = Texture::CreateEmpty(previewSize, previewSize);
        if(previewFrameBuffer && previewTexture){
            previewFrameBuffer->attachTexture(previewTexture);
        }
    }

    if(!previewCamera){
        previewCamera = Camera::CreatePerspective(
            45.0f,
            Math3D::Vec2((float)previewSize, (float)previewSize),
            0.1f,
            64.0f
        );
    }
    if(previewCamera){
        previewCamera->resize((float)previewSize, (float)previewSize);
        updatePreviewCameraFromOrbit();
    }

    if(!previewSphere){
        previewSphere = ModelPartPrefabs::MakeSphere(0.9f, 36, 24);
    }

    if(!previewSkyBox){
        previewSkyBox = SkyBoxLoader::CreateSkyBox("@assets/images/skybox/default", "skybox_default");
    }

    if(!previewEnvironment){
        previewEnvironment = std::make_shared<Environment>();
        previewEnvironment->setLightingEnabled(true);
        auto& lights = previewEnvironment->getLightManager();
        lights.clearLights();

        Light sun = Light::CreateDirectionalLight(
            Math3D::Vec3(-0.3f, -1.0f, -0.2f),
            Color::fromRGBA255(255, 208, 180, 255),
            0.70f
        );
        sun.castsShadows = false;
        lights.addLight(sun);

        Light key = Light::CreatePointLight(
            Math3D::Vec3(2.5f, 2.0f, 1.5f),
            Color::fromRGBA255(255, 230, 180, 255),
            4.0f,
            12.0f,
            2.0f
        );
        key.castsShadows = false;
        lights.addLight(key);

        Light fill = Light::CreatePointLight(
            Math3D::Vec3(-2.5f, 1.4f, -1.0f),
            Color::fromRGBA255(120, 180, 255, 255),
            2.0f,
            12.0f,
            2.0f
        );
        fill.castsShadows = false;
        lights.addLight(fill);
    }

    if(previewEnvironment){
        previewEnvironment->setSkyBox(previewSkyBox);
    }
}

void FilePreviewWidget::updatePreviewCameraFromOrbit(){
    if(!previewCamera){
        return;
    }

    previewOrbitDistance = Math3D::Clamp(previewOrbitDistance, kMaterialPreviewMinDistance, 12.0f);

    const float yawRad = previewOrbitYaw * ((float)Math3D::PI / 180.0f);
    const float pitchRad = previewOrbitPitch * ((float)Math3D::PI / 180.0f);
    const float cosPitch = std::cos(pitchRad);

    const Math3D::Vec3 camPos(
        previewOrbitDistance * cosPitch * std::cos(yawRad),
        previewOrbitDistance * std::sin(pitchRad),
        previewOrbitDistance * cosPitch * std::sin(yawRad)
    );
    previewCamera->transform().setPosition(camPos);
    previewCamera->transform().lookAt(Math3D::Vec3(0.0f, 0.0f, 0.0f));
}

bool FilePreviewWidget::handlePreviewOrbitInput(){
    if(!previewCamera || !ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly)){
        return false;
    }

    bool changed = false;
    const ImGuiIO& io = ImGui::GetIO();
    if(ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)){
        if(io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f){
            previewOrbitYaw += io.MouseDelta.x * 0.35f;
            previewOrbitPitch = Math3D::Clamp(previewOrbitPitch - io.MouseDelta.y * 0.35f, -80.0f, 80.0f);
            changed = true;
        }
    }

    if(io.MouseWheel != 0.0f){
        previewOrbitDistance = Math3D::Clamp(previewOrbitDistance - io.MouseWheel * 0.25f, kMaterialPreviewMinDistance, 12.0f);
        changed = true;
    }

    if(changed){
        updatePreviewCameraFromOrbit();
    }
    return changed;
}

void FilePreviewWidget::renderMaterialPreview(const std::shared_ptr<Material>& material){
    if(!previewFrameBuffer || !previewTexture || !previewSphere || !previewCamera){
        return;
    }

    auto renderMaterial = getRenderablePreviewMaterial(material);
    if(!renderMaterial){
        renderMaterial = getRenderablePreviewMaterial(previewSphere->material);
    }
    if(!renderMaterial){
        return;
    }

    auto previousCamera = Screen::GetCurrentCamera();
    auto previousEnvironment = Screen::GetCurrentEnvironment();

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    GLint previousFrontFace = GL_CCW;
    GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean wasCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean wasBlend = glIsEnabled(GL_BLEND);
    GLboolean previousDepthMask = GL_TRUE;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_FRONT_FACE, &previousFrontFace);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

    previewFrameBuffer->bind();
    previewFrameBuffer->clear(Color::fromRGB24(0x263248));

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    glDisable(GL_BLEND);

    Screen::MakeCameraCurrent(previewCamera);
    Screen::MakeEnvironmentCurrent(previewEnvironment);

    if(previewSkyBox){
        previewSkyBox->draw(previewCamera, false);
    }

    previewSphere->material = renderMaterial;

    if(previewSphere->material){
        if(auto pbr = Material::GetAs<PBRMaterial>(previewSphere->material)){
            if(previewSkyBox && previewSkyBox->getCubeMap()){
                pbr->EnvMap = previewSkyBox->getCubeMap();
            }
        }
    }

    previewSphere->localTransform.setRotation(12.0f, 26.0f, 0.0f);

    const Math3D::Mat4 identity;
    glFrontFace(GL_CW);
    previewSphere->draw(identity, previewCamera->getViewMatrix(), previewCamera->getProjectionMatrix());
    glFrontFace(GL_CCW);
    previewSphere->draw(identity, previewCamera->getViewMatrix(), previewCamera->getProjectionMatrix());

    previewFrameBuffer->unbind();

    if(previousFramebuffer != 0){
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
    }
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    if(wasDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if(wasCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if(wasBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glDepthMask(previousDepthMask);
    glFrontFace((GLenum)previousFrontFace);

    Screen::MakeCameraCurrent(previousCamera);
    Screen::MakeEnvironmentCurrent(previousEnvironment);
}

void FilePreviewWidget::renderModelPreview(const std::shared_ptr<Model>& model){
    if(!previewFrameBuffer || !previewTexture || !previewCamera || !model){
        return;
    }

    auto previousCamera = Screen::GetCurrentCamera();
    auto previousEnvironment = Screen::GetCurrentEnvironment();

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    GLboolean wasDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean wasCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean wasBlend = glIsEnabled(GL_BLEND);
    GLboolean previousDepthMask = GL_TRUE;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

    previewFrameBuffer->bind();
    previewFrameBuffer->clear(Color::fromRGB24(0x1f2735));

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    Screen::MakeCameraCurrent(previewCamera);
    Screen::MakeEnvironmentCurrent(previewEnvironment);

    if(previewSkyBox){
        previewSkyBox->draw(previewCamera, false);
    }

    model->setBackfaceCulling(true);
    Math3D::Vec3 center(0.0f, 0.0f, 0.0f);
    float radius = 1.0f;
    if(computeModelBounds(model, center, radius)){
        const float fitScale = 1.1f / Math3D::Max(radius, 0.0001f);
        model->transform().setPosition(Math3D::Vec3(-center.x * fitScale, -center.y * fitScale, -center.z * fitScale));
        model->transform().setScale(Math3D::Vec3(fitScale, fitScale, fitScale));
        model->transform().setRotation(0.0f, 24.0f, 0.0f);
    }else{
        model->transform().reset();
    }

    model->draw(previewCamera);

    previewFrameBuffer->unbind();

    if(previousFramebuffer != 0){
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
    }
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    if(wasDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if(wasCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if(wasBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glDepthMask(previousDepthMask);

    Screen::MakeCameraCurrent(previousCamera);
    Screen::MakeEnvironmentCurrent(previousEnvironment);
}
