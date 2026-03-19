/**
 * @file src/Editor/Core/EditorAssetUI.cpp
 * @brief Implementation for EditorAssetUI.
 */

#include "Editor/Core/EditorAssetUI.h"
#include "Editor/Core/EditorPropertyUI.h"
#include "Assets/Bundles/AssetBundle.h"
#include "Assets/Core/Asset.h"
#include "Assets/Bundles/AssetBundleRegistry.h"
#include "Assets/Core/AssetDescriptorUtils.h"
#include "Assets/Descriptors/ImageAsset.h"
#include "Rendering/Lighting/Environment.h"
#include "Rendering/Core/FrameBuffer.h"
#include "Foundation/Logging/Logbot.h"
#include "Assets/Descriptors/MaterialAsset.h"
#include "Rendering/Materials/MaterialDefaults.h"
#include "Rendering/Geometry/ModelPartPrefabs.h"
#include "Rendering/Materials/PBRMaterial.h"
#include "Rendering/Core/Screen.h"
#include "Rendering/Textures/SkyBox.h"
#include "Foundation/Util/StringUtils.h"
#include "Rendering/Textures/Texture.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {
    constexpr const char* kAssetPayloadType = "EDITOR_ASSET_DND";
    constexpr size_t kMaxPathChars = 512;

    /// @brief Represents Asset Drag Payload data.
    struct AssetDragPayload {
        int kind = static_cast<int>(EditorAssetUI::AssetKind::Unknown);
        int isDirectory = 0;
        char absolutePath[kMaxPathChars] = {};
        char assetRef[kMaxPathChars] = {};
    };

    /// @brief Represents Thumbnail Cache Entry data.
    struct ThumbnailCacheEntry {
        std::shared_ptr<Texture> texture;
        std::uint64_t sourceRevision = 0;
        int lastValidationFrame = -100000;
        int lastUsedFrame = -100000;
    };

    std::unordered_map<std::string, ThumbnailCacheEntry> g_imageThumbnailCache;

    /// @brief Represents Material Thumbnail Cache Entry data.
    struct MaterialThumbnailCacheEntry {
        std::shared_ptr<FrameBuffer> framebuffer;
        std::shared_ptr<Texture> texture;
        std::filesystem::file_time_type sourceWriteTime{};
        std::filesystem::file_time_type objectWriteTime{};
        bool hasSourceWriteTime = false;
        bool hasObjectWriteTime = false;
        int lastValidationFrame = -100000;
        int lastUsedFrame = -100000;
    };

    std::unordered_map<std::string, MaterialThumbnailCacheEntry> g_materialThumbnailCache;
    std::shared_ptr<ModelPart> g_materialThumbnailSphere;
    std::shared_ptr<Camera> g_materialThumbnailCamera;
    std::shared_ptr<Environment> g_materialThumbnailEnvironment;
    std::shared_ptr<SkyBox> g_materialThumbnailSkyBox;
    std::shared_ptr<Material> g_materialThumbnailFallbackMaterial;
    int g_materialThumbnailSize = 0;

    /// @brief Represents Asset Picker State data.
    struct AssetPickerState{
        bool windowOpen = false;
        bool focusRequested = false;
        int drawnFrame = -1;
        ImGuiID ownerId = 0;
        EditorAssetUI::AssetBrowserWidget browser;
        std::vector<EditorAssetUI::AssetKind> requestedKinds;
        bool hasPendingSelection = false;
        ImGuiID pendingOwnerId = 0;
        EditorAssetUI::PickedAssetRef pendingSelection;
    };

    constexpr const char* kAssetPickerWindowId = "Editor Asset Picker";
    AssetPickerState g_assetPickerState;

    std::string requestedKindsTooltip(const EditorAssetUI::AssetKind* requestedKinds, size_t requestedKindCount);
    constexpr int kThumbnailValidationIntervalFrames = 30;
    constexpr int kThumbnailCachePruneIntervalFrames = 120;
    constexpr int kThumbnailCacheIdleFrames = 600;
    constexpr size_t kMaxImageThumbnailCacheEntries = 48;
    constexpr size_t kMaxMaterialThumbnailCacheEntries = 96;
    int g_lastThumbnailCachePruneFrame = -100000;

    template <typename TEntry>
    void pruneThumbnailCacheMap(std::unordered_map<std::string, TEntry>& cache, int frameNow, int idleFrames, size_t maxEntries){
        if(cache.empty()){
            return;
        }

        for(auto it = cache.begin(); it != cache.end();){
            if((frameNow - it->second.lastUsedFrame) > idleFrames){
                it = cache.erase(it);
            }else{
                ++it;
            }
        }

        while(cache.size() > maxEntries){
            auto oldestIt = cache.end();
            for(auto it = cache.begin(); it != cache.end(); ++it){
                if(oldestIt == cache.end() || it->second.lastUsedFrame < oldestIt->second.lastUsedFrame){
                    oldestIt = it;
                }
            }
            if(oldestIt == cache.end()){
                break;
            }
            cache.erase(oldestIt);
        }
    }

    void pruneThumbnailCaches(int frameNow){
        if((frameNow - g_lastThumbnailCachePruneFrame) < kThumbnailCachePruneIntervalFrames){
            return;
        }
        g_lastThumbnailCachePruneFrame = frameNow;

        pruneThumbnailCacheMap(g_imageThumbnailCache, frameNow, kThumbnailCacheIdleFrames, kMaxImageThumbnailCacheEntries);
        pruneThumbnailCacheMap(g_materialThumbnailCache, frameNow, kThumbnailCacheIdleFrames, kMaxMaterialThumbnailCacheEntries);
    }

    std::shared_ptr<Material> buildThumbnailFallbackMaterial(){
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

    std::shared_ptr<Material> getRenderableThumbnailMaterial(const std::shared_ptr<Material>& candidate){
        if(candidate){
            auto shader = candidate->getShader();
            if(shader && shader->getID() != 0){
                return candidate;
            }
        }

        if(!g_materialThumbnailFallbackMaterial ||
           !g_materialThumbnailFallbackMaterial->getShader() ||
           g_materialThumbnailFallbackMaterial->getShader()->getID() == 0){
            g_materialThumbnailFallbackMaterial = buildThumbnailFallbackMaterial();
        }

        if(g_materialThumbnailFallbackMaterial &&
           g_materialThumbnailFallbackMaterial->getShader() &&
           g_materialThumbnailFallbackMaterial->getShader()->getID() != 0){
            return g_materialThumbnailFallbackMaterial;
        }

        return nullptr;
    }

    std::string toLower(std::string value){
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool endsWith(const std::string& value, const std::string& suffix){
        if(value.size() < suffix.size()){
            return false;
        }
        return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
    }

    const char* kindGlyph(EditorAssetUI::AssetKind kind){
        using EditorAssetUI::AssetKind;
        switch(kind){
            case AssetKind::Directory:      return "DIR";
            case AssetKind::Image:          return "IMG";
            case AssetKind::Model:          return "MOD";
            case AssetKind::ModelAsset:     return "MOA";
            case AssetKind::ShaderVertex:   return "VTX";
            case AssetKind::ShaderFragment: return "FRG";
            case AssetKind::ShaderGrometry: return "GEO";
            case AssetKind::ShaderCompute:  return "CMP";
            case AssetKind::ShaderTesselation:return "TES";
            case AssetKind::ShaderTask:     return "TSK";
            case AssetKind::ShaderRaytrace: return "RTX";
            case AssetKind::ShaderGeneric:  return "SHD";
            case AssetKind::ShaderAsset:    return "SAS";
            case AssetKind::SkyboxAsset:    return "SKY";
            case AssetKind::EnvironmentAsset:return "ENV";
            case AssetKind::LensFlareAsset: return "FLR";
            case AssetKind::EffectAsset:    return "EFX";
            case AssetKind::MaterialAsset:  return "MAT";
            case AssetKind::Material:       return "MTL";
            case AssetKind::Font:           return "FNT";
            case AssetKind::Text:           return "TXT";
            case AssetKind::Any:
            case AssetKind::Unknown:
            default:                        return "FIL";
        }
    }

    ImU32 kindColor(EditorAssetUI::AssetKind kind){
        using EditorAssetUI::AssetKind;
        switch(kind){
            case AssetKind::Directory:      return IM_COL32(88, 130, 255, 255);
            case AssetKind::Image:          return IM_COL32(72, 190, 124, 255);
            case AssetKind::Model:          return IM_COL32(244, 160, 70, 255);
            case AssetKind::ModelAsset:     return IM_COL32(219, 190, 102, 255);
            case AssetKind::ShaderVertex:   return IM_COL32(188, 112, 255, 255);
            case AssetKind::ShaderFragment: return IM_COL32(255, 116, 156, 255);
            case AssetKind::ShaderGrometry: return IM_COL32(255, 160, 98, 255);
            case AssetKind::ShaderCompute:  return IM_COL32(82, 204, 190, 255);
            case AssetKind::ShaderTesselation:return IM_COL32(196, 170, 255, 255);
            case AssetKind::ShaderTask:     return IM_COL32(122, 174, 255, 255);
            case AssetKind::ShaderRaytrace: return IM_COL32(255, 94, 94, 255);
            case AssetKind::ShaderGeneric:  return IM_COL32(166, 96, 242, 255);
            case AssetKind::ShaderAsset:    return IM_COL32(237, 132, 255, 255);
            case AssetKind::SkyboxAsset:    return IM_COL32(96, 182, 255, 255);
            case AssetKind::EnvironmentAsset:return IM_COL32(104, 186, 140, 255);
            case AssetKind::LensFlareAsset: return IM_COL32(255, 182, 76, 255);
            case AssetKind::EffectAsset:    return IM_COL32(255, 138, 90, 255);
            case AssetKind::MaterialAsset:  return IM_COL32(90, 201, 155, 255);
            case AssetKind::Material:       return IM_COL32(62, 168, 212, 255);
            case AssetKind::Font:           return IM_COL32(104, 212, 219, 255);
            case AssetKind::Text:           return IM_COL32(152, 152, 152, 255);
            case AssetKind::Any:
            case AssetKind::Unknown:
            default:                        return IM_COL32(132, 132, 132, 255);
        }
    }

    void copyStringFixed(char* dst, size_t dstSize, const std::string& src){
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

    bool kindMatchesShader(EditorAssetUI::AssetKind offered, EditorAssetUI::AssetKind requested){
        using EditorAssetUI::AssetKind;
        auto isSpecificShaderStage = [](AssetKind kind){
            return kind == AssetKind::ShaderVertex ||
                   kind == AssetKind::ShaderFragment ||
                   kind == AssetKind::ShaderGrometry ||
                   kind == AssetKind::ShaderCompute ||
                   kind == AssetKind::ShaderTesselation ||
                   kind == AssetKind::ShaderTask ||
                   kind == AssetKind::ShaderRaytrace;
        };
        if(requested == AssetKind::ShaderGeneric){
            return isSpecificShaderStage(offered) || offered == AssetKind::ShaderGeneric;
        }
        if(requested == AssetKind::ShaderVertex){
            return (offered == AssetKind::ShaderVertex || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderFragment){
            return (offered == AssetKind::ShaderFragment || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderGrometry){
            return (offered == AssetKind::ShaderGrometry || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderCompute){
            return (offered == AssetKind::ShaderCompute || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderTesselation){
            return (offered == AssetKind::ShaderTesselation || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderTask){
            return (offered == AssetKind::ShaderTask || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderRaytrace){
            return (offered == AssetKind::ShaderRaytrace || offered == AssetKind::ShaderGeneric);
        }
        if(requested == AssetKind::ShaderAsset){
            return offered == AssetKind::ShaderAsset;
        }
        if(requested == AssetKind::SkyboxAsset){
            return offered == AssetKind::SkyboxAsset;
        }
        if(requested == AssetKind::EnvironmentAsset){
            return offered == AssetKind::EnvironmentAsset;
        }
        if(requested == AssetKind::LensFlareAsset){
            return offered == AssetKind::LensFlareAsset;
        }
        if(requested == AssetKind::EffectAsset){
            return offered == AssetKind::EffectAsset;
        }
        if(requested == AssetKind::MaterialAsset){
            return offered == AssetKind::MaterialAsset;
        }
        if(requested == AssetKind::Material){
            return offered == AssetKind::Material || offered == AssetKind::MaterialAsset;
        }
        if(requested == AssetKind::ModelAsset){
            return offered == AssetKind::ModelAsset;
        }
        if(requested == AssetKind::Model){
            return offered == AssetKind::Model;
        }
        return false;
    }

    std::shared_ptr<Texture> getImageThumbnail(const EditorAssetUI::AssetTransaction& tx){
        try{
            if(tx.kind != EditorAssetUI::AssetKind::Image || tx.assetRef.empty()){
                return nullptr;
            }

            const int frameNow = ImGui::GetFrameCount();
            pruneThumbnailCaches(frameNow);

            auto cachedIt = g_imageThumbnailCache.find(tx.assetRef);
            if(cachedIt != g_imageThumbnailCache.end()){
                ThumbnailCacheEntry& cached = cachedIt->second;
                if(cached.texture){
                    const std::uint64_t currentRevision = ImageAssetIO::GetTextureRefRevision(tx.assetRef);
                    if((frameNow - cached.lastValidationFrame) < kThumbnailValidationIntervalFrames){
                        cached.lastUsedFrame = frameNow;
                        return cached.texture;
                    }

                    if(cached.sourceRevision == currentRevision){
                        cached.lastValidationFrame = frameNow;
                        cached.lastUsedFrame = frameNow;
                        return cached.texture;
                    }
                }
            }

            auto texture = ImageAssetIO::InstantiateTextureFromRef(tx.assetRef, nullptr, nullptr);
            if(!texture || texture->getID() == 0){
                return nullptr;
            }
            texture->setFilterMode(TextureFilterMode::NEAREST);

            ThumbnailCacheEntry entry;
            entry.texture = texture;
            entry.sourceRevision = ImageAssetIO::GetTextureRefRevision(tx.assetRef);
            entry.lastValidationFrame = frameNow;
            entry.lastUsedFrame = frameNow;
            g_imageThumbnailCache[tx.assetRef] = entry;
            return texture;
        }catch(const std::exception& e){
            LogBot.Log(LOG_ERRO, "Image thumbnail failed for '%s': %s", tx.assetRef.c_str(), e.what());
        }catch(...){
            LogBot.Log(LOG_ERRO, "Image thumbnail failed for '%s': unknown exception", tx.assetRef.c_str());
        }
        return nullptr;
    }

    bool assetRefToAbsolutePath(const std::string& assetRef, std::filesystem::path& outPath){
        return AssetDescriptorUtils::AssetRefToAbsolutePath(assetRef, outPath);
    }

    std::filesystem::path getAssetRootPathForPicker(){
        return AssetDescriptorUtils::GetAssetRootPath().lexically_normal();
    }

    bool pathWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root){
        if(AssetBundleRegistry::IsVirtualEntryPath(path)){
            return true;
        }

        std::error_code ec;
        std::filesystem::path normalizedRoot = std::filesystem::weakly_canonical(root, ec);
        if(ec){
            normalizedRoot = root.lexically_normal();
        }

        std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, ec);
        if(ec){
            normalizedPath = path.lexically_normal();
        }

        std::filesystem::path rel = normalizedPath.lexically_relative(normalizedRoot);
        if(rel.empty()){
            return normalizedPath == normalizedRoot;
        }
        return !StringUtils::BeginsWith(rel.generic_string(), "..");
    }

    void requestAssetPicker(ImGuiID ownerId, const EditorAssetUI::AssetKind* requestedKinds, size_t requestedKindCount, const std::string& currentValue){
        g_assetPickerState.ownerId = ownerId;
        g_assetPickerState.browser.setAssetRoot(getAssetRootPathForPicker());
        g_assetPickerState.requestedKinds.clear();
        if(requestedKinds && requestedKindCount > 0){
            g_assetPickerState.requestedKinds.assign(requestedKinds, requestedKinds + requestedKindCount);
        }
        if(g_assetPickerState.requestedKinds.empty()){
            g_assetPickerState.requestedKinds.push_back(EditorAssetUI::AssetKind::Any);
        }
        g_assetPickerState.browser.setRequestedKinds(
            g_assetPickerState.requestedKinds.data(),
            g_assetPickerState.requestedKinds.size()
        );
        g_assetPickerState.browser.syncSelectionFromValue(currentValue);
        g_assetPickerState.windowOpen = true;
        g_assetPickerState.focusRequested = true;
    }

    bool consumeAssetPickerSelection(ImGuiID ownerId, EditorAssetUI::PickedAssetRef& outSelection){
        if(!g_assetPickerState.hasPendingSelection || g_assetPickerState.pendingOwnerId != ownerId){
            return false;
        }
        outSelection = g_assetPickerState.pendingSelection;
        g_assetPickerState.hasPendingSelection = false;
        g_assetPickerState.pendingOwnerId = 0;
        g_assetPickerState.pendingSelection = EditorAssetUI::PickedAssetRef{};
        return true;
    }

    bool consumeAssetPickerSelection(ImGuiID ownerId, std::string& outAssetRef){
        EditorAssetUI::PickedAssetRef selection;
        if(!consumeAssetPickerSelection(ownerId, selection)){
            return false;
        }

        outAssetRef = selection.transaction.assetRef;
        return true;
    }

    void drawAssetPickerWindow(){
        const int frame = ImGui::GetFrameCount();
        if(g_assetPickerState.drawnFrame == frame){
            return;
        }
        g_assetPickerState.drawnFrame = frame;

        if(!g_assetPickerState.windowOpen){
            return;
        }

        if(g_assetPickerState.focusRequested){
            ImGui::SetNextWindowFocus();
            g_assetPickerState.focusRequested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(760.0f, 500.0f), ImGuiCond_FirstUseEver);
        bool keepWindowOpen = g_assetPickerState.windowOpen;
        const bool bundleViewActive = g_assetPickerState.browser.isBrowsingBundle();
        if(bundleViewActive){
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.095f, 0.135f, 1.0f));
        }
        if(ImGui::Begin(kAssetPickerWindowId, &keepWindowOpen, ImGuiWindowFlags_NoCollapse)){
        ImGui::TextDisabled("Allowed: %s", requestedKindsTooltip(
            g_assetPickerState.requestedKinds.data(),
            g_assetPickerState.requestedKinds.size()).c_str()
        );

        const float footerReserve = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + 8.0f;
        const EditorAssetUI::AssetBrowserWidget::DrawResult drawResult =
            g_assetPickerState.browser.draw("EditorAssetPickerBrowser", footerReserve);

        auto commitSelection = [&](){
            EditorAssetUI::PickedAssetRef selection;
            if(!g_assetPickerState.browser.tryGetSelectedReference(selection)){
                return false;
            }

            g_assetPickerState.hasPendingSelection = true;
            g_assetPickerState.pendingOwnerId = g_assetPickerState.ownerId;
            g_assetPickerState.pendingSelection = selection;
            keepWindowOpen = false;
            return true;
        };

        if(drawResult.selectionActivated){
            commitSelection();
        }

        const bool canSelect = g_assetPickerState.browser.hasSelectedAsset();
        if(!canSelect){
            ImGui::BeginDisabled();
        }
        if(ImGui::Button("Select")){
            commitSelection();
        }
        if(!canSelect){
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")){
            keepWindowOpen = false;
        }
        }
        ImGui::End();
        if(bundleViewActive){
            ImGui::PopStyleColor();
        }
        g_assetPickerState.windowOpen = keepWindowOpen;
    }

    bool tryGetWriteTime(const std::filesystem::path& path, std::filesystem::file_time_type& outWriteTime){
        std::error_code ec;
        if(path.empty() || !std::filesystem::exists(path, ec)){
            return false;
        }
        outWriteTime = std::filesystem::last_write_time(path, ec);
        return !ec;
    }

    void ensureMaterialThumbnailResources(int size){
        if(size < 32){
            size = 32;
        }
        if(g_materialThumbnailSize != size){
            g_materialThumbnailSize = size;
            g_materialThumbnailCamera.reset();
        }

        if(!g_materialThumbnailCamera){
            g_materialThumbnailCamera = Camera::CreatePerspective(
                45.0f,
                Math3D::Vec2((float)size, (float)size),
                0.1f,
                64.0f
            );
        }
        if(g_materialThumbnailCamera){
            g_materialThumbnailCamera->resize((float)size, (float)size);
            g_materialThumbnailCamera->transform().setPosition(Math3D::Vec3(2.35f, 1.25f, 2.35f));
            g_materialThumbnailCamera->transform().lookAt(Math3D::Vec3(0.0f, 0.0f, 0.0f));
        }

        if(!g_materialThumbnailSphere){
            g_materialThumbnailSphere = ModelPartPrefabs::MakeSphere(1.0f, 28, 18);
        }

        if(!g_materialThumbnailSkyBox){
            g_materialThumbnailSkyBox = SkyBoxLoader::CreateSkyBox("@assets/images/skybox/default", "skybox_default");
        }

        if(!g_materialThumbnailEnvironment){
            g_materialThumbnailEnvironment = std::make_shared<Environment>();
            g_materialThumbnailEnvironment->setLightingEnabled(true);
            auto& lights = g_materialThumbnailEnvironment->getLightManager();
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
        }

        if(g_materialThumbnailEnvironment){
            g_materialThumbnailEnvironment->setSkyBox(g_materialThumbnailSkyBox);
        }
    }

    void renderMaterialThumbnail(const std::shared_ptr<Material>& material,
                                 const std::shared_ptr<FrameBuffer>& framebuffer){
        auto renderMaterial = getRenderableThumbnailMaterial(material);
        if(!renderMaterial || !framebuffer || !g_materialThumbnailSphere || !g_materialThumbnailCamera){
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

        framebuffer->bind();
        framebuffer->clear(Color::CLEAR);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CW);
        glDisable(GL_BLEND);

        Screen::MakeCameraCurrent(g_materialThumbnailCamera);
        Screen::MakeEnvironmentCurrent(g_materialThumbnailEnvironment);

        g_materialThumbnailSphere->material = renderMaterial;
        if(auto pbr = Material::GetAs<PBRMaterial>(g_materialThumbnailSphere->material)){
            if(g_materialThumbnailSkyBox && g_materialThumbnailSkyBox->getCubeMap()){
                pbr->EnvMap = g_materialThumbnailSkyBox->getCubeMap();
                if(pbr->UseEnvMap.get() == 0){
                    pbr->UseEnvMap = 1;
                }
            }
        }
        g_materialThumbnailSphere->localTransform.setRotation(12.0f, 26.0f, 0.0f);
        g_materialThumbnailSphere->localTransform.setScale(Math3D::Vec3(1.08f, 1.08f, 1.08f));

        const Math3D::Mat4 identity;
        glFrontFace(GL_CW);
        g_materialThumbnailSphere->draw(identity, g_materialThumbnailCamera->getViewMatrix(), g_materialThumbnailCamera->getProjectionMatrix());
        glFrontFace(GL_CCW);
        g_materialThumbnailSphere->draw(identity, g_materialThumbnailCamera->getViewMatrix(), g_materialThumbnailCamera->getProjectionMatrix());

        framebuffer->unbind();

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

    std::shared_ptr<Texture> getMaterialThumbnail(const EditorAssetUI::AssetTransaction& tx){
        try{
            using EditorAssetUI::AssetKind;
            if(tx.kind != AssetKind::MaterialAsset && tx.kind != AssetKind::Material){
                return nullptr;
            }
            if(tx.assetRef.empty()){
                return nullptr;
            }

            const int frameNow = ImGui::GetFrameCount();
            pruneThumbnailCaches(frameNow);

            auto it = g_materialThumbnailCache.find(tx.assetRef);
            if(it != g_materialThumbnailCache.end()){
                MaterialThumbnailCacheEntry& cached = it->second;
                if(cached.texture){
                    cached.lastUsedFrame = frameNow;
                    return cached.texture;
                }
            }

            std::filesystem::file_time_type objectWriteTime{};
            std::filesystem::file_time_type sourceWriteTime{};
            bool hasObjectWriteTime = false;
            bool hasSourceWriteTime = false;

            if(!tx.absolutePath.empty()){
                hasObjectWriteTime = tryGetWriteTime(tx.absolutePath, objectWriteTime);
            }

            std::string resolvedAssetRef;
            std::string error;
            if(!MaterialAssetIO::ResolveMaterialAssetRef(tx.assetRef, resolvedAssetRef, &error)){
                return nullptr;
            }

            std::filesystem::path sourcePath;
            if(assetRefToAbsolutePath(resolvedAssetRef, sourcePath)){
                hasSourceWriteTime = tryGetWriteTime(sourcePath, sourceWriteTime);
            }

            auto material = MaterialAssetIO::InstantiateMaterialFromRef(tx.assetRef, nullptr, &error);
            material = getRenderableThumbnailMaterial(material);
            if(!material){
                return nullptr;
            }

            constexpr int kThumbSize = 96;
            ensureMaterialThumbnailResources(kThumbSize);
            MaterialThumbnailCacheEntry& entry = g_materialThumbnailCache[tx.assetRef];
            const bool needsResize =
                !entry.framebuffer ||
                !entry.texture ||
                entry.framebuffer->getWidth() != kThumbSize ||
                entry.framebuffer->getHeight() != kThumbSize;
            if(needsResize){
                entry.framebuffer = FrameBuffer::Create(kThumbSize, kThumbSize);
                entry.texture = Texture::CreateEmpty(kThumbSize, kThumbSize);
                if(entry.framebuffer && entry.texture){
                    entry.framebuffer->attachTexture(entry.texture);
                }
            }

            if(!entry.framebuffer || !entry.texture){
                return nullptr;
            }

            renderMaterialThumbnail(material, entry.framebuffer);
            entry.hasObjectWriteTime = hasObjectWriteTime;
            entry.objectWriteTime = objectWriteTime;
            entry.hasSourceWriteTime = hasSourceWriteTime;
            entry.sourceWriteTime = sourceWriteTime;
            entry.lastValidationFrame = frameNow;
            entry.lastUsedFrame = frameNow;
            return entry.texture;
        }catch(const std::exception& e){
            LogBot.Log(LOG_ERRO, "Material thumbnail failed for '%s': %s", tx.assetRef.c_str(), e.what());
        }catch(...){
            LogBot.Log(LOG_ERRO, "Material thumbnail failed for '%s': unknown exception", tx.assetRef.c_str());
        }
        return nullptr;
    }

    void drawCenteredTextureInSquare(ImDrawList* drawList,
                                     const std::shared_ptr<Texture>& texture,
                                     const ImVec2& minPos,
                                     float sideLength,
                                     bool flipY){
        if(!drawList || !texture || texture->getID() == 0 || sideLength <= 0.0f){
            return;
        }

        float texWidth = (float)texture->getWidth();
        float texHeight = (float)texture->getHeight();
        if(texWidth <= 0.0f || texHeight <= 0.0f){
            ImVec2 uvMin(0.0f, flipY ? 1.0f : 0.0f);
            ImVec2 uvMax(1.0f, flipY ? 0.0f : 1.0f);
            drawList->AddImage(
                (ImTextureID)(intptr_t)texture->getID(),
                minPos,
                ImVec2(minPos.x + sideLength, minPos.y + sideLength),
                uvMin,
                uvMax
            );
            return;
        }

        float maxDim = Math3D::Max(texWidth, texHeight);
        if(maxDim <= 0.0f){
            return;
        }

        float drawWidth = sideLength * (texWidth / maxDim);
        float drawHeight = sideLength * (texHeight / maxDim);
        ImVec2 drawMin(
            minPos.x + ((sideLength - drawWidth) * 0.5f),
            minPos.y + ((sideLength - drawHeight) * 0.5f)
        );
        ImVec2 drawMax(drawMin.x + drawWidth, drawMin.y + drawHeight);

        ImVec2 uvMin(0.0f, flipY ? 1.0f : 0.0f);
        ImVec2 uvMax(1.0f, flipY ? 0.0f : 1.0f);
        drawList->AddImage((ImTextureID)(intptr_t)texture->getID(), drawMin, drawMax, uvMin, uvMax);
    }

    std::string requestedKindsTooltip(const EditorAssetUI::AssetKind* requestedKinds, size_t requestedKindCount){
        if(!requestedKinds || requestedKindCount == 0){
            return "any";
        }
        std::string out;
        for(size_t i = 0; i < requestedKindCount; ++i){
            if(i > 0){
                out += "/";
            }
            out += kindGlyph(requestedKinds[i]);
        }
        return out;
    }
}

namespace EditorAssetUI {

AssetKind ClassifyPath(const std::filesystem::path& path, bool isDirectory){
    if(isDirectory){
        return AssetKind::Directory;
    }

    std::string pathLower = toLower(path.generic_string());
    if(endsWith(pathLower, ".shader.asset")){
        return AssetKind::ShaderAsset;
    }
    if(endsWith(pathLower, ".skybox.asset")){
        return AssetKind::SkyboxAsset;
    }
    if(endsWith(pathLower, ".environment.asset")){
        return AssetKind::EnvironmentAsset;
    }
    if(endsWith(pathLower, ".flare.asset")){
        return AssetKind::LensFlareAsset;
    }
    if(endsWith(pathLower, ".effect.asset")){
        return AssetKind::EffectAsset;
    }
    if(endsWith(pathLower, ".material.asset")){
        return AssetKind::MaterialAsset;
    }
    if(endsWith(pathLower, ".mat.asset")){
        return AssetKind::MaterialAsset;
    }
    if(endsWith(pathLower, ".image.asset")){
        return AssetKind::Image;
    }
    if(endsWith(pathLower, ".material")){
        return AssetKind::Material;
    }
    if(endsWith(pathLower, ".model.asset")){
        return AssetKind::ModelAsset;
    }

    std::string ext = toLower(path.extension().string());
    if(ext.empty()){
        return AssetKind::Unknown;
    }

    if(ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".dds" || ext == ".hdr"){
        return AssetKind::Image;
    }
    if(ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb"){
        return AssetKind::Model;
    }
    if(ext == ".vert"){
        return AssetKind::ShaderVertex;
    }
    if(ext == ".frag"){
        return AssetKind::ShaderFragment;
    }
    if(ext == ".geom" || ext == ".geo" || ext == ".gs" || ext == ".geometry"){
        return AssetKind::ShaderGrometry;
    }
    if(ext == ".tesc" || ext == ".tese" || ext == ".tess" || ext == ".tes" || ext == ".tcs"){
        return AssetKind::ShaderTesselation;
    }
    if(ext == ".comp" || ext == ".compute" || ext == ".cs"){
        return AssetKind::ShaderCompute;
    }
    if(ext == ".task" || ext == ".mesh" || ext == ".ms"){
        return AssetKind::ShaderTask;
    }
    if(ext == ".rtx" || ext == ".rgen" || ext == ".rchit" || ext == ".rmiss" || ext == ".raytrace" || ext == ".ray"){
        return AssetKind::ShaderRaytrace;
    }
    if(ext == ".glsl" || ext == ".shader"){
        return AssetKind::ShaderGeneric;
    }
    if(ext == ".ttf" || ext == ".otf" || ext == ".fnt"){
        return AssetKind::Font;
    }
    if(ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".xml" || ext == ".yaml" || ext == ".yml"){
        return AssetKind::Text;
    }
    return AssetKind::Unknown;
}

bool IsKindCompatible(AssetKind offered, AssetKind requested){
    if(requested == AssetKind::Any){
        return true;
    }
    if(kindMatchesShader(offered, requested)){
        return true;
    }
    return offered == requested;
}

bool IsKindCompatibleAny(AssetKind offered, const AssetKind* requestedKinds, size_t requestedKindCount){
    if(!requestedKinds || requestedKindCount == 0){
        return true;
    }
    for(size_t i = 0; i < requestedKindCount; ++i){
        if(IsKindCompatible(offered, requestedKinds[i])){
            return true;
        }
    }
    return false;
}

bool ResolvePickedAssetRef(const AssetTransaction& tx, PickedAssetRef& out){
    out = PickedAssetRef{};
    out.transaction = tx;
    if(tx.assetRef.empty() || tx.isDirectory){
        return false;
    }

    out.asset = AssetManager::Instance.getOrLoad(tx.assetRef);
    return true;
}

void AssetBrowserWidget::setAssetRoot(const std::filesystem::path& rootPath){
    assetRoot = rootPath.empty() ? std::filesystem::path() : rootPath.lexically_normal();
    if(currentDirectory.empty()){
        currentDirectory = assetRoot;
    }
}

void AssetBrowserWidget::setCurrentDirectory(const std::filesystem::path& directoryPath){
    currentDirectory = directoryPath.empty() ? std::filesystem::path() : directoryPath.lexically_normal();
}

void AssetBrowserWidget::setSelectedPath(const std::filesystem::path& newSelectedPath){
    selectedPath = newSelectedPath.empty() ? std::filesystem::path() : newSelectedPath.lexically_normal();
}

void AssetBrowserWidget::setRequestedKinds(const AssetKind* newRequestedKinds, size_t requestedKindCount){
    requestedKinds.clear();
    if(newRequestedKinds && requestedKindCount > 0){
        requestedKinds.assign(newRequestedKinds, newRequestedKinds + requestedKindCount);
    }
    if(requestedKinds.empty()){
        requestedKinds.push_back(AssetKind::Any);
    }
}

void AssetBrowserWidget::resetRequestedKinds(){
    requestedKinds.clear();
    requestedKinds.push_back(AssetKind::Any);
}

void AssetBrowserWidget::setTileSize(float size){
    tileSize = std::clamp(size, 56.0f, 128.0f);
}

void AssetBrowserWidget::syncSelectionFromValue(const std::string& currentValue){
    if(assetRoot.empty()){
        assetRoot = getAssetRootPathForPicker();
    }

    if(currentValue.empty()){
        selectedPath.clear();
        if(currentDirectory.empty()){
            currentDirectory = assetRoot;
        }
        return;
    }

    std::filesystem::path resolvedPath;
    bool resolvedIsDirectory = false;
    if(!assetRefToAbsolutePath(currentValue, resolvedPath) ||
       !AssetDescriptorUtils::PathExists(resolvedPath, &resolvedIsDirectory)){
        selectedPath.clear();
        if(currentDirectory.empty()){
            currentDirectory = assetRoot;
        }
        return;
    }

    resolvedPath = resolvedPath.lexically_normal();
    if(resolvedIsDirectory){
        currentDirectory = resolvedPath;
        selectedPath.clear();
        return;
    }

    currentDirectory = resolvedPath.parent_path().lexically_normal();
    selectedPath = resolvedPath;
}

void AssetBrowserWidget::goToRoot(){
    if(assetRoot.empty()){
        assetRoot = getAssetRootPathForPicker();
    }

    std::filesystem::path bundlePath;
    std::string entryPath;
    if(AssetBundleRegistry::DecodeVirtualEntryPath(currentDirectory, bundlePath, entryPath)){
        currentDirectory = AssetBundleRegistry::MakeVirtualEntryPath(bundlePath, "", true);
    }else{
        currentDirectory = assetRoot;
    }
    selectedPath.clear();
}

void AssetBrowserWidget::goUp(){
    if(assetRoot.empty()){
        assetRoot = getAssetRootPathForPicker();
    }

    std::filesystem::path bundlePath;
    std::string entryPath;
    if(AssetBundleRegistry::DecodeVirtualEntryPath(currentDirectory, bundlePath, entryPath)){
        if(entryPath.empty()){
            currentDirectory = bundlePath.parent_path().lexically_normal();
        }else{
            std::string parentDir = entryPath;
            if(!parentDir.empty() && parentDir.back() == '/'){
                parentDir.pop_back();
            }
            parentDir = std::filesystem::path(parentDir).parent_path().generic_string();
            currentDirectory = AssetBundleRegistry::MakeVirtualEntryPath(bundlePath, parentDir, true);
        }
    }else if(currentDirectory != assetRoot){
        const std::filesystem::path parent = currentDirectory.parent_path().lexically_normal();
        if(pathWithinRoot(parent, assetRoot)){
            currentDirectory = parent;
        }else{
            currentDirectory = assetRoot;
        }
    }

    selectedPath.clear();
}

AssetBrowserWidget::DrawResult AssetBrowserWidget::draw(const char* childId, float footerReserve){
    DrawResult result;

    if(assetRoot.empty()){
        assetRoot = getAssetRootPathForPicker();
    }
    if(requestedKinds.empty()){
        requestedKinds.push_back(AssetKind::Any);
    }
    if(currentDirectory.empty()){
        currentDirectory = assetRoot;
    }

    bool currentDirIsDirectory = false;
    if(!pathWithinRoot(currentDirectory, assetRoot) ||
       !AssetDescriptorUtils::PathExists(currentDirectory, &currentDirIsDirectory) ||
       !currentDirIsDirectory){
        currentDirectory = assetRoot;
        currentDirIsDirectory = true;
        if(!selectedPath.empty()){
            selectedPath.clear();
            result.selectionChanged = true;
        }
    }

    if(!selectedPath.empty() && !AssetDescriptorUtils::PathExists(selectedPath)){
        selectedPath.clear();
        result.selectionChanged = true;
    }

    ImGui::Text("Directory: %s", AssetDescriptorUtils::AbsolutePathToAssetRef(currentDirectory).c_str());
    if(ImGui::Button("Root")){
        goToRoot();
        result.selectionChanged = true;
    }

    const bool canGoUp = AssetBundleRegistry::IsVirtualEntryPath(currentDirectory) || currentDirectory != assetRoot;
    ImGui::SameLine();
    if(!canGoUp){
        ImGui::BeginDisabled();
    }
    if(ImGui::Button("Up") && canGoUp){
        goUp();
        result.selectionChanged = true;
    }
    if(!canGoUp){
        ImGui::EndDisabled();
    }

    /// @brief Represents Browser Entry data.
    struct BrowserEntry {
        std::filesystem::path path;
        AssetTransaction tx;
        bool isUp = false;
        bool opensContainer = false;
        bool canSelect = false;
    };

    auto buildDirectoryTx = [&](const std::filesystem::path& path) -> AssetTransaction {
        AssetTransaction tx{};
        if(!BuildTransaction(path, assetRoot, tx)){
            tx.absolutePath = path.lexically_normal();
            tx.assetRef = AssetDescriptorUtils::AbsolutePathToAssetRef(path);
            tx.extension.clear();
            tx.kind = AssetKind::Directory;
            tx.isDirectory = true;
        }
        return tx;
    };

    auto canSelectTx = [&](const AssetTransaction& tx) -> bool {
        if(tx.isDirectory || tx.assetRef.empty()){
            return false;
        }
        return IsKindCompatibleAny(tx.kind, requestedKinds.data(), requestedKinds.size());
    };

    auto pushEntry = [&](std::vector<BrowserEntry>& entries, const std::filesystem::path& path, bool isDirectory, bool isUp){
        BrowserEntry entry{};
        entry.path = path.lexically_normal();
        entry.isUp = isUp;
        entry.opensContainer =
            isDirectory ||
            (AssetBundle::IsBundlePath(entry.path) && !AssetBundleRegistry::IsVirtualEntryPath(entry.path));

        if(isDirectory){
            entry.tx = buildDirectoryTx(entry.path);
            entry.canSelect = false;
        }else if(BuildTransaction(entry.path, assetRoot, entry.tx)){
            entry.canSelect = canSelectTx(entry.tx);
        }else{
            return;
        }

        entries.push_back(entry);
    };

    std::vector<BrowserEntry> entries;
    if(canGoUp){
        std::filesystem::path upPath = currentDirectory;
        if(AssetBundleRegistry::IsVirtualEntryPath(currentDirectory)){
            std::filesystem::path bundlePath;
            std::string entryPath;
            if(AssetBundleRegistry::DecodeVirtualEntryPath(currentDirectory, bundlePath, entryPath)){
                if(entryPath.empty()){
                    upPath = bundlePath.parent_path();
                }else{
                    std::string parentDir = entryPath;
                    if(!parentDir.empty() && parentDir.back() == '/'){
                        parentDir.pop_back();
                    }
                    parentDir = std::filesystem::path(parentDir).parent_path().generic_string();
                    upPath = AssetBundleRegistry::MakeVirtualEntryPath(bundlePath, parentDir, true);
                }
            }
        }else{
            upPath = currentDirectory.parent_path();
        }
        pushEntry(entries, upPath, true, true);
    }

    std::filesystem::path currentBundlePath;
    std::string currentBundleEntryPath;
    bool currentIsVirtual = AssetBundleRegistry::DecodeVirtualEntryPath(currentDirectory, currentBundlePath, currentBundleEntryPath);
    if(currentIsVirtual){
        std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(currentBundlePath);
        if(bundle){
            std::string normalizedCurrent = currentBundleEntryPath;
            if(!normalizedCurrent.empty() && normalizedCurrent.back() == '/'){
                normalizedCurrent.pop_back();
            }

            for(const auto& manifestEntry : bundle->getEntries()){
                const bool isDirectory = (manifestEntry.kind == "directory");
                std::string displayPath = manifestEntry.path;
                if(isDirectory && !displayPath.empty() && displayPath.back() == '/'){
                    displayPath.pop_back();
                }
                if(displayPath.empty()){
                    continue;
                }

                const std::string parentDir = std::filesystem::path(displayPath).parent_path().generic_string();
                if(normalizedCurrent.empty()){
                    if(!parentDir.empty() && parentDir != "."){
                        continue;
                    }
                }else if(parentDir != normalizedCurrent){
                    continue;
                }

                pushEntry(
                    entries,
                    AssetBundleRegistry::MakeVirtualEntryPath(currentBundlePath, manifestEntry.path, isDirectory),
                    isDirectory,
                    false
                );
            }
        }
    }else{
        std::error_code ec;
        for(const auto& dirEntry : std::filesystem::directory_iterator(
                currentDirectory,
                std::filesystem::directory_options::skip_permission_denied,
                ec)){
            if(ec){
                break;
            }

            const std::filesystem::path path = dirEntry.path().lexically_normal();
            bool isDirectory = dirEntry.is_directory(ec);
            if(ec){
                ec.clear();
                continue;
            }

            if(isDirectory){
                pushEntry(entries, path, true, false);
                continue;
            }
            pushEntry(entries, path, false, false);
        }
    }

    std::sort(entries.begin(), entries.end(), [](const BrowserEntry& a, const BrowserEntry& b){
        if(a.isUp != b.isUp){
            return a.isUp;
        }
        if(a.tx.isDirectory != b.tx.isDirectory){
            return a.tx.isDirectory > b.tx.isDirectory;
        }
        const std::string aLabel = a.isUp ? std::string("..") : a.path.filename().string();
        const std::string bLabel = b.isUp ? std::string("..") : b.path.filename().string();
        return StringUtils::ToLowerCase(aLabel) < StringUtils::ToLowerCase(bLabel);
    });

    ImGui::Separator();
    ImGui::BeginChild(childId, ImVec2(0.0f, footerReserve > 0.0f ? -footerReserve : 0.0f), true);

    bool anyEntryHovered = false;
    if(entries.empty()){
        ImGui::TextDisabled("Directory empty.");
    }else{
        const float effectiveTileSize = std::clamp(tileSize, 56.0f, 128.0f);
        const float cellWidth = effectiveTileSize + 20.0f;
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellWidth));

        std::string tableId = std::string(childId) + "_table";
        if(ImGui::BeginTable(tableId.c_str(),
                             columns,
                             ImGuiTableFlags_SizingFixedFit |
                                 ImGuiTableFlags_NoPadOuterX |
                                 ImGuiTableFlags_NoPadInnerX)){
            for(size_t index = 0; index < entries.size(); ++index){
                const BrowserEntry& entry = entries[index];
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(index));

                const bool selected = (selectedPath == entry.path);
                bool doubleClicked = false;
                bool clicked = DrawAssetTile("asset_browser_tile", entry.tx, effectiveTileSize, selected, &doubleClicked);
                const ImVec2 tileMin = ImGui::GetItemRectMin();
                const ImVec2 tileMax = ImGui::GetItemRectMax();
                const bool tileHovered = ImGui::IsMouseHoveringRect(tileMin, tileMax, false);
                const bool tileRightClicked = tileHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right);

                std::string label = entry.isUp ? std::string("..") : entry.path.filename().string();
                ImGui::TextWrapped("%s", label.c_str());
                const bool labelHovered = ImGui::IsItemHovered();
                const bool labelClicked = labelHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                const bool labelDoubleClicked = labelHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                const bool labelRightClicked = labelHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right);

                anyEntryHovered = anyEntryHovered || tileHovered || labelHovered;
                clicked = clicked || labelClicked;
                doubleClicked = doubleClicked || labelDoubleClicked;

                if(tileRightClicked || labelRightClicked){
                    if(selectedPath != entry.path){
                        selectedPath = entry.path;
                        result.selectionChanged = true;
                    }
                    result.itemContextRequested = true;
                    clicked = false;
                    doubleClicked = false;
                }

                if(clicked && selectedPath != entry.path){
                    selectedPath = entry.path;
                    result.selectionChanged = true;
                }

                if(doubleClicked){
                    if(entry.isUp){
                        goUp();
                        result.selectionChanged = true;
                    }else if(entry.opensContainer){
                        if(AssetBundle::IsBundlePath(entry.path) && !AssetBundleRegistry::IsVirtualEntryPath(entry.path)){
                            currentDirectory = AssetBundleRegistry::MakeVirtualEntryPath(entry.path, "", true);
                        }else{
                            currentDirectory = entry.path;
                        }
                        selectedPath.clear();
                        result.selectionChanged = true;
                    }else if(entry.canSelect){
                        result.selectionActivated = true;
                    }
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
       ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
       !anyEntryHovered){
        if(!selectedPath.empty()){
            selectedPath.clear();
            result.selectionChanged = true;
        }
    }
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
       ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
       !anyEntryHovered){
        result.backgroundContextRequested = true;
    }

    ImGui::EndChild();
    return result;
}

const std::filesystem::path& AssetBrowserWidget::getAssetRoot() const{
    return assetRoot;
}

const std::filesystem::path& AssetBrowserWidget::getCurrentDirectory() const{
    return currentDirectory;
}

const std::filesystem::path& AssetBrowserWidget::getSelectedPath() const{
    return selectedPath;
}

bool AssetBrowserWidget::isBrowsingBundle() const{
    return AssetBundleRegistry::IsVirtualEntryPath(currentDirectory);
}

bool AssetBrowserWidget::hasSelectedAsset() const{
    AssetTransaction tx{};
    return tryGetSelectedTransaction(tx);
}

bool AssetBrowserWidget::tryGetSelectedTransaction(AssetTransaction& out) const{
    out = AssetTransaction{};
    if(selectedPath.empty()){
        return false;
    }

    bool isDirectory = false;
    if(!AssetDescriptorUtils::PathExists(selectedPath, &isDirectory) || isDirectory){
        return false;
    }
    if(!BuildTransaction(selectedPath, assetRoot.empty() ? getAssetRootPathForPicker() : assetRoot, out)){
        return false;
    }

    return IsKindCompatibleAny(out.kind, requestedKinds.data(), requestedKinds.size());
}

bool AssetBrowserWidget::tryGetSelectedReference(PickedAssetRef& out) const{
    AssetTransaction tx{};
    if(!tryGetSelectedTransaction(tx)){
        out = PickedAssetRef{};
        return false;
    }
    return ResolvePickedAssetRef(tx, out);
}

bool BuildTransaction(const std::filesystem::path& absolutePath, const std::filesystem::path& assetRoot, AssetTransaction& out){
    // Workspace/grid drawing calls this for visible entries every frame. Avoid
    // weakly_canonical() here because it performs filesystem I/O on Windows.
    std::filesystem::path normalized = absolutePath.lexically_normal();
    std::filesystem::path normalizedRoot = assetRoot.lexically_normal();
    std::error_code ec;

    std::filesystem::path bundlePath;
    std::string entryPath;
    bool isBundleDirectory = false;
    if(AssetBundleRegistry::DecodeVirtualEntryPath(normalized, bundlePath, entryPath, &isBundleDirectory)){
        std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
        if(!bundle){
            return false;
        }

        std::filesystem::path entryDisplayPath;
        if(!entryPath.empty()){
            std::string displayPath = entryPath;
            if(isBundleDirectory && !displayPath.empty() && displayPath.back() == '/'){
                displayPath.pop_back();
            }
            entryDisplayPath = std::filesystem::path(displayPath);
        }

        out.absolutePath = normalized;
        out.kind = ClassifyPath(entryDisplayPath, isBundleDirectory);
        out.isDirectory = isBundleDirectory;
        out.extension = toLower(entryDisplayPath.extension().string());
        out.assetRef = bundle->aliasToken();
        if(!entryPath.empty()){
            std::string normalizedEntry = entryPath;
            if(!normalizedEntry.empty() && normalizedEntry.back() == '/'){
                normalizedEntry.pop_back();
            }
            out.assetRef += "/" + normalizedEntry;
        }
        return true;
    }

    std::filesystem::path rel = normalized.lexically_relative(normalizedRoot);
    if(rel.empty() || rel.string().find("..") == 0){
        return false;
    }

    bool isDir = std::filesystem::is_directory(normalized, ec);
    if(ec){
        isDir = false;
    }
    out.absolutePath = normalized;
    out.kind = ClassifyPath(normalized, isDir);
    out.isDirectory = isDir;
    out.extension = toLower(normalized.extension().string());
    std::string relStr = rel.generic_string();
    out.assetRef = std::string(ASSET_DELIMITER) + "/" + relStr;
    return true;
}

void InvalidateAllThumbnails(){
    g_imageThumbnailCache.clear();
    g_materialThumbnailCache.clear();
    g_lastThumbnailCachePruneFrame = -100000;
}

void InvalidateMaterialThumbnail(const std::string& assetRef){
    if(assetRef.empty()){
        g_materialThumbnailCache.clear();
        return;
    }
    g_materialThumbnailCache.erase(assetRef);
}

bool DrawAssetTile(const char* id, const AssetTransaction& tx, float iconSize, bool selected, bool* outDoubleClicked){
    if(outDoubleClicked){
        *outDoubleClicked = false;
    }

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 size(iconSize, iconSize);
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if(outDoubleClicked){
        *outDoubleClicked = doubleClicked;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 base = kindColor(tx.kind);
    ImU32 frame = hovered ? IM_COL32(248, 248, 248, 255) : IM_COL32(44, 44, 44, 255);
    if(selected){
        frame = IM_COL32(90, 160, 255, 255);
    }

    drawList->AddRectFilled(cursor, ImVec2(cursor.x + size.x, cursor.y + size.y), IM_COL32(18, 18, 24, 255), 6.0f);
    drawList->AddRect(cursor, ImVec2(cursor.x + size.x, cursor.y + size.y), frame, 6.0f, 0, 2.0f);

    const bool hasThumbnailPreviewKind =
        (tx.kind == AssetKind::Image ||
         tx.kind == AssetKind::MaterialAsset ||
         tx.kind == AssetKind::Material);
    const float footerReserve = hasThumbnailPreviewKind ? 0.0f : 16.0f;
    float inset = 8.0f;
    ImVec2 innerMin(cursor.x + inset, cursor.y + inset);
    ImVec2 innerMax(cursor.x + size.x - inset, cursor.y + size.y - inset - footerReserve);
    float innerWidth = Math3D::Max(0.0f, innerMax.x - innerMin.x);
    float innerHeight = Math3D::Max(0.0f, innerMax.y - innerMin.y);
    float squareSide = Math3D::Min(innerWidth, innerHeight);
    ImVec2 squareMin(
        innerMin.x + ((innerWidth - squareSide) * 0.5f),
        innerMin.y + ((innerHeight - squareSide) * 0.5f)
    );
    ImVec2 squareMax(squareMin.x + squareSide, squareMin.y + squareSide);
    bool drewThumbnail = false;
    if(tx.kind == AssetKind::Image){
        std::shared_ptr<Texture> thumb = getImageThumbnail(tx);
        if(thumb && thumb->getID() != 0){
            drawList->AddRectFilled(squareMin, squareMax, IM_COL32(20, 20, 20, 255), 4.0f);
            drawCenteredTextureInSquare(drawList, thumb, squareMin, squareSide, true);
            drewThumbnail = true;
        }
    }else if(tx.kind == AssetKind::MaterialAsset || tx.kind == AssetKind::Material){
        std::shared_ptr<Texture> thumb = getMaterialThumbnail(tx);
        if(thumb && thumb->getID() != 0){
            drawCenteredTextureInSquare(drawList, thumb, squareMin, squareSide, true);
            drewThumbnail = true;
        }
    }

    if(!drewThumbnail){
        drawList->AddRectFilled(innerMin, innerMax, base, 4.0f);

        const char* glyph = kindGlyph(tx.kind);
        ImVec2 textSz = ImGui::CalcTextSize(glyph);
        ImVec2 textPos(
            innerMin.x + (innerMax.x - innerMin.x - textSz.x) * 0.5f,
            innerMin.y + (innerMax.y - innerMin.y - textSz.y) * 0.5f
        );
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), glyph);
    }

    if(hovered){
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tx.absolutePath.filename().string().c_str());
        ImGui::TextDisabled("%s", tx.assetRef.c_str());
        ImGui::EndTooltip();
    }

    return clicked;
}

void BeginAssetDragSource(const AssetTransaction& tx){
    if(!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)){
        return;
    }

    AssetDragPayload payload;
    payload.kind = static_cast<int>(tx.kind);
    payload.isDirectory = tx.isDirectory ? 1 : 0;
    copyStringFixed(payload.absolutePath, sizeof(payload.absolutePath), tx.absolutePath.generic_string());
    copyStringFixed(payload.assetRef, sizeof(payload.assetRef), tx.assetRef);

    ImGui::SetDragDropPayload(kAssetPayloadType, &payload, sizeof(payload));
    ImGui::TextUnformatted(tx.absolutePath.filename().string().c_str());
    ImGui::TextDisabled("%s", tx.assetRef.c_str());
    ImGui::EndDragDropSource();
}

