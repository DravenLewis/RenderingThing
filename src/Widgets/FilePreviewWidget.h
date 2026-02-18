#ifndef FILE_PREVIEW_WIDGET_H
#define FILE_PREVIEW_WIDGET_H

#include <filesystem>
#include <memory>
#include <string>

#include "MaterialAsset.h"

struct FrameBuffer;
class Texture;
struct ModelPart;
struct Camera;
class Material;
class Environment;
class SkyBox;

class FilePreviewWidget {
    public:
        void setAssetRoot(const std::filesystem::path& rootPath);
        void setFilePath(const std::filesystem::path& path);
        void draw();

    private:
        void reloadFromDisk(bool force = false);
        void drawShaderAssetEditor();
        void drawMaterialAssetEditor();
        void drawGenericInfo() const;
        void ensureMaterialPreviewResources(int size);
        void renderMaterialPreview(const std::shared_ptr<Material>& material);

        std::filesystem::path assetRoot;
        std::filesystem::path filePath;
        std::filesystem::file_time_type lastWriteTime{};
        bool hasLoadedData = false;
        bool isShaderAssetFile = false;
        bool isMaterialAssetFile = false;
        bool statusIsError = false;
        std::string statusMessage;
        char cacheName[128] = {};
        char vertexAsset[256] = {};
        char fragmentAsset[256] = {};
        MaterialAssetData materialData;
        char materialName[128] = {};
        char materialShaderAsset[256] = {};
        char materialTexture[256] = {};
        char materialBaseColorTex[256] = {};
        char materialMetalRoughTex[256] = {};
        char materialNormalTex[256] = {};
        char materialEmissiveTex[256] = {};
        char materialOcclusionTex[256] = {};

        std::shared_ptr<FrameBuffer> previewFrameBuffer;
        std::shared_ptr<Texture> previewTexture;
        std::shared_ptr<ModelPart> previewCube;
        std::shared_ptr<Camera> previewCamera;
        std::shared_ptr<Environment> previewEnvironment;
        std::shared_ptr<SkyBox> previewSkyBox;
        std::shared_ptr<Material> previewMaterial;
        bool previewMaterialDirty = true;
        int previewSize = 196;
};

#endif // FILE_PREVIEW_WIDGET_H
