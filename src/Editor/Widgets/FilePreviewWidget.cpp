/**
 * @file src/Editor/Widgets/FilePreviewWidget.cpp
 * @brief Implementation for FilePreviewWidget.
 */

#include "Editor/Widgets/FilePreviewWidget.h"

#include "Editor/Core/EditorAssetUI.h"
#include "Assets/Core/Asset.h"
#include "Assets/Core/AssetDescriptorUtils.h"
#include "Assets/Bundles/AssetBundleRegistry.h"
#include "Rendering/Materials/ConstructedMaterial.h"
#include "Rendering/Lighting/Environment.h"
#include "Rendering/Core/FrameBuffer.h"
#include "Rendering/Materials/MaterialDefaults.h"
#include "Assets/Importers/MtlMaterialImporter.h"
#include "Rendering/Geometry/ModelPartPrefabs.h"
#include "Rendering/Geometry/Model.h"
#include "Rendering/Materials/PBRMaterial.h"
#include "Rendering/Core/Screen.h"
#include "Assets/Descriptors/ShaderAsset.h"
#include "Rendering/Textures/SkyBox.h"
#include "Foundation/Logging/Logbot.h"
#include "Foundation/Util/StringUtils.h"
#include "Rendering/Textures/Texture.h"
#include "imgui.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {
    constexpr bool kEnableRaytraceShaderStage = false;
    constexpr float kMaterialPreviewMinDistance = 2.2f;
    std::shared_ptr<Material> g_previewFallbackMaterial;
    /// @brief Represents Texture Preview Cache Entry data.
    struct TexturePreviewCacheEntry{
        std::shared_ptr<Texture> texture;
        std::filesystem::file_time_type writeTime{};
        bool hasWriteTime = false;
        int lastValidationFrame = -100000;
        int lastUsedFrame = -100000;
    };
    std::unordered_map<std::string, TexturePreviewCacheEntry> g_texturePreviewCache;
    constexpr int kTexturePreviewCachePruneIntervalFrames = 120;
    constexpr int kTexturePreviewCacheIdleFrames = 600;
    constexpr size_t kMaxTexturePreviewCacheEntries = 48;
    constexpr int kTexturePreviewCacheValidationIntervalFrames = 30;
    constexpr int kFilePreviewWriteTimeValidationIntervalFrames = 15;
    int g_texturePreviewCacheLastPruneFrame = -100000;

    void pruneTexturePreviewCache(int frameNow){
        if((frameNow - g_texturePreviewCacheLastPruneFrame) < kTexturePreviewCachePruneIntervalFrames){
            return;
        }
        g_texturePreviewCacheLastPruneFrame = frameNow;

        for(auto it = g_texturePreviewCache.begin(); it != g_texturePreviewCache.end();){
            if((frameNow - it->second.lastUsedFrame) > kTexturePreviewCacheIdleFrames){
                it = g_texturePreviewCache.erase(it);
            }else{
                ++it;
            }
        }

        while(g_texturePreviewCache.size() > kMaxTexturePreviewCacheEntries){
            auto oldestIt = g_texturePreviewCache.end();
            for(auto it = g_texturePreviewCache.begin(); it != g_texturePreviewCache.end(); ++it){
                if(oldestIt == g_texturePreviewCache.end() ||
                   it->second.lastUsedFrame < oldestIt->second.lastUsedFrame){
                    oldestIt = it;
                }
            }
            if(oldestIt == g_texturePreviewCache.end()){
                break;
            }
            g_texturePreviewCache.erase(oldestIt);
        }
    }

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
        const std::string resolved = AssetDescriptorUtils::AbsolutePathToAssetRef(absolutePath);
        if(!resolved.empty()){
            return resolved;
        }
        if(assetRoot.empty()){
            return absolutePath.generic_string();
        }

        return absolutePath.generic_string();
    }

    bool pathExists(const std::filesystem::path& path, bool* outIsDirectory = nullptr){
        return AssetDescriptorUtils::PathExists(path, outIsDirectory);
    }

    std::filesystem::path backingWritePath(const std::filesystem::path& path){
        std::filesystem::path bundlePath;
        std::string entryPath;
        if(AssetBundleRegistry::DecodeVirtualEntryPath(path, bundlePath, entryPath)){
            return bundlePath;
        }
        return path;
    }

    std::string buildRawByteDump(const BinaryBuffer& bytes){
        const size_t maxBytes = 4096;
        const size_t shownBytes = std::min(bytes.size(), maxBytes);
        const bool truncated = bytes.size() > maxBytes;

        std::ostringstream oss;
        oss << "Length: " << bytes.size() << " bytes";
        if(truncated){
            oss << " (showing first " << maxBytes << ")";
        }
        oss << "\n\n";

        if(bytes.empty()){
            oss << "<empty>";
            return oss.str();
        }

        for(size_t offset = 0; offset < shownBytes; offset += 16){
            oss << std::uppercase << std::hex << std::setfill('0') << std::setw(6) << offset << ": ";
            for(size_t i = 0; i < 16; ++i){
                const size_t index = offset + i;
                if(index < shownBytes){
                    oss << std::setw(2) << static_cast<unsigned int>(bytes[index]) << ' ';
                }else{
                    oss << "   ";
                }
            }

            oss << " |";
            for(size_t i = 0; i < 16; ++i){
                const size_t index = offset + i;
                if(index < shownBytes){
                    const unsigned char c = bytes[index];
                    if(c >= 32 && c <= 126){
                        oss << static_cast<char>(c);
                    }else{
                        oss << '.';
                    }
                }else{
                    oss << ' ';
                }
            }
            oss << "|\n";
        }

        return oss.str();
    }

    std::string buildRawByteDump(const std::filesystem::path& path){
        std::error_code ec;
        std::filesystem::path bundlePath;
        std::string entryPath;
        if(AssetBundleRegistry::DecodeVirtualEntryPath(path, bundlePath, entryPath)){
            std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
            if(!bundle || entryPath.empty()){
                return "Unable to open bundle entry for raw-byte dump.";
            }

            BinaryBuffer data;
            std::string error;
            if(!bundle->readEntryBytes(entryPath, data, &error)){
                return error.empty() ? "Unable to open bundle entry for raw-byte dump." : error;
            }
            return buildRawByteDump(data);
        }

        std::ifstream file(path, std::ios::binary);
        if(!file.is_open()){
            return "Unable to open file for raw-byte dump.";
        }

        std::vector<unsigned char> bytes;
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        file.seekg(0, std::ios::beg);
        if(size > 0){
            bytes.resize(static_cast<size_t>(size));
            file.read(reinterpret_cast<char*>(bytes.data()), size);
        }

        return buildRawByteDump(bytes);
    }

    std::shared_ptr<Texture> loadTextureAsset(const std::string& assetRef){
        if(assetRef.empty()){
            return nullptr;
        }
        const int frameNow = ImGui::GetFrameCount();
        pruneTexturePreviewCache(frameNow);

        auto cachedIt = g_texturePreviewCache.find(assetRef);
        if(cachedIt != g_texturePreviewCache.end()){
            TexturePreviewCacheEntry& cached = cachedIt->second;
            if(cached.texture){
                cached.lastUsedFrame = frameNow;
                const bool needsValidation =
                    (frameNow - cached.lastValidationFrame) >= kTexturePreviewCacheValidationIntervalFrames;
                if(!needsValidation){
                    return cached.texture;
                }

                std::filesystem::path absolutePath;
                if(!AssetDescriptorUtils::AssetRefToAbsolutePath(assetRef, absolutePath)){
                    absolutePath = std::filesystem::path(assetRef);
                }

                std::filesystem::path writePath = backingWritePath(absolutePath);
                std::error_code ec;
                bool hasWriteTime = false;
                std::filesystem::file_time_type writeTime{};
                if(std::filesystem::exists(writePath, ec) && !std::filesystem::is_directory(writePath, ec)){
                    writeTime = std::filesystem::last_write_time(writePath, ec);
                    hasWriteTime = !ec;
                }
                cached.lastValidationFrame = frameNow;

                const bool cacheValid =
                    (cached.hasWriteTime == hasWriteTime) &&
                    (!hasWriteTime || cached.writeTime == writeTime);
                if(cacheValid){
                    return cached.texture;
                }
            }
        }

        auto asset = AssetManager::Instance.getOrLoad(assetRef);
        if(!asset){
            return nullptr;
        }
        auto tex = Texture::Load(asset);
        if(!tex){
            return nullptr;
        }

        TexturePreviewCacheEntry entry;
        entry.texture = tex;
        entry.lastValidationFrame = frameNow;
        entry.lastUsedFrame = frameNow;

        std::filesystem::path absolutePath;
        if(!AssetDescriptorUtils::AssetRefToAbsolutePath(assetRef, absolutePath)){
            absolutePath = std::filesystem::path(assetRef);
        }
        const std::filesystem::path writePath = backingWritePath(absolutePath);
        std::error_code ec;
        if(std::filesystem::exists(writePath, ec) && !std::filesystem::is_directory(writePath, ec)){
            entry.writeTime = std::filesystem::last_write_time(writePath, ec);
            entry.hasWriteTime = !ec;
        }

        g_texturePreviewCache[assetRef] = entry;
        return tex;
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

    bool isMtlPath(const std::filesystem::path& path){
        return StringUtils::ToLowerCase(path.extension().string()) == ".mtl";
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
    lastExternalWriteTimeValidationFrame = -100000;
    hasLoadedData = false;
    statusMessage.clear();
    statusIsError = false;
    bundleAsset.reset();
    previewMaterial.reset();
    previewModel.reset();
    bundleAssetSavePending = false;
    skyboxAssetSavePending = false;
    materialAssetSavePending = false;
    previewMaterialDirty = true;
    previewModelDirty = true;
    mtlMaterials.clear();
    selectedMtlMaterialIndex = 0;
    std::memset(bundleAlias, 0, sizeof(bundleAlias));
    std::memset(bundleRootEntry, 0, sizeof(bundleRootEntry));
    std::memset(bundleAddEntryPath, 0, sizeof(bundleAddEntryPath));
    std::memset(bundleAddSourceRef, 0, sizeof(bundleAddSourceRef));
    std::memset(skyboxName, 0, sizeof(skyboxName));
    std::memset(skyboxRightFace, 0, sizeof(skyboxRightFace));
    std::memset(skyboxLeftFace, 0, sizeof(skyboxLeftFace));
    std::memset(skyboxTopFace, 0, sizeof(skyboxTopFace));
    std::memset(skyboxBottomFace, 0, sizeof(skyboxBottomFace));
    std::memset(skyboxFrontFace, 0, sizeof(skyboxFrontFace));
    std::memset(skyboxBackFace, 0, sizeof(skyboxBackFace));
    std::memset(modelAssetName, 0, sizeof(modelAssetName));
    std::memset(modelAssetSource, 0, sizeof(modelAssetSource));
    std::memset(modelAssetMaterialRef, 0, sizeof(modelAssetMaterialRef));
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
    isBundleAssetFile = AssetBundle::IsBundlePath(filePath);
    isShaderAssetFile = ShaderAssetIO::IsShaderAssetPath(filePath);
    isSkyboxAssetFile = SkyboxAssetIO::IsSkyboxAssetPath(filePath);
    isMaterialAssetFile = MaterialAssetIO::IsMaterialAssetPath(filePath);
    isMaterialObjectFile = MaterialAssetIO::IsMaterialObjectPath(filePath);
    isModelAssetFile = ModelAssetIO::IsModelAssetPath(filePath);
    isMtlFile = isMtlPath(filePath);
    isModelFile = isModelPath(filePath);

    std::error_code ec;
    const std::filesystem::path writePath = backingWritePath(filePath);
    if(std::filesystem::exists(writePath, ec)){
        lastWriteTime = std::filesystem::last_write_time(writePath, ec);
    }
    lastExternalWriteTimeValidationFrame = ImGui::GetFrameCount();

    if(isBundleAssetFile){
        bundleAsset = std::make_shared<AssetBundle>();
        std::string error;
        if(!bundleAsset || !bundleAsset->open(filePath, &error)){
            statusIsError = true;
            statusMessage = error.empty() ? "Failed to load asset bundle." : error;
            bundleAsset.reset();
            return;
        }

        copyBuffer(bundleAlias, sizeof(bundleAlias), bundleAsset->alias());
        copyBuffer(bundleRootEntry, sizeof(bundleRootEntry), bundleAsset->rootEntry());
        bundleAssetSavePending = false;
        return;
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

    if(isSkyboxAssetFile){
        std::string error;
        if(!SkyboxAssetIO::LoadFromAbsolutePath(filePath, skyboxData, &error)){
            statusIsError = true;
            statusMessage = error;
            return;
        }
        copyBuffer(skyboxName, sizeof(skyboxName), skyboxData.name);
        copyBuffer(skyboxRightFace, sizeof(skyboxRightFace), skyboxData.rightFaceRef);
        copyBuffer(skyboxLeftFace, sizeof(skyboxLeftFace), skyboxData.leftFaceRef);
        copyBuffer(skyboxTopFace, sizeof(skyboxTopFace), skyboxData.topFaceRef);
        copyBuffer(skyboxBottomFace, sizeof(skyboxBottomFace), skyboxData.bottomFaceRef);
        copyBuffer(skyboxFrontFace, sizeof(skyboxFrontFace), skyboxData.frontFaceRef);
        copyBuffer(skyboxBackFace, sizeof(skyboxBackFace), skyboxData.backFaceRef);
        skyboxAssetSavePending = false;
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
        materialAssetSavePending = false;
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

    if(isModelAssetFile){
        std::string error;
        if(!ModelAssetIO::LoadFromAbsolutePath(filePath, modelAssetData, &error)){
            statusIsError = true;
            statusMessage = error;
            return;
        }
        copyBuffer(modelAssetName, sizeof(modelAssetName), modelAssetData.name);
        copyBuffer(modelAssetSource, sizeof(modelAssetSource), modelAssetData.sourceModelRef);
        copyBuffer(modelAssetMaterialRef, sizeof(modelAssetMaterialRef), modelAssetData.defaultMaterialRef);
        previewModelDirty = true;
        return;
    }

    if(isMtlFile){
        std::string error;
        if(!MtlMaterialImporter::LoadFromAbsolutePath(filePath, mtlMaterials, &error)){
            statusIsError = true;
            statusMessage = error;
            mtlMaterials.clear();
            selectedMtlMaterialIndex = 0;
            return;
        }
        if(mtlMaterials.empty()){
            statusMessage = "No materials found in .mtl file.";
            selectedMtlMaterialIndex = 0;
        }else if(selectedMtlMaterialIndex >= static_cast<int>(mtlMaterials.size())){
            selectedMtlMaterialIndex = 0;
        }
        if(!error.empty()){
            statusIsError = false;
            statusMessage = error;
        }
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

    const int frameNow = ImGui::GetFrameCount();
    if(hasLoadedData &&
       (frameNow - lastExternalWriteTimeValidationFrame) >= kFilePreviewWriteTimeValidationIntervalFrames){
        lastExternalWriteTimeValidationFrame = frameNow;

        std::error_code ec;
        const std::filesystem::path writePath = backingWritePath(filePath);
        if(std::filesystem::exists(writePath, ec)){
            const auto onDiskWriteTime = std::filesystem::last_write_time(writePath, ec);
            if(!ec && onDiskWriteTime != lastWriteTime){
                hasLoadedData = false;
            }
        }
    }
    reloadFromDisk(false);

    ImGui::Text("File: %s", filePath.filename().string().c_str());
    ImGui::TextDisabled("%s", toAssetRef(filePath, assetRoot).c_str());
    ImGui::Separator();

    if(isBundleAssetFile){
        drawBundleAssetEditor();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isShaderAssetFile){
        drawShaderAssetEditor();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isSkyboxAssetFile){
        drawSkyboxAssetEditor();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isMaterialAssetFile){
        drawMaterialAssetEditor();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isMaterialObjectFile){
        drawMaterialObjectEditor();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isModelAssetFile){
        drawModelAssetEditor();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isMtlFile){
        drawMtlFilePreview();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isModelFile){
        drawModelFilePreview();
        drawErrorByteDumpIfNeeded();
        return;
    }
    drawGenericInfo();
    drawErrorByteDumpIfNeeded();
}

void FilePreviewWidget::drawBundleAssetEditor(){
    if(!bundleAsset){
        ImGui::TextDisabled("Bundle preview unavailable.");
        return;
    }

    const bool aliasChanged = ImGui::InputText("Bundle Alias", bundleAlias, sizeof(bundleAlias));
    if(aliasChanged){
        bundleAssetSavePending = true;
    }

    const bool commitNow = bundleAssetSavePending && !ImGui::IsAnyItemActive();
    if(commitNow){
        std::string error;
        if(bundleAsset->setAlias(StringUtils::Trim(bundleAlias), &error) &&
           bundleAsset->save(&error)){
            std::string mountError;
            AssetBundleRegistry::Instance.mountBundle(filePath, &mountError);
            if(!mountError.empty()){
                LogBot.Log(LOG_WARN, "Bundle saved but registry refresh failed: %s", mountError.c_str());
            }
            reloadFromDisk(true);
            bundleAssetSavePending = false;
            statusIsError = false;
            statusMessage = "Bundle alias updated.";
        }else{
            bundleAssetSavePending = false;
            statusIsError = true;
            statusMessage = error.empty() ? "Failed to save bundle." : error;
        }
    }

    ImGui::TextDisabled("Root Entry: ./");
    ImGui::Separator();
    ImGui::Text("Entries: %d", static_cast<int>(bundleAsset->getEntries().size()));
    if(bundleAsset->getEntries().empty()){
        ImGui::TextDisabled("Double-click the bundle in the workspace to browse and edit its contents.");
    }else if(ImGui::BeginTable("##BundleEntries", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f))){
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 2.8f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableHeadersRow();

        for(const auto& entry : bundleAsset->getEntries()){
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(entry.path.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(entry.kind.c_str());
            ImGui::TableSetColumnIndex(2);
            if(entry.sourceRef.empty()){
                ImGui::TextDisabled("-");
            }else{
                ImGui::TextUnformatted(entry.sourceRef.c_str());
            }
        }

        ImGui::EndTable();
    }

    if(bundleAssetSavePending){
        ImGui::TextDisabled("Apply the alias field to save.");
    }else if(statusMessage.empty()){
        ImGui::TextDisabled("Double-click this bundle in the workspace to open it like a folder.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }
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
            lastWriteTime = std::filesystem::last_write_time(backingWritePath(filePath), ec);
        }else{
            statusIsError = true;
            statusMessage = error;
            materialAssetSavePending = false;
        }
    }

    if(materialAssetSavePending){
        ImGui::TextDisabled("Release control to apply changes.");
    }else if(statusMessage.empty()){
        ImGui::TextDisabled("Edits save automatically.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }
}

void FilePreviewWidget::drawSkyboxAssetEditor(){
    bool changed = false;
    changed |= ImGui::InputText("Skybox Name", skyboxName, sizeof(skyboxName));

    auto drawFaceField = [&](const char* label, char* pathBuffer, size_t bufferSize){
        bool localChanged = false;
        localChanged |= EditorAssetUI::DrawAssetDropInput(label, pathBuffer, bufferSize, EditorAssetUI::AssetKind::Image);
        ImGui::PushID(label);
        drawTexturePreviewSmall(pathBuffer);
        ImGui::PopID();
        ImGui::Spacing();
        return localChanged;
    };

    changed |= drawFaceField("Right (+X)", skyboxRightFace, sizeof(skyboxRightFace));
    changed |= drawFaceField("Left (-X)", skyboxLeftFace, sizeof(skyboxLeftFace));
    changed |= drawFaceField("Top (+Y)", skyboxTopFace, sizeof(skyboxTopFace));
    changed |= drawFaceField("Bottom (-Y)", skyboxBottomFace, sizeof(skyboxBottomFace));
    changed |= drawFaceField("Front (+Z)", skyboxFrontFace, sizeof(skyboxFrontFace));
    changed |= drawFaceField("Back (-Z)", skyboxBackFace, sizeof(skyboxBackFace));

    if(changed){
        skyboxData.name = skyboxName;
        skyboxData.rightFaceRef = skyboxRightFace;
        skyboxData.leftFaceRef = skyboxLeftFace;
        skyboxData.topFaceRef = skyboxTopFace;
        skyboxData.bottomFaceRef = skyboxBottomFace;
        skyboxData.frontFaceRef = skyboxFrontFace;
        skyboxData.backFaceRef = skyboxBackFace;
        skyboxAssetSavePending = true;
    }

    const bool commitEditsNow = skyboxAssetSavePending && !ImGui::IsAnyItemActive();
    if(commitEditsNow){
        std::string error;
        if(SkyboxAssetIO::SaveToAbsolutePath(filePath, skyboxData, &error)){
            statusIsError = false;
            statusMessage = "Saved.";
            std::error_code ec;
            lastWriteTime = std::filesystem::last_write_time(backingWritePath(filePath), ec);
            skyboxAssetSavePending = false;
        }else{
            statusIsError = true;
            statusMessage = error;
        }
    }

    if(skyboxAssetSavePending){
        ImGui::TextDisabled("Release control to apply changes.");
    }else if(statusMessage.empty()){
        ImGui::TextDisabled("Edits save automatically.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }

    if(!SkyboxAssetIO::HasRequiredFaces(skyboxData)){
        ImGui::TextDisabled("Assign all six faces before this skybox can render.");
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
        materialAssetSavePending = true;
    }

    const bool commitEditsNow = materialAssetSavePending && !ImGui::IsAnyItemActive();
    if(commitEditsNow){
        std::string error;
        if(MaterialAssetIO::SaveToAbsolutePath(filePath, materialData, &error)){
            statusIsError = false;
            statusMessage = "Saved.";
            std::error_code ec;
            lastWriteTime = std::filesystem::last_write_time(backingWritePath(filePath), ec);
            materialAssetSavePending = false;
            previewMaterialDirty = true;
            EditorAssetUI::InvalidateMaterialThumbnail();
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
            lastWriteTime = std::filesystem::last_write_time(backingWritePath(filePath), ec);
            previewMaterialDirty = true;
            EditorAssetUI::InvalidateMaterialThumbnail();
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

void FilePreviewWidget::drawModelAssetEditor(){
    bool changed = false;
    changed |= ImGui::InputText("Model Name", modelAssetName, sizeof(modelAssetName));
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Source Model",
        modelAssetSource,
        sizeof(modelAssetSource),
        EditorAssetUI::AssetKind::Model
    );
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Default Material",
        modelAssetMaterialRef,
        sizeof(modelAssetMaterialRef),
        EditorAssetUI::AssetKind::Material
    );

    bool forceSmoothNormals = (modelAssetData.forceSmoothNormals != 0);
    if(ImGui::Checkbox("Force Smooth Normals", &forceSmoothNormals)){
        modelAssetData.forceSmoothNormals = forceSmoothNormals ? 1 : 0;
        changed = true;
    }

    if(ImGui::Button("Reload Source")){
        previewModelDirty = true;
    }

    if(changed){
        modelAssetData.name = modelAssetName;
        modelAssetData.sourceModelRef = modelAssetSource;
        modelAssetData.defaultMaterialRef = modelAssetMaterialRef;

        std::string error;
        if(ModelAssetIO::SaveToAbsolutePath(filePath, modelAssetData, &error)){
            statusIsError = false;
            statusMessage = "Saved.";
            std::error_code ec;
            lastWriteTime = std::filesystem::last_write_time(backingWritePath(filePath), ec);
            previewModelDirty = true;
            EditorAssetUI::InvalidateAllThumbnails();
        }else{
            statusIsError = true;
            statusMessage = error;
        }
    }

    bool needsRender = false;
    if(previewModelDirty){
        previewModel.reset();
        std::string error;
        previewModel = ModelAssetIO::InstantiateModel(modelAssetData, &error);
        if(!previewModel){
            if(!modelAssetData.sourceModelRef.empty()){
                statusIsError = true;
                statusMessage = error.empty() ? "Failed to load model from descriptor." : error;
            }
        }else{
            needsRender = true;
        }
        previewModelDirty = false;
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
        ImGui::TextDisabled("Descriptor edits save automatically.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }
}

bool FilePreviewWidget::importSelectedMtlMaterial(){
    if(!isMtlFile || mtlMaterials.empty()){
        statusIsError = true;
        statusMessage = "No .mtl material selected for import.";
        return false;
    }
    if(selectedMtlMaterialIndex < 0 || selectedMtlMaterialIndex >= static_cast<int>(mtlMaterials.size())){
        statusIsError = true;
        statusMessage = "Invalid .mtl material selection.";
        return false;
    }

    const MtlMaterialDefinition& selectedDef = mtlMaterials[(size_t)selectedMtlMaterialIndex];
    std::string baseName = MtlMaterialImporter::SanitizeMaterialName(selectedDef.name);
    if(baseName.empty()){
        baseName = "ImportedMaterial";
    }

    const std::filesystem::path parentDir = filePath.parent_path();
    std::filesystem::path materialPath;
    std::filesystem::path materialAssetPath;
    int suffixIndex = 0;
    for(;;){
        const std::string suffix = (suffixIndex <= 0) ? "" : ("_" + std::to_string(suffixIndex));
        const std::string candidateBaseName = baseName + suffix;
        materialPath = parentDir / (candidateBaseName + ".material");
        materialAssetPath = parentDir / (candidateBaseName + ".mat.asset");
        if(!pathExists(materialPath) && !pathExists(materialAssetPath)){
            break;
        }
        suffixIndex++;
    }

    MaterialAssetData data;
    std::string error;
    if(!MtlMaterialImporter::BuildMaterialAssetData(selectedDef, data, &error)){
        statusIsError = true;
        statusMessage = error.empty() ? "Failed to convert .mtl material." : error;
        return false;
    }

    data.name = materialAssetPath.filename().string();
    data.linkParentRef = toAssetRef(materialPath, assetRoot);
    if(!MaterialAssetIO::SaveToAbsolutePath(materialAssetPath, data, &error)){
        statusIsError = true;
        statusMessage = error;
        return false;
    }

    std::string materialAssetRef;
    if(!MaterialAssetIO::ResolveMaterialAssetRef(materialAssetPath.generic_string(), materialAssetRef, &error)){
        statusIsError = true;
        statusMessage = error;
        return false;
    }

    MaterialObjectData objectData;
    objectData.name = materialPath.stem().string();
    objectData.materialAssetRef = materialAssetRef;
    if(!MaterialAssetIO::SaveMaterialObjectToAbsolutePath(materialPath, objectData, &error)){
        statusIsError = true;
        statusMessage = error;
        return false;
    }

    EditorAssetUI::InvalidateAllThumbnails();
    statusIsError = false;
    statusMessage = "Imported material: " + materialPath.filename().string();
    return true;
}

bool FilePreviewWidget::importCurrentModelAsAsset(){
    if(!isModelFile){
        statusIsError = true;
        statusMessage = "No source model selected for import.";
        return false;
    }

    const std::string sourceAssetRef = toAssetRef(filePath, assetRoot);
    if(sourceAssetRef.empty()){
        statusIsError = true;
        statusMessage = "Failed to resolve source model asset ref.";
        return false;
    }

    std::string baseName = StringUtils::Trim(filePath.stem().string());
    if(baseName.empty()){
        baseName = "ImportedModel";
    }

    const std::filesystem::path parentDir = filePath.parent_path();
    std::filesystem::path modelAssetPath;
    int suffixIndex = 0;
    for(;;){
        const std::string suffix = (suffixIndex <= 0) ? "" : ("_" + std::to_string(suffixIndex));
        const std::string candidateBaseName = baseName + suffix;
        modelAssetPath = parentDir / (candidateBaseName + ".model.asset");
        if(!pathExists(modelAssetPath)){
            break;
        }
        suffixIndex++;
    }

    ModelAssetData data;
    data.name = modelAssetPath.filename().string();
    data.linkParentRef = sourceAssetRef;
    data.sourceModelRef = sourceAssetRef;
    data.defaultMaterialRef.clear();
    data.forceSmoothNormals = (StringUtils::ToLowerCase(filePath.extension().string()) == ".obj") ? 1 : 0;

    std::string error;
    if(!ModelAssetIO::SaveToAbsolutePath(modelAssetPath, data, &error)){
        statusIsError = true;
        statusMessage = error.empty() ? "Failed to save model asset." : error;
        return false;
    }

    EditorAssetUI::InvalidateAllThumbnails();
    statusIsError = false;
    statusMessage = "Imported model asset: " + modelAssetPath.filename().string();
    return true;
}

void FilePreviewWidget::drawMtlFilePreview(){
    if(mtlMaterials.empty()){
        if(statusMessage.empty()){
            ImGui::TextDisabled("No materials found in .mtl file.");
        }else if(statusIsError){
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
        }else{
            ImGui::TextDisabled("%s", statusMessage.c_str());
        }
        return;
    }

    if(selectedMtlMaterialIndex < 0 || selectedMtlMaterialIndex >= static_cast<int>(mtlMaterials.size())){
        selectedMtlMaterialIndex = 0;
        previewMaterialDirty = true;
    }

    const char* comboPreview = mtlMaterials[(size_t)selectedMtlMaterialIndex].name.c_str();
    if(ImGui::BeginCombo("MTL Material", comboPreview)){
        for(int i = 0; i < static_cast<int>(mtlMaterials.size()); ++i){
            const bool selected = (i == selectedMtlMaterialIndex);
            const std::string& itemName = mtlMaterials[(size_t)i].name;
            const char* displayName = itemName.empty() ? "<unnamed>" : itemName.c_str();
            if(ImGui::Selectable(displayName, selected)){
                selectedMtlMaterialIndex = i;
                previewMaterialDirty = true;
            }
            if(selected){
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if(ImGui::Button("Import Material")){
        importSelectedMtlMaterial();
    }
    ImGui::SameLine();
    if(ImGui::Button("Reload .mtl")){
        hasLoadedData = false;
        reloadFromDisk(true);
        previewMaterialDirty = true;
    }

    bool materialNeedsRender = false;
    ensureMaterialPreviewResources(previewSize);
    if(previewMaterialDirty || !previewMaterial){
        std::string error;
        auto constructed = MtlMaterialImporter::BuildConstructedMaterial(
            mtlMaterials[(size_t)selectedMtlMaterialIndex],
            toAssetRef(filePath, assetRoot),
            &error
        );
        previewMaterial = std::static_pointer_cast<Material>(constructed);
        if(!previewMaterial){
            statusIsError = true;
            statusMessage = error.empty() ? "Failed to build constructed preview material." : error;
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
        ImGui::TextDisabled("Preview generated from .mtl. Use Import Material to save reusable assets.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }
}

void FilePreviewWidget::drawModelFilePreview(){
    const std::string ext = StringUtils::ToLowerCase(filePath.extension().string());
    bool needsRender = false;

    if(ImGui::Button("Import Model")){
        importCurrentModelAsAsset();
    }
    ImGui::SameLine();
    if(ImGui::Button("Reload Model")){
        previewModelDirty = true;
    }

    if(previewModelDirty){
        previewModel.reset();
        statusMessage.clear();
        statusIsError = false;

        if(ext == ".obj"){
            ModelAssetData previewData;
            previewData.sourceModelRef = toAssetRef(filePath, assetRoot);
            previewData.forceSmoothNormals = 1;
            std::string previewError;
            previewModel = ModelAssetIO::InstantiateModel(previewData, &previewError);
            if(!previewModel){
                statusIsError = true;
                statusMessage = previewError.empty() ? "Failed to load OBJ preview." : previewError;
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
    bool isDirectory = false;
    if(!pathExists(filePath, &isDirectory)){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "File does not exist.");
        return;
    }
    if(isDirectory){
        ImGui::TextUnformatted("Directory selected.");
        return;
    }

    std::filesystem::path bundlePath;
    std::string entryPath;
    if(AssetBundleRegistry::DecodeVirtualEntryPath(filePath, bundlePath, entryPath)){
        std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
        if(bundle && !entryPath.empty()){
            for(const auto& entry : bundle->getEntries()){
                if(entry.path == entryPath){
                    ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(entry.size));
                    break;
                }
            }
        }
    }else{
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(filePath, ec);
        if(!ec){
            ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(fileSize));
        }
    }
    ImGui::TextDisabled("No preview widget for this file type.");
}

void FilePreviewWidget::refreshErrorByteDump(){
    if(!statusIsError || filePath.empty()){
        errorByteDump.clear();
        errorByteDumpPath.clear();
        return;
    }

    std::error_code ec;
    bool isDirectory = false;
    if(!pathExists(filePath, &isDirectory) || isDirectory){
        errorByteDump = "File does not exist or is a directory.";
        errorByteDumpPath = filePath;
        return;
    }

    std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(backingWritePath(filePath), ec);
    if(errorByteDumpPath == filePath && !ec && writeTime == errorByteDumpWriteTime && !errorByteDump.empty()){
        return;
    }

    errorByteDumpPath = filePath;
    if(!ec){
        errorByteDumpWriteTime = writeTime;
    }
    errorByteDump = buildRawByteDump(filePath);
}

void FilePreviewWidget::drawErrorByteDumpIfNeeded(){
    if(!statusIsError){
        return;
    }

    refreshErrorByteDump();
    if(errorByteDump.empty()){
        return;
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Raw Byte Data");
    ImGui::BeginDisabled();
    ImGui::InputTextMultiline(
        "##RawByteDump",
        const_cast<char*>(errorByteDump.c_str()),
        errorByteDump.size() + 1,
        ImVec2(-1.0f, 180.0f),
        ImGuiInputTextFlags_ReadOnly
    );
    ImGui::EndDisabled();
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
    previewFrameBuffer->clear(Color::CLEAR);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    glDisable(GL_BLEND);

    Screen::MakeCameraCurrent(previewCamera);
    Screen::MakeEnvironmentCurrent(previewEnvironment);

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
