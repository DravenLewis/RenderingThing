/**
 * @file src/Editor/Widgets/FilePreviewWidget.cpp
 * @brief Implementation for FilePreviewWidget.
 */

#include "Editor/Widgets/FilePreviewWidget.h"

#include "Editor/Core/EditorAssetUI.h"
#include "Editor/Core/EditorArrayUI.h"
#include "Editor/Core/EditorPropertyUI.h"
#include "Assets/Core/Asset.h"
#include "Assets/Core/AssetDescriptorUtils.h"
#include "Assets/Bundles/AssetBundleRegistry.h"
#include "Assets/Descriptors/EffectAsset.h"
#include "Assets/Descriptors/EnvironmentAsset.h"
#include "Assets/Descriptors/ImageAsset.h"
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

#include <algorithm>
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
        std::uint64_t sourceRevision = 0;
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

    void invalidatePreviewCaches(){
        g_texturePreviewCache.clear();
        g_texturePreviewCacheLastPruneFrame = -100000;
        EditorAssetUI::InvalidateAllThumbnails();
    }

    void notifyEditedAsset(const std::filesystem::path& path, const std::filesystem::path& assetRoot){
        if(path.empty()){
            return;
        }

        invalidatePreviewCaches();
        const std::string assetRef = toAssetRef(path, assetRoot);
        if(!assetRef.empty()){
            AssetManager::Instance.notifyAssetChanged(assetRef);
        }else{
            AssetManager::Instance.notifyAssetChanged(path.generic_string());
        }
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
        const std::uint64_t currentRevision = ImageAssetIO::GetTextureRefRevision(assetRef);

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
                cached.lastValidationFrame = frameNow;
                if(cached.sourceRevision == currentRevision){
                    return cached.texture;
                }
            }
        }

        auto tex = ImageAssetIO::InstantiateTextureFromRef(assetRef);
        if(!tex){
            return nullptr;
        }
        tex->setFilterMode(TextureFilterMode::NEAREST);

        TexturePreviewCacheEntry entry;
        entry.texture = tex;
        entry.sourceRevision = currentRevision;
        entry.lastValidationFrame = frameNow;
        entry.lastUsedFrame = frameNow;

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

    bool isImagePath(const std::filesystem::path& path){
        return ImageAssetIO::IsRawImagePath(path);
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
    environmentAssetSavePending = false;
    lensFlareAssetSavePending = false;
    effectAssetSavePending = false;
    materialAssetSavePending = false;
    previewMaterialDirty = true;
    previewModelDirty = true;
    mtlMaterials.clear();
    selectedMtlMaterialIndex = 0;
    std::memset(environmentName, 0, sizeof(environmentName));
    std::memset(environmentSkybox, 0, sizeof(environmentSkybox));
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
    std::memset(lensFlareName, 0, sizeof(lensFlareName));
    std::memset(lensFlareTexture, 0, sizeof(lensFlareTexture));
    std::memset(effectAssetName, 0, sizeof(effectAssetName));
    std::memset(effectAssetVertex, 0, sizeof(effectAssetVertex));
    std::memset(effectAssetFragment, 0, sizeof(effectAssetFragment));
    std::memset(modelAssetName, 0, sizeof(modelAssetName));
    std::memset(modelAssetSource, 0, sizeof(modelAssetSource));
    std::memset(modelAssetMaterialRef, 0, sizeof(modelAssetMaterialRef));
    std::memset(imageAssetName, 0, sizeof(imageAssetName));
    std::memset(imageAssetSource, 0, sizeof(imageAssetSource));
    std::memset(imageImportAssetPath, 0, sizeof(imageImportAssetPath));
    imageAssetData = ImageAssetData{};
    environmentData = EnvironmentAssetData{};
    imageImportData = ImageAssetData{};
    imageAssetSavePending = false;
    imageImportPopupOpen = false;
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
    isEnvironmentAssetFile = EnvironmentAssetIO::IsEnvironmentAssetPath(filePath);
    isSkyboxAssetFile = SkyboxAssetIO::IsSkyboxAssetPath(filePath);
    isLensFlareAssetFile = LensFlareAssetIO::IsLensFlareAssetPath(filePath);
    isEffectAssetFile = EffectAssetIO::IsEffectAssetPath(filePath);
    isMaterialAssetFile = MaterialAssetIO::IsMaterialAssetPath(filePath);
    isMaterialObjectFile = MaterialAssetIO::IsMaterialObjectPath(filePath);
    isModelAssetFile = ModelAssetIO::IsModelAssetPath(filePath);
    isImageAssetFile = ImageAssetIO::IsImageAssetPath(filePath);
    isMtlFile = isMtlPath(filePath);
    isModelFile = isModelPath(filePath);
    isImageFile = isImagePath(filePath) || isImageAssetFile;

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

    if(isEnvironmentAssetFile){
        std::string error;
        if(!EnvironmentAssetIO::LoadFromAbsolutePath(filePath, environmentData, &error)){
            statusIsError = true;
            statusMessage = error;
            return;
        }
        copyBuffer(environmentName, sizeof(environmentName), environmentData.name);
        copyBuffer(environmentSkybox, sizeof(environmentSkybox), environmentData.skyboxAssetRef);
        sanitizeEnvironmentSettings(environmentData.settings);
        environmentAssetSavePending = false;
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

    if(isLensFlareAssetFile){
        std::string error;
        if(!LensFlareAssetIO::LoadFromAbsolutePath(filePath, lensFlareData, &error)){
            statusIsError = true;
            statusMessage = error;
            return;
        }
        copyBuffer(lensFlareName, sizeof(lensFlareName), lensFlareData.name);
        copyBuffer(lensFlareTexture, sizeof(lensFlareTexture), lensFlareData.textureRef);
        lensFlareAssetSavePending = false;
        return;
    }

    if(isEffectAssetFile){
        std::string error;
        if(!EffectAssetIO::LoadFromAbsolutePath(filePath, effectAssetData, &error)){
            statusIsError = true;
            statusMessage = error;
            return;
        }
        copyBuffer(effectAssetName, sizeof(effectAssetName), effectAssetData.name);
        copyBuffer(effectAssetVertex, sizeof(effectAssetVertex), effectAssetData.vertexAssetRef);
        copyBuffer(effectAssetFragment, sizeof(effectAssetFragment), effectAssetData.fragmentAssetRef);
        effectAssetSavePending = false;
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

    if(isImageAssetFile){
        std::string error;
        if(!ImageAssetIO::LoadFromAbsolutePath(filePath, imageAssetData, &error)){
            statusIsError = true;
            statusMessage = error;
            return;
        }
        copyBuffer(imageAssetName, sizeof(imageAssetName), imageAssetData.name);
        copyBuffer(imageAssetSource, sizeof(imageAssetSource), imageAssetData.sourceImageRef);
        imageAssetSavePending = false;
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
                notifyEditedAsset(filePath, assetRoot);
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
    if(isEnvironmentAssetFile){
        drawEnvironmentAssetEditor();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isSkyboxAssetFile){
        drawSkyboxAssetEditor();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isLensFlareAssetFile){
        drawLensFlareAssetEditor();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isEffectAssetFile){
        drawEffectAssetEditor();
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
    if(isImageAssetFile){
        drawImageAssetEditor();
        drawErrorByteDumpIfNeeded();
        return;
    }
    if(isImageFile){
        drawImageFilePreview();
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

    const bool aliasChanged = EditorPropertyUI::InputText("Bundle Alias", bundleAlias, sizeof(bundleAlias));
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
    changed |= EditorPropertyUI::InputText("Cache Name", cacheName, sizeof(cacheName));
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
            notifyEditedAsset(filePath, assetRoot);
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

void FilePreviewWidget::drawEnvironmentAssetEditor(){
    bool changed = false;
    changed |= EditorPropertyUI::InputText("Environment Name", environmentName, sizeof(environmentName));
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Skybox Asset",
        environmentSkybox,
        sizeof(environmentSkybox),
        EditorAssetUI::AssetKind::SkyboxAsset
    );

    changed |= EditorPropertyUI::ColorEdit3("Ambient Color", &environmentData.settings.ambientColor.x);
    changed |= EditorPropertyUI::SliderFloat("Ambient Intensity", &environmentData.settings.ambientIntensity, 0.0f, 8.0f, "%.2f");

    ImGui::Separator();
    changed |= EditorPropertyUI::Checkbox("Fog Enabled", &environmentData.settings.fogEnabled);
    if(environmentData.settings.fogEnabled){
        changed |= EditorPropertyUI::ColorEdit3("Fog Color", &environmentData.settings.fogColor.x);
        changed |= EditorPropertyUI::DragFloat("Fog Start", &environmentData.settings.fogStart, 0.1f, 0.0f, 20000.0f, "%.2f");
        changed |= EditorPropertyUI::DragFloat("Fog Stop", &environmentData.settings.fogStop, 0.1f, 0.0f, 20000.0f, "%.2f");
        changed |= EditorPropertyUI::DragFloat("Fog End", &environmentData.settings.fogEnd, 0.1f, 0.0f, 20000.0f, "%.2f");
    }

    ImGui::Separator();
    changed |= EditorPropertyUI::Checkbox("Use Procedural Sky", &environmentData.settings.useProceduralSky);
    if(environmentData.settings.useProceduralSky){
        float sunDirection[3] = {
            environmentData.settings.sunDirection.x,
            environmentData.settings.sunDirection.y,
            environmentData.settings.sunDirection.z
        };
        if(EditorPropertyUI::DragFloat3("Sun Direction", sunDirection, 0.01f, -1.0f, 1.0f, "%.3f")){
            environmentData.settings.sunDirection = Math3D::Vec3(sunDirection[0], sunDirection[1], sunDirection[2]);
            changed = true;
        }
        changed |= EditorPropertyUI::SliderFloat("Rayleigh Strength", &environmentData.settings.rayleighStrength, 0.0f, 8.0f, "%.3f");
        changed |= EditorPropertyUI::SliderFloat("Mie Strength", &environmentData.settings.mieStrength, 0.0f, 8.0f, "%.3f");
        changed |= EditorPropertyUI::SliderFloat("Mie Anisotropy", &environmentData.settings.mieAnisotropy, 0.0f, 0.99f, "%.3f");
    }

    if(changed){
        environmentData.name = environmentName;
        environmentData.skyboxAssetRef = environmentSkybox;
        sanitizeEnvironmentSettings(environmentData.settings);
        environmentAssetSavePending = true;
    }

    const bool commitEditsNow = environmentAssetSavePending && !ImGui::IsAnyItemActive();
    if(commitEditsNow){
        std::string error;
        if(EnvironmentAssetIO::SaveToAbsolutePath(filePath, environmentData, &error)){
            statusIsError = false;
            statusMessage = "Saved.";
            std::error_code ec;
            lastWriteTime = std::filesystem::last_write_time(backingWritePath(filePath), ec);
            environmentAssetSavePending = false;
            notifyEditedAsset(filePath, assetRoot);
        }else{
            statusIsError = true;
            statusMessage = error;
        }
    }

    if(environmentAssetSavePending){
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
    changed |= EditorPropertyUI::InputText("Skybox Name", skyboxName, sizeof(skyboxName));

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
            notifyEditedAsset(filePath, assetRoot);
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

void FilePreviewWidget::drawLensFlareAssetEditor(){
    static const char* kElementTypeNames[] = {
        "Image",
        "Polygon",
        "Circle"
    };
    static constexpr int kMaxLensFlareElements = 64;

    bool changed = false;
    changed |= EditorPropertyUI::InputText("Flare Name", lensFlareName, sizeof(lensFlareName));

    changed |= EditorPropertyUI::ColorEdit3("Tint", &lensFlareData.tint.x);
    changed |= EditorPropertyUI::DragFloat("Intensity", &lensFlareData.intensity, 0.01f, 0.0f, 8.0f);
    changed |= EditorPropertyUI::DragFloat("Sprite Scale", &lensFlareData.spriteScale, 0.25f, 8.0f, 512.0f);
    changed |= EditorPropertyUI::DragFloat("Ghost Intensity", &lensFlareData.ghostIntensity, 0.01f, 0.0f, 4.0f);
    changed |= EditorPropertyUI::DragFloat("Ghost Spacing", &lensFlareData.ghostSpacing, 0.01f, 0.0f, 1.2f);
    changed |= EditorPropertyUI::DragFloat("Halo Intensity", &lensFlareData.haloIntensity, 0.01f, 0.0f, 4.0f);
    changed |= EditorPropertyUI::DragFloat("Halo Scale", &lensFlareData.haloScale, 0.01f, 0.1f, 6.0f);
    changed |= EditorPropertyUI::DragFloat("Glare Threshold", &lensFlareData.glareThreshold, 0.01f, 0.0f, 16.0f);
    changed |= EditorPropertyUI::DragFloat("Glare Intensity", &lensFlareData.glareIntensity, 0.005f, 0.0f, 4.0f);
    changed |= EditorPropertyUI::DragFloat("Glare Length Px", &lensFlareData.glareLengthPx, 0.5f, 0.0f, 512.0f);
    changed |= EditorPropertyUI::DragFloat("Glare Falloff", &lensFlareData.glareFalloff, 0.01f, 0.05f, 8.0f);
    ImGui::Spacing();

    if(ImGui::TreeNodeEx("Elements", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::TextDisabled("Size sets the active element count.");
        ImGui::TextDisabled("Axis Position: 1 = source, 0 = screen center, negative = opposite side.");
        changed |= EditorArrayUI::DrawArray("LensFlareElements", lensFlareData.elements, kMaxLensFlareElements, "Lens Flare Element",
            [&](LensFlareElementData& element, size_t index) -> bool {
                (void)index;
                bool elementChanged = false;

                int typeIndex = static_cast<int>(element.type);
                typeIndex = std::clamp(typeIndex, 0, static_cast<int>(IM_ARRAYSIZE(kElementTypeNames)) - 1);
                if(EditorPropertyUI::Combo("Type", &typeIndex, kElementTypeNames, IM_ARRAYSIZE(kElementTypeNames))){
                    element.type = static_cast<LensFlareElementType>(typeIndex);
                    if(element.type == LensFlareElementType::Circle){
                        element.polygonSides = 64;
                    }
                    elementChanged = true;
                }

                if(element.type == LensFlareElementType::Image){
                    char elementTexture[256] = {};
                    copyBuffer(elementTexture, sizeof(elementTexture), element.textureRef);
                    if(EditorAssetUI::DrawAssetDropInput("Image", elementTexture, sizeof(elementTexture), EditorAssetUI::AssetKind::Image)){
                        element.textureRef = elementTexture;
                        elementChanged = true;
                    }
                    drawTexturePreviewSmall(elementTexture);
                }else if(element.type == LensFlareElementType::Polygon){
                    int polygonSides = std::clamp(element.polygonSides, 3, 64);
                    if(EditorPropertyUI::InputInt("Count", &polygonSides)){
                        element.polygonSides = std::clamp(polygonSides, 3, 64);
                        elementChanged = true;
                    }
                }else{
                    element.polygonSides = 64;
                    ImGui::TextDisabled("Circle uses 64 polygon sides.");
                }

                elementChanged |= EditorPropertyUI::ColorEdit3("Tint", &element.tint.x);
                elementChanged |= EditorPropertyUI::DragFloat("Intensity", &element.intensity, 0.01f, 0.0f, 8.0f);
                elementChanged |= EditorPropertyUI::DragFloat("Size Scale", &element.sizeScale, 0.01f, 0.05f, 8.0f);
                elementChanged |= EditorPropertyUI::DragFloat("Axis Position", &element.axisPosition, 0.01f, -3.0f, 3.0f);
                return elementChanged;
            }
        );
        ImGui::TreePop();
    }

    if(changed){
        lensFlareData.name = lensFlareName;
        lensFlareAssetSavePending = true;
    }

    const bool commitEditsNow = lensFlareAssetSavePending && !ImGui::IsAnyItemActive();
    if(commitEditsNow){
        std::string error;
        if(LensFlareAssetIO::SaveToAbsolutePath(filePath, lensFlareData, &error)){
            statusIsError = false;
            statusMessage = "Saved.";
            std::error_code ec;
            lastWriteTime = std::filesystem::last_write_time(backingWritePath(filePath), ec);
            lensFlareAssetSavePending = false;
            notifyEditedAsset(filePath, assetRoot);
        }else{
            statusIsError = true;
            statusMessage = error;
        }
    }

    if(lensFlareAssetSavePending){
        ImGui::TextDisabled("Release control to apply changes.");
    }else if(statusMessage.empty()){
        ImGui::TextDisabled("Edits save automatically.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }

    if(lensFlareData.elements.empty()){
        ImGui::TextDisabled("Add one or more flare elements to render anything.");
    }else{
        ImGui::TextDisabled("Image elements render only when their Image field is set.");
    }
}

void FilePreviewWidget::drawEffectAssetEditor(){
    static constexpr int kMaxRequiredEffects = 16;

    bool changed = false;
    changed |= EditorPropertyUI::InputText("Effect Name", effectAssetName, sizeof(effectAssetName));

    std::string vertexAssetRef = effectAssetData.vertexAssetRef;
    if(EditorAssetUI::DrawAssetDropInput(
            "Vertex Shader",
            vertexAssetRef,
            {EditorAssetUI::AssetKind::ShaderVertex, EditorAssetUI::AssetKind::ShaderGeneric})){
        effectAssetData.vertexAssetRef = vertexAssetRef;
        copyBuffer(effectAssetVertex, sizeof(effectAssetVertex), effectAssetData.vertexAssetRef);
        changed = true;
    }

    std::string fragmentAssetRef = effectAssetData.fragmentAssetRef;
    if(EditorAssetUI::DrawAssetDropInput(
            "Fragment Shader",
            fragmentAssetRef,
            {EditorAssetUI::AssetKind::ShaderFragment, EditorAssetUI::AssetKind::ShaderGeneric})){
        effectAssetData.fragmentAssetRef = fragmentAssetRef;
        copyBuffer(effectAssetFragment, sizeof(effectAssetFragment), effectAssetData.fragmentAssetRef);
        changed = true;
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Properties");
    ImGui::Separator();
    if(effectAssetData.properties.empty()){
        ImGui::TextDisabled("No exposed properties.");
    }else{
        ImGui::TextDisabled("Exposed parameters are built from the effect asset metadata.");
        for(size_t i = 0; i < effectAssetData.properties.size(); ++i){
            EffectPropertyData& property = effectAssetData.properties[i];
            std::string displayName = property.displayName;
            if(displayName.empty()){
                const std::string sourceName = !property.key.empty() ? property.key : property.uniformName;
                displayName = SanitizeEffectDisplayName(sourceName);
            }
            if(displayName.empty()){
                displayName = "Property";
            }

            const std::string labelSource = StringUtils::ToLowerCase(
                displayName + " " + property.key + " " + property.uniformName
            );

            ImGui::PushID(static_cast<int>(i));
            switch(property.type){
                case EffectPropertyType::Float:
                    changed |= EditorPropertyUI::DragFloat(displayName.c_str(), &property.floatValue, 0.01f);
                    break;
                case EffectPropertyType::Int:
                    changed |= EditorPropertyUI::InputInt(displayName.c_str(), &property.intValue);
                    break;
                case EffectPropertyType::Bool:
                    changed |= EditorPropertyUI::Checkbox(displayName.c_str(), &property.boolValue);
                    break;
                case EffectPropertyType::Vec2:
                    changed |= EditorPropertyUI::DragFloat2(displayName.c_str(), &property.vec2Value.x, 0.01f);
                    break;
                case EffectPropertyType::Vec3:
                    if(labelSource.find("color") != std::string::npos || labelSource.find("tint") != std::string::npos){
                        changed |= EditorPropertyUI::ColorEdit3(displayName.c_str(), &property.vec3Value.x);
                    }else{
                        changed |= EditorPropertyUI::DragFloat3(displayName.c_str(), &property.vec3Value.x, 0.01f);
                    }
                    break;
                case EffectPropertyType::Vec4:
                    if(labelSource.find("color") != std::string::npos || labelSource.find("tint") != std::string::npos){
                        changed |= EditorPropertyUI::ColorEdit4(displayName.c_str(), &property.vec4Value.x);
                    }else{
                        changed |= EditorPropertyUI::DragFloat4(displayName.c_str(), &property.vec4Value.x, 0.01f);
                    }
                    break;
                case EffectPropertyType::Texture2D:
                    changed |= EditorAssetUI::DrawAssetDropInput(
                        displayName.c_str(),
                        property.textureAssetRef,
                        {EditorAssetUI::AssetKind::Image}
                    );
                    drawTexturePreviewSmall(property.textureAssetRef);
                    break;
                default:
                    break;
            }
            ImGui::PopID();
        }
    }

    if(ImGui::TreeNodeEx("Dependencies", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::TextDisabled("Use builtin/... for built-ins or select another .effect.asset.");
        changed |= EditorArrayUI::DrawArray("RequiredEffects",
                                            effectAssetData.requiredEffects,
                                            kMaxRequiredEffects,
                                            "Required Effect",
            [&](std::string& effectRef, size_t index) -> bool {
                (void)index;
                return EditorAssetUI::DrawAssetDropInput(
                    "Effect Ref",
                    effectRef,
                    {EditorAssetUI::AssetKind::EffectAsset}
                );
            }
        );
        ImGui::TreePop();
    }

    if(changed){
        effectAssetData.name = effectAssetName;
        effectAssetSavePending = true;
    }

    const bool commitEditsNow = effectAssetSavePending && !ImGui::IsAnyItemActive();
    if(commitEditsNow){
        effectAssetData.name = effectAssetName;
        std::string error;
        if(EffectAssetIO::SaveToAbsolutePath(filePath, effectAssetData, &error)){
            statusIsError = false;
            statusMessage = "Saved.";
            std::error_code ec;
            lastWriteTime = std::filesystem::last_write_time(backingWritePath(filePath), ec);
            effectAssetSavePending = false;
            notifyEditedAsset(filePath, assetRoot);
        }else{
            statusIsError = true;
            statusMessage = error;
        }
    }

    if(effectAssetSavePending){
        ImGui::TextDisabled("Release control to apply changes.");
    }else if(statusMessage.empty()){
        ImGui::TextDisabled("Edits save automatically.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }

    if(!effectAssetData.isComplete()){
        ImGui::TextDisabled("Assign both a vertex and fragment shader before this effect can compile.");
    }
}

void FilePreviewWidget::drawMaterialAssetEditor(){
    static constexpr std::array<const char*, 7> kTypeNames = {
        "PBR", "Color", "Image", "LitColor", "LitImage", "FlatColor", "FlatImage"
    };

    bool changed = false;
    changed |= EditorPropertyUI::InputText("Material Name", materialName, sizeof(materialName));

    int typeIndex = static_cast<int>(materialData.type);
    if(typeIndex < 0 || typeIndex >= static_cast<int>(kTypeNames.size())){
        typeIndex = 0;
    }
    if(EditorPropertyUI::Combo("Material Type", &typeIndex, kTypeNames.data(), static_cast<int>(kTypeNames.size()))){
        materialData.type = static_cast<MaterialAssetType>(typeIndex);
        changed = true;
    }

    changed |= EditorAssetUI::DrawAssetDropInput("Shader Asset", materialShaderAsset, sizeof(materialShaderAsset), EditorAssetUI::AssetKind::ShaderAsset);

    bool castsShadows = materialData.castsShadows;
    if(EditorPropertyUI::Checkbox("Casts Shadows", &castsShadows)){
        materialData.castsShadows = castsShadows;
        changed = true;
    }

    bool receivesShadows = materialData.receivesShadows;
    if(EditorPropertyUI::Checkbox("Receives Shadows", &receivesShadows)){
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
            if(EditorPropertyUI::ColorEdit4("Base Color", &color.x)){
                materialData.color = color;
                changed = true;
            }
            changed |= EditorPropertyUI::SliderFloat("Metallic", &materialData.metallic, 0.0f, 1.0f);
            changed |= EditorPropertyUI::SliderFloat("Roughness", &materialData.roughness, 0.0f, 1.0f);
            changed |= EditorPropertyUI::DragFloat("Normal Scale", &materialData.normalScale, 0.01f, 0.0f, 8.0f);
            changed |= EditorPropertyUI::DragFloat("Height Scale", &materialData.heightScale, 0.001f, 0.0f, 1.0f);
            changed |= EditorPropertyUI::DragFloat("Emissive Strength", &materialData.emissiveStrength, 0.01f, 0.0f, 32.0f);
            changed |= EditorPropertyUI::SliderFloat("AO Strength", &materialData.occlusionStrength, 0.0f, 4.0f);
            changed |= EditorPropertyUI::DragFloat("Env Strength", &materialData.envStrength, 0.01f, 0.0f, 8.0f);

            bool useEnvMap = (materialData.useEnvMap != 0);
            if(EditorPropertyUI::Checkbox("Use Env Map", &useEnvMap)){
                materialData.useEnvMap = useEnvMap ? 1 : 0;
                changed = true;
            }

            bool useAlphaClip = (materialData.useAlphaClip != 0);
            if(EditorPropertyUI::Checkbox("Use Alpha Clip", &useAlphaClip)){
                materialData.useAlphaClip = useAlphaClip ? 1 : 0;
                changed = true;
            }
            if(materialData.useAlphaClip != 0){
                changed |= EditorPropertyUI::SliderFloat("Alpha Cutoff", &materialData.alphaCutoff, 0.0f, 1.0f);
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
            if(EditorPropertyUI::ColorEdit4("Color", &color.x)){
                materialData.color = color;
                changed = true;
            }
            changed |= drawTextureField("Texture", materialTexture, sizeof(materialTexture));
            if(materialData.type == MaterialAssetType::Image){
                changed |= EditorPropertyUI::DragFloat2("UV", &materialData.uv.x, 0.01f, -64.0f, 64.0f);
            }
            break;
        }
        case MaterialAssetType::Color:
        case MaterialAssetType::LitColor:
        case MaterialAssetType::FlatColor:
        default:{
            Math3D::Vec4 color = materialData.color;
            if(EditorPropertyUI::ColorEdit4("Color", &color.x)){
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
            notifyEditedAsset(filePath, assetRoot);
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
    changed |= EditorPropertyUI::InputText("Material Name", materialObjectName, sizeof(materialObjectName));
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
            notifyEditedAsset(filePath, assetRoot);
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
    changed |= EditorPropertyUI::InputText("Model Name", modelAssetName, sizeof(modelAssetName));
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
    if(EditorPropertyUI::Checkbox("Force Smooth Normals", &forceSmoothNormals)){
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
            notifyEditedAsset(filePath, assetRoot);
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

    notifyEditedAsset(materialAssetPath, assetRoot);
    notifyEditedAsset(materialPath, assetRoot);
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

    notifyEditedAsset(modelAssetPath, assetRoot);
    EditorAssetUI::InvalidateAllThumbnails();
    statusIsError = false;
    statusMessage = "Imported model asset: " + modelAssetPath.filename().string();
    return true;
}

bool FilePreviewWidget::importCurrentImageAsAsset(){
    if(!isImageFile || isImageAssetFile){
        statusIsError = true;
        statusMessage = "No source image selected for import.";
        return false;
    }

    const std::string sourceAssetRef = toAssetRef(filePath, assetRoot);
    if(sourceAssetRef.empty()){
        statusIsError = true;
        statusMessage = "Failed to resolve source image asset ref.";
        return false;
    }

    std::string fileName = StringUtils::Trim(imageImportAssetPath);
    if(fileName.empty()){
        fileName = filePath.stem().string();
    }
    if(fileName.empty()){
        fileName = "ImportedImage";
    }

    std::string lowerName = StringUtils::ToLowerCase(fileName);
    if(!StringUtils::EndsWith(lowerName, ".image.asset")){
        fileName += ".image.asset";
    }

    const std::filesystem::path targetPath = filePath.parent_path() / fileName;
    if(pathExists(targetPath)){
        statusIsError = true;
        statusMessage = "Target image asset already exists: " + targetPath.filename().string();
        return false;
    }

    ImageAssetData data = imageImportData;
    data.name = targetPath.filename().string();
    data.linkParentRef = sourceAssetRef;
    data.sourceImageRef = sourceAssetRef;

    std::string error;
    if(!ImageAssetIO::SaveToAbsolutePath(targetPath, data, &error)){
        statusIsError = true;
        statusMessage = error.empty() ? "Failed to save image asset." : error;
        return false;
    }

    notifyEditedAsset(targetPath, assetRoot);
    EditorAssetUI::InvalidateAllThumbnails();
    statusIsError = false;
    statusMessage = "Imported image asset: " + targetPath.filename().string();
    return true;
}

void FilePreviewWidget::drawImageFilePreview(){
    const std::string sourceAssetRef = toAssetRef(filePath, assetRoot);
    auto preview = loadTextureAsset(sourceAssetRef);

    if(preview && preview->getID() != 0){
        ImGui::TextDisabled("Resolution: %d x %d", preview->getWidth(), preview->getHeight());
    }else{
        ImGui::TextDisabled("Preview unavailable for this image.");
    }

    const float availW = ImGui::GetContentRegionAvail().x;
    const float imageSize = Math3D::Clamp(availW > 1.0f ? availW : 196.0f, 96.0f, 340.0f);
    if(preview && preview->getID() != 0){
        ImGui::Image((ImTextureID)(intptr_t)preview->getID(), ImVec2(imageSize, imageSize), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }else{
        ImGui::BeginDisabled();
        ImGui::Button("Preview Unavailable", ImVec2(imageSize, imageSize));
        ImGui::EndDisabled();
    }

    if(ImGui::Button("Import As Image Asset...")){
        imageImportData = ImageAssetData{};
        imageImportData.filterMode = ImageAssetFilterMode::Nearest;
        imageImportData.wrapMode = ImageAssetWrapMode::Repeat;
        imageImportData.mapType = ImageAssetMapType::Color;
        imageImportData.supportsAlpha = 1;
        imageImportData.flipVertical = 1;
        imageImportData.linkParentRef = sourceAssetRef;
        imageImportData.sourceImageRef = sourceAssetRef;

        std::filesystem::path defaultPath = filePath.parent_path() / (filePath.stem().string() + ".image.asset");
        int suffixIndex = 1;
        while(pathExists(defaultPath)){
            defaultPath = filePath.parent_path() /
                          StringUtils::Format("%s_%d.image.asset", filePath.stem().string().c_str(), suffixIndex);
            suffixIndex++;
        }
        copyBuffer(imageImportAssetPath, sizeof(imageImportAssetPath), defaultPath.filename().string());
        imageImportPopupOpen = true;
        ImGui::OpenPopup("Import Image Asset");
    }

    if(ImGui::BeginPopupModal("Import Image Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        EditorPropertyUI::InputText("Asset File", imageImportAssetPath, sizeof(imageImportAssetPath));
        bool supportsAlpha = (imageImportData.supportsAlpha != 0);
        if(EditorPropertyUI::Checkbox("Supports Alpha", &supportsAlpha)){
            imageImportData.supportsAlpha = supportsAlpha ? 1 : 0;
        }
        bool flipVertical = (imageImportData.flipVertical != 0);
        if(EditorPropertyUI::Checkbox("Flip Vertical", &flipVertical)){
            imageImportData.flipVertical = flipVertical ? 1 : 0;
        }

        int filterIndex = static_cast<int>(imageImportData.filterMode);
        const char* filterNames[] = {"Nearest", "Linear", "Trilinear"};
        if(EditorPropertyUI::Combo("Filter", &filterIndex, filterNames, IM_ARRAYSIZE(filterNames))){
            imageImportData.filterMode = static_cast<ImageAssetFilterMode>(std::clamp(filterIndex, 0, 2));
        }

        int wrapIndex = static_cast<int>(imageImportData.wrapMode);
        const char* wrapNames[] = {"Repeat", "ClampEdge", "ClampBorder"};
        if(EditorPropertyUI::Combo("Wrap", &wrapIndex, wrapNames, IM_ARRAYSIZE(wrapNames))){
            imageImportData.wrapMode = static_cast<ImageAssetWrapMode>(std::clamp(wrapIndex, 0, 2));
        }

        int mapTypeIndex = static_cast<int>(imageImportData.mapType);
        const char* mapTypeNames[] = {"Color", "Normal", "Height", "Roughness", "Metallic", "Occlusion", "Emissive", "Opacity", "Data"};
        if(EditorPropertyUI::Combo("Map Type", &mapTypeIndex, mapTypeNames, IM_ARRAYSIZE(mapTypeNames))){
            imageImportData.mapType = static_cast<ImageAssetMapType>(std::clamp(mapTypeIndex, 0, 8));
        }

        ImGui::TextDisabled("Source: %s", sourceAssetRef.c_str());

        bool closePopup = false;
        if(ImGui::Button("Create")){
            if(importCurrentImageAsAsset()){
                closePopup = true;
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")){
            closePopup = true;
            statusIsError = false;
            statusMessage.clear();
        }

        if(closePopup){
            imageImportPopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if(statusMessage.empty()){
        ImGui::TextDisabled("Preview uses nearest filtering in the file picker.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }
}

void FilePreviewWidget::drawImageAssetEditor(){
    bool changed = false;
    changed |= EditorPropertyUI::InputText("Asset Name", imageAssetName, sizeof(imageAssetName));
    changed |= EditorAssetUI::DrawAssetDropInput(
        "Source Image",
        imageAssetSource,
        sizeof(imageAssetSource),
        EditorAssetUI::AssetKind::Image
    );

    bool supportsAlpha = (imageAssetData.supportsAlpha != 0);
    if(EditorPropertyUI::Checkbox("Supports Alpha", &supportsAlpha)){
        imageAssetData.supportsAlpha = supportsAlpha ? 1 : 0;
        changed = true;
    }

    bool flipVertical = (imageAssetData.flipVertical != 0);
    if(EditorPropertyUI::Checkbox("Flip Vertical", &flipVertical)){
        imageAssetData.flipVertical = flipVertical ? 1 : 0;
        changed = true;
    }

    int filterIndex = static_cast<int>(imageAssetData.filterMode);
    const char* filterNames[] = {"Nearest", "Linear", "Trilinear"};
    if(EditorPropertyUI::Combo("Filter", &filterIndex, filterNames, IM_ARRAYSIZE(filterNames))){
        imageAssetData.filterMode = static_cast<ImageAssetFilterMode>(std::clamp(filterIndex, 0, 2));
        changed = true;
    }

    int wrapIndex = static_cast<int>(imageAssetData.wrapMode);
    const char* wrapNames[] = {"Repeat", "ClampEdge", "ClampBorder"};
    if(EditorPropertyUI::Combo("Wrap", &wrapIndex, wrapNames, IM_ARRAYSIZE(wrapNames))){
        imageAssetData.wrapMode = static_cast<ImageAssetWrapMode>(std::clamp(wrapIndex, 0, 2));
        changed = true;
    }

    int mapTypeIndex = static_cast<int>(imageAssetData.mapType);
    const char* mapTypeNames[] = {"Color", "Normal", "Height", "Roughness", "Metallic", "Occlusion", "Emissive", "Opacity", "Data"};
    if(EditorPropertyUI::Combo("Map Type", &mapTypeIndex, mapTypeNames, IM_ARRAYSIZE(mapTypeNames))){
        imageAssetData.mapType = static_cast<ImageAssetMapType>(std::clamp(mapTypeIndex, 0, 8));
        changed = true;
    }

    if(changed){
        imageAssetData.name = imageAssetName;
        imageAssetData.sourceImageRef = imageAssetSource;
        imageAssetSavePending = true;
    }

    const bool commitEditsNow = imageAssetSavePending && !ImGui::IsAnyItemActive();
    if(commitEditsNow){
        std::filesystem::path sourcePath;
        std::error_code ec;
        const bool sourceIsRef = AssetDescriptorUtils::AssetRefToAbsolutePath(imageAssetData.sourceImageRef, sourcePath);
        if(!sourceIsRef || sourcePath.empty() || !std::filesystem::exists(sourcePath, ec) || std::filesystem::is_directory(sourcePath, ec)){
            statusIsError = true;
            statusMessage = "Source Image must point to a valid raw image asset.";
        }else if(ImageAssetIO::IsImageAssetPath(sourcePath) || !ImageAssetIO::IsRawImagePath(sourcePath)){
            statusIsError = true;
            statusMessage = "Source Image must reference a raw image file (png/jpg/bmp/tga/dds/hdr).";
        }else{
            std::string error;
            if(ImageAssetIO::SaveToAbsolutePath(filePath, imageAssetData, &error)){
                statusIsError = false;
                statusMessage = "Saved.";
                lastWriteTime = std::filesystem::last_write_time(backingWritePath(filePath), ec);
                imageAssetSavePending = false;
                EditorAssetUI::InvalidateAllThumbnails();
                notifyEditedAsset(filePath, assetRoot);
            }else{
                statusIsError = true;
                statusMessage = error.empty() ? "Failed to save image asset." : error;
            }
        }
    }

    std::string selfAssetRef = toAssetRef(filePath, assetRoot);
    auto preview = loadTextureAsset(selfAssetRef);
    const float availW = ImGui::GetContentRegionAvail().x;
    const float imageSize = Math3D::Clamp(availW > 1.0f ? availW : 196.0f, 96.0f, 340.0f);
    if(preview && preview->getID() != 0){
        ImGui::Image((ImTextureID)(intptr_t)preview->getID(), ImVec2(imageSize, imageSize), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }else{
        ImGui::BeginDisabled();
        ImGui::Button("Preview Unavailable", ImVec2(imageSize, imageSize));
        ImGui::EndDisabled();
    }

    if(imageAssetSavePending){
        ImGui::TextDisabled("Release control to apply changes.");
    }else if(statusMessage.empty()){
        ImGui::TextDisabled("Edits save automatically.");
    }else if(statusIsError){
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", statusMessage.c_str());
    }else{
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", statusMessage.c_str());
    }
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
    if(EditorPropertyUI::BeginCombo("MTL Material", comboPreview)){
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