bool AcceptAssetDrop(AssetKind requestedKind, AssetTransaction& out, bool acceptBeforeDelivery, bool* outIsDelivery){
    return AcceptAssetDrop(&requestedKind, 1, out, acceptBeforeDelivery, outIsDelivery);
}

bool AcceptAssetDropInCurrentTarget(AssetKind requestedKind, AssetTransaction& out, bool acceptBeforeDelivery, bool* outIsDelivery){
    return AcceptAssetDropInCurrentTarget(&requestedKind, 1, out, acceptBeforeDelivery, outIsDelivery);
}

bool AcceptAssetDropInCurrentTarget(const AssetKind* requestedKinds,
                                    size_t requestedKindCount,
                                    AssetTransaction& out,
                                    bool acceptBeforeDelivery,
                                    bool* outIsDelivery){
    if(outIsDelivery){
        *outIsDelivery = false;
    }

    ImGuiDragDropFlags flags = 0;
    if(acceptBeforeDelivery){
        flags |= ImGuiDragDropFlags_AcceptBeforeDelivery;
    }

    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayloadType, flags);
    if(!(payload && payload->Data && payload->DataSize == sizeof(AssetDragPayload))){
        return false;
    }

    const AssetDragPayload* data = static_cast<const AssetDragPayload*>(payload->Data);
    AssetKind kind = static_cast<AssetKind>(data->kind);
    if(!IsKindCompatibleAny(kind, requestedKinds, requestedKindCount)){
        return false;
    }

    out.kind = kind;
    out.isDirectory = (data->isDirectory != 0);
    out.absolutePath = std::filesystem::path(data->absolutePath);
    out.assetRef = data->assetRef;
    out.extension = toLower(out.absolutePath.extension().string());
    if(outIsDelivery){
        *outIsDelivery = payload->IsDelivery();
    }
    return true;
}

