#include "Widgets/FilePreviewWidget.h"

#include "EditorAssetUI.h"
#include "Asset.h"
#include "Environment.h"
#include "FrameBuffer.h"
#include "MaterialDefaults.h"
#include "ModelPartPrefabs.h"
#include "PBRMaterial.h"
#include "Screen.h"
#include "ShaderAsset.h"
#include "SkyBox.h"
#include "StringUtils.h"
#include "Texture.h"
#include "imgui.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace {
    constexpr bool kEnableRaytraceShaderStage = false;

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
    previewMaterialDirty = true;
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
        copyBuffer(materialMetalRoughTex, sizeof(materialMetalRoughTex), materialData.metallicRoughnessTexRef);
        copyBuffer(materialNormalTex, sizeof(materialNormalTex), materialData.normalTexRef);
        copyBuffer(materialEmissiveTex, sizeof(materialEmissiveTex), materialData.emissiveTexRef);
        copyBuffer(materialOcclusionTex, sizeof(materialOcclusionTex), materialData.occlusionTexRef);
        previewMaterialDirty = true;
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
        drawTexturePreviewSmall(pathBuffer);
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
            changed |= drawTextureField("Metal/Rough Tex", materialMetalRoughTex, sizeof(materialMetalRoughTex));
            changed |= drawTextureField("Normal Tex", materialNormalTex, sizeof(materialNormalTex));
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
        materialData.metallicRoughnessTexRef = materialMetalRoughTex;
        materialData.normalTexRef = materialNormalTex;
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
        }
        previewMaterialDirty = false;
    }

    if(ImGui::TreeNodeEx("Preview", ImGuiTreeNodeFlags_DefaultOpen)){
        renderMaterialPreview(previewMaterial);
        if(previewTexture && previewTexture->getID() != 0){
            ImTextureID texId = (ImTextureID)(intptr_t)previewTexture->getID();
            ImGui::Image(texId, ImVec2((float)previewSize, (float)previewSize), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }else{
            ImGui::BeginDisabled();
            ImGui::Button("Preview Unavailable", ImVec2((float)previewSize, (float)previewSize));
            ImGui::EndDisabled();
        }
        ImGui::TreePop();
    }

    if(statusMessage.empty()){
        ImGui::TextDisabled("Edits save automatically.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
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
        previewCamera->transform().setPosition(Math3D::Vec3(2.1f, 1.4f, 2.1f));
        previewCamera->transform().lookAt(Math3D::Vec3(0.0f, 0.0f, 0.0f));
    }

    if(!previewCube){
        previewCube = ModelPartPrefabs::MakeBox(
            0.65f,
            0.65f,
            0.65f,
            MaterialDefaults::ColorMaterial::Create(Color::WHITE)
        );
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

void FilePreviewWidget::renderMaterialPreview(const std::shared_ptr<Material>& material){
    if(!previewFrameBuffer || !previewTexture || !previewCube || !previewCamera){
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
    previewFrameBuffer->clear(Color::fromRGB24(0x263248));

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

    if(material){
        previewCube->material = material;
    }else if(!previewCube->material){
        previewCube->material = MaterialDefaults::ColorMaterial::Create(Color::MAGENTA);
    }

    if(previewCube->material){
        if(auto pbr = Material::GetAs<PBRMaterial>(previewCube->material)){
            if(previewSkyBox && previewSkyBox->getCubeMap()){
                pbr->EnvMap = previewSkyBox->getCubeMap();
            }
        }
    }

    previewCube->localTransform.setRotation(18.0f, (float)ImGui::GetTime() * 24.0f, 0.0f);

    const Math3D::Mat4 identity;
    previewCube->draw(identity, previewCamera->getViewMatrix(), previewCamera->getProjectionMatrix());

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
