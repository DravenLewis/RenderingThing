#include "EditorAssetUI.h"
#include "Asset.h"
#include "Environment.h"
#include "File.h"
#include "FrameBuffer.h"
#include "Logbot.h"
#include "MaterialAsset.h"
#include "MaterialDefaults.h"
#include "ModelPartPrefabs.h"
#include "PBRMaterial.h"
#include "Screen.h"
#include "SkyBox.h"
#include "StringUtils.h"
#include "Texture.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {
    constexpr const char* kAssetPayloadType = "EDITOR_ASSET_DND";
    constexpr size_t kMaxPathChars = 512;

    struct AssetDragPayload {
        int kind = static_cast<int>(EditorAssetUI::AssetKind::Unknown);
        int isDirectory = 0;
        char absolutePath[kMaxPathChars] = {};
        char assetRef[kMaxPathChars] = {};
    };

    struct ThumbnailCacheEntry {
        std::shared_ptr<Texture> texture;
        std::filesystem::file_time_type writeTime{};
        bool hasWriteTime = false;
    };

    std::unordered_map<std::string, ThumbnailCacheEntry> g_imageThumbnailCache;

    struct MaterialThumbnailCacheEntry {
        std::shared_ptr<FrameBuffer> framebuffer;
        std::shared_ptr<Texture> texture;
        std::filesystem::file_time_type sourceWriteTime{};
        std::filesystem::file_time_type objectWriteTime{};
        bool hasSourceWriteTime = false;
        bool hasObjectWriteTime = false;
    };

    std::unordered_map<std::string, MaterialThumbnailCacheEntry> g_materialThumbnailCache;
    std::shared_ptr<ModelPart> g_materialThumbnailSphere;
    std::shared_ptr<Camera> g_materialThumbnailCamera;
    std::shared_ptr<Environment> g_materialThumbnailEnvironment;
    std::shared_ptr<SkyBox> g_materialThumbnailSkyBox;
    std::shared_ptr<Material> g_materialThumbnailFallbackMaterial;
    int g_materialThumbnailSize = 0;

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
            case AssetKind::ShaderVertex:   return "VTX";
            case AssetKind::ShaderFragment: return "FRG";
            case AssetKind::ShaderGrometry: return "GEO";
            case AssetKind::ShaderCompute:  return "CMP";
            case AssetKind::ShaderTesselation:return "TES";
            case AssetKind::ShaderTask:     return "TSK";
            case AssetKind::ShaderRaytrace: return "RTX";
            case AssetKind::ShaderGeneric:  return "SHD";
            case AssetKind::ShaderAsset:    return "SAS";
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
            case AssetKind::ShaderVertex:   return IM_COL32(188, 112, 255, 255);
            case AssetKind::ShaderFragment: return IM_COL32(255, 116, 156, 255);
            case AssetKind::ShaderGrometry: return IM_COL32(255, 160, 98, 255);
            case AssetKind::ShaderCompute:  return IM_COL32(82, 204, 190, 255);
            case AssetKind::ShaderTesselation:return IM_COL32(196, 170, 255, 255);
            case AssetKind::ShaderTask:     return IM_COL32(122, 174, 255, 255);
            case AssetKind::ShaderRaytrace: return IM_COL32(255, 94, 94, 255);
            case AssetKind::ShaderGeneric:  return IM_COL32(166, 96, 242, 255);
            case AssetKind::ShaderAsset:    return IM_COL32(237, 132, 255, 255);
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
        if(requested == AssetKind::MaterialAsset){
            return offered == AssetKind::MaterialAsset;
        }
        if(requested == AssetKind::Material){
            return offered == AssetKind::Material || offered == AssetKind::MaterialAsset;
        }
        return false;
    }

    std::shared_ptr<Texture> getImageThumbnail(const EditorAssetUI::AssetTransaction& tx){
        try{
            if(tx.kind != EditorAssetUI::AssetKind::Image || tx.assetRef.empty()){
                return nullptr;
            }

            std::error_code ec;
            bool hasWriteTime = false;
            std::filesystem::file_time_type writeTime{};
            if(!tx.absolutePath.empty() && std::filesystem::exists(tx.absolutePath, ec)){
                writeTime = std::filesystem::last_write_time(tx.absolutePath, ec);
                hasWriteTime = !ec;
            }

            auto cachedIt = g_imageThumbnailCache.find(tx.assetRef);
            if(cachedIt != g_imageThumbnailCache.end()){
                const ThumbnailCacheEntry& cached = cachedIt->second;
                if(cached.texture){
                    bool cacheValid = false;
                    if(!hasWriteTime && !cached.hasWriteTime){
                        cacheValid = true;
                    }else if(hasWriteTime && cached.hasWriteTime && cached.writeTime == writeTime){
                        cacheValid = true;
                    }
                    if(cacheValid){
                        return cached.texture;
                    }
                }
            }

            auto asset = AssetManager::Instance.getOrLoad(tx.assetRef);
            if(!asset){
                return nullptr;
            }
            auto texture = Texture::Load(asset);
            if(!texture || texture->getID() == 0){
                return nullptr;
            }

            ThumbnailCacheEntry entry;
            entry.texture = texture;
            entry.writeTime = writeTime;
            entry.hasWriteTime = hasWriteTime;
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
        if(assetRef.empty()){
            return false;
        }
        if(StringUtils::BeginsWith(assetRef, ASSET_DELIMITER)){
            std::string rel = assetRef.substr(std::strlen(ASSET_DELIMITER));
            if(!rel.empty() && (rel[0] == '/' || rel[0] == '\\')){
                rel.erase(rel.begin());
            }
            outPath = std::filesystem::path(File::GetCWD()) / "res" / rel;
            return true;
        }
        outPath = std::filesystem::path(assetRef);
        return true;
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

            auto it = g_materialThumbnailCache.find(tx.assetRef);
            if(it != g_materialThumbnailCache.end()){
                const MaterialThumbnailCacheEntry& cached = it->second;
                bool sourceMatches = (hasSourceWriteTime == cached.hasSourceWriteTime) &&
                                     (!hasSourceWriteTime || cached.sourceWriteTime == sourceWriteTime);
                bool objectMatches = (hasObjectWriteTime == cached.hasObjectWriteTime) &&
                                     (!hasObjectWriteTime || cached.objectWriteTime == objectWriteTime);
                if(cached.texture && sourceMatches && objectMatches){
                    return cached.texture;
                }
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
    if(endsWith(pathLower, ".material.asset")){
        return AssetKind::MaterialAsset;
    }
    if(endsWith(pathLower, ".mat.asset")){
        return AssetKind::MaterialAsset;
    }
    if(endsWith(pathLower, ".material")){
        return AssetKind::Material;
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

bool BuildTransaction(const std::filesystem::path& absolutePath, const std::filesystem::path& assetRoot, AssetTransaction& out){
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(absolutePath, ec);
    if(ec){
        normalized = absolutePath.lexically_normal();
    }
    std::filesystem::path normalizedRoot = std::filesystem::weakly_canonical(assetRoot, ec);
    if(ec){
        normalizedRoot = assetRoot.lexically_normal();
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
            drawCenteredTextureInSquare(drawList, thumb, squareMin, squareSide, false);
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

bool AcceptAssetDrop(AssetKind requestedKind, AssetTransaction& out){
    return AcceptAssetDrop(&requestedKind, 1, out);
}

bool AcceptAssetDrop(const AssetKind* requestedKinds, size_t requestedKindCount, AssetTransaction& out){
    if(!ImGui::BeginDragDropTarget()){
        return false;
    }

    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayloadType);
    bool accepted = false;
    if(payload && payload->Data && payload->DataSize == sizeof(AssetDragPayload)){
        const AssetDragPayload* data = static_cast<const AssetDragPayload*>(payload->Data);
        AssetKind kind = static_cast<AssetKind>(data->kind);
        if(IsKindCompatibleAny(kind, requestedKinds, requestedKindCount)){
            out.kind = kind;
            out.isDirectory = (data->isDirectory != 0);
            out.absolutePath = std::filesystem::path(data->absolutePath);
            out.assetRef = data->assetRef;
            out.extension = toLower(out.absolutePath.extension().string());
            accepted = true;
        }
    }

    ImGui::EndDragDropTarget();
    return accepted;
}

bool DrawAssetDropInput(const char* label, char* buffer, size_t bufferSize, AssetKind requestedKind, bool readOnly, bool* outDropped){
    if(outDropped){
        *outDropped = false;
    }

    ImGuiInputTextFlags flags = readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
    bool changed = ImGui::InputText(label, buffer, bufferSize, flags);

    AssetTransaction tx;
    if(AcceptAssetDrop(requestedKind, tx)){
        std::memset(buffer, 0, bufferSize);
        std::strncpy(buffer, tx.assetRef.c_str(), bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        changed = true;
        if(outDropped){
            *outDropped = true;
        }
    }

    if(ImGui::IsItemHovered()){
        ImGui::SetTooltip("Drop %s asset here", (requestedKind == AssetKind::Any) ? "any" : kindGlyph(requestedKind));
    }

    return changed;
}

bool DrawAssetDropInput(const char* label, std::string& value, const AssetKind* requestedKinds, size_t requestedKindCount, bool readOnly, bool* outDropped){
    if(outDropped){
        *outDropped = false;
    }

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
    bool changed = ImGui::InputText(label, buffer.data(), buffer.size(), flags);
    if(changed){
        value = buffer.data();
    }

    AssetTransaction tx;
    if(AcceptAssetDrop(requestedKinds, requestedKindCount, tx)){
        value = tx.assetRef;
        changed = true;
        if(outDropped){
            *outDropped = true;
        }
    }

    if(ImGui::IsItemHovered()){
        const std::string wanted = requestedKindsTooltip(requestedKinds, requestedKindCount);
        ImGui::SetTooltip("Drop %s asset here", wanted.c_str());
    }

    return changed;
}

bool DrawAssetDropInput(const char* label, std::string& value, std::initializer_list<AssetKind> requestedKinds, bool readOnly, bool* outDropped){
    const AssetKind* requestedPtr = requestedKinds.size() > 0 ? requestedKinds.begin() : nullptr;
    return DrawAssetDropInput(label, value, requestedPtr, requestedKinds.size(), readOnly, outDropped);
}

} // namespace EditorAssetUI