bool AcceptAssetDrop(const AssetKind* requestedKinds,
                     size_t requestedKindCount,
                     AssetTransaction& out,
                     bool acceptBeforeDelivery,
                     bool* outIsDelivery){
    if(outIsDelivery){
        *outIsDelivery = false;
    }
    if(!ImGui::BeginDragDropTarget()){
        return false;
    }

    const bool accepted = AcceptAssetDropInCurrentTarget(
        requestedKinds,
        requestedKindCount,
        out,
        acceptBeforeDelivery,
        outIsDelivery
    );
    ImGui::EndDragDropTarget();
    return accepted;
}

bool DrawAssetDropInput(const char* label,
                        char* buffer,
                        size_t bufferSize,
                        AssetKind requestedKind,
                        bool readOnly,
                        bool* outDropped,
                        bool* outCommitted){
    if(outDropped){
        *outDropped = false;
    }
    if(outCommitted){
        *outCommitted = false;
    }

    ImGuiID ownerId = ImGui::GetID(label);
    ImGuiInputTextFlags flags = readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float browseButtonWidth = ImGui::CalcTextSize("...").x + (style.FramePadding.x * 2.0f);
    const float reservedWidth = browseButtonWidth + style.ItemInnerSpacing.x;
    EditorPropertyUI::DrawLabel(label);
    ImGui::PushItemWidth(EditorPropertyUI::GetFieldWidth(reservedWidth));
    bool changed = ImGui::InputText(EditorPropertyUI::HiddenLabel(label).c_str(), buffer, bufferSize, flags);
    ImGui::PopItemWidth();
    const bool inputHovered = ImGui::IsItemHovered();
    const bool inputCommitted = !readOnly && ImGui::IsItemDeactivatedAfterEdit();
    if(inputCommitted && outCommitted){
        *outCommitted = true;
    }

    AssetTransaction tx;
    if(AcceptAssetDrop(requestedKind, tx)){
        std::memset(buffer, 0, bufferSize);
        std::strncpy(buffer, tx.assetRef.c_str(), bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        changed = true;
        if(outDropped){
            *outDropped = true;
        }
        if(outCommitted){
            *outCommitted = true;
        }
    }

    if(inputHovered){
        ImGui::SetTooltip("Drop %s asset here", (requestedKind == AssetKind::Any) ? "any" : kindGlyph(requestedKind));
    }

    bool requestPicker = false;
    if(inputHovered){
        if(readOnly && ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
            requestPicker = true;
        }else if(!readOnly && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){
            requestPicker = true;
        }
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    std::string browseButtonId = StringUtils::Format("...##asset_picker_btn_%u", static_cast<unsigned int>(ownerId));
    if(ImGui::Button(browseButtonId.c_str())){
        requestPicker = true;
    }
    if(ImGui::IsItemHovered()){
        ImGui::SetTooltip("Browse assets");
    }

    if(requestPicker){
        requestAssetPicker(ownerId, &requestedKind, 1, std::string(buffer));
    }
    drawAssetPickerWindow();

    std::string selectedAssetRef;
    if(consumeAssetPickerSelection(ownerId, selectedAssetRef)){
        std::memset(buffer, 0, bufferSize);
        std::strncpy(buffer, selectedAssetRef.c_str(), bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        changed = true;
        if(outDropped){
            *outDropped = true;
        }
        if(outCommitted){
            *outCommitted = true;
        }
    }

    return changed;
}

bool DrawAssetDropInput(const char* label,
                        std::string& value,
                        const AssetKind* requestedKinds,
                        size_t requestedKindCount,
                        bool readOnly,
                        bool* outDropped,
                        bool* outCommitted){
    if(outDropped){
        *outDropped = false;
    }
    if(outCommitted){
        *outCommitted = false;
    }

    ImGuiID ownerId = ImGui::GetID(label);
    const size_t minCapacity = 256;
    const size_t maxCapacity = 2048;
    size_t capacity = value.size() + 64;
    if(capacity < minCapacity){
        capacity = minCapacity;
    }else if(capacity > maxCapacity){
        capacity = maxCapacity;
    }

    std::vector<char> buffer(capacity, 0);
    if(!value.empty()){
        std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
        buffer.back() = '\0';
    }

    ImGuiInputTextFlags flags = readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float browseButtonWidth = ImGui::CalcTextSize("...").x + (style.FramePadding.x * 2.0f);
    const float reservedWidth = browseButtonWidth + style.ItemInnerSpacing.x;
    EditorPropertyUI::DrawLabel(label);
    ImGui::PushItemWidth(EditorPropertyUI::GetFieldWidth(reservedWidth));
    bool changed = ImGui::InputText(EditorPropertyUI::HiddenLabel(label).c_str(), buffer.data(), buffer.size(), flags);
    ImGui::PopItemWidth();
    const bool inputHovered = ImGui::IsItemHovered();
    const bool inputCommitted = !readOnly && ImGui::IsItemDeactivatedAfterEdit();
    if(changed){
        value = buffer.data();
    }
    if(inputCommitted && outCommitted){
        *outCommitted = true;
    }

    AssetTransaction tx;
    if(AcceptAssetDrop(requestedKinds, requestedKindCount, tx)){
        value = tx.assetRef;
        changed = true;
        if(outDropped){
            *outDropped = true;
        }
        if(outCommitted){
            *outCommitted = true;
        }
    }

    if(inputHovered){
        const std::string wanted = requestedKindsTooltip(requestedKinds, requestedKindCount);
        ImGui::SetTooltip("Drop %s asset here", wanted.c_str());
    }

    bool requestPicker = false;
    if(inputHovered){
        if(readOnly && ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
            requestPicker = true;
        }else if(!readOnly && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){
            requestPicker = true;
        }
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    std::string browseButtonId = StringUtils::Format("...##asset_picker_btn_%u", static_cast<unsigned int>(ownerId));
    if(ImGui::Button(browseButtonId.c_str())){
        requestPicker = true;
    }
    if(ImGui::IsItemHovered()){
        ImGui::SetTooltip("Browse assets");
    }

    if(requestPicker){
        requestAssetPicker(ownerId, requestedKinds, requestedKindCount, value);
    }
    drawAssetPickerWindow();

    std::string selectedAssetRef;
    if(consumeAssetPickerSelection(ownerId, selectedAssetRef)){
        value = selectedAssetRef;
        changed = true;
        if(outDropped){
            *outDropped = true;
        }
        if(outCommitted){
            *outCommitted = true;
        }
    }

    return changed;
}

bool DrawAssetDropInput(const char* label,
                        std::string& value,
                        std::initializer_list<AssetKind> requestedKinds,
                        bool readOnly,
                        bool* outDropped,
                        bool* outCommitted){
    const AssetKind* requestedPtr = requestedKinds.size() > 0 ? requestedKinds.begin() : nullptr;
    return DrawAssetDropInput(label, value, requestedPtr, requestedKinds.size(), readOnly, outDropped, outCommitted);
}

} // namespace EditorAssetUI
