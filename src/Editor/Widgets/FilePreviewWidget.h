#ifndef FILE_PREVIEW_WIDGET_H
#define FILE_PREVIEW_WIDGET_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Assets/Descriptors/MaterialAsset.h"
#include "Assets/Descriptors/ModelAsset.h"
#include "Assets/Importers/MtlMaterialImporter.h"
#include "Assets/Descriptors/ShaderAsset.h"

struct FrameBuffer;
class Texture;
struct ModelPart;
struct Model;
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
        void drawMaterialObjectEditor();
        void drawModelAssetEditor();
        void drawMtlFilePreview();
        void drawModelFilePreview();
        void drawGenericInfo() const;
        void drawErrorByteDumpIfNeeded();
        void refreshErrorByteDump();
        void ensureMaterialPreviewResources(int size);
        void updatePreviewCameraFromOrbit();
        bool handlePreviewOrbitInput();
        void renderMaterialPreview(const std::shared_ptr<Material>& material);
        void renderModelPreview(const std::shared_ptr<Model>& model);
        bool importSelectedMtlMaterial();
        bool importCurrentModelAsAsset();

        std::filesystem::path assetRoot;
        std::filesystem::path filePath;
        std::filesystem::file_time_type lastWriteTime{};
        bool hasLoadedData = false;
        bool isShaderAssetFile = false;
        bool isMaterialAssetFile = false;
        bool isMaterialObjectFile = false;
        bool isModelAssetFile = false;
        bool isMtlFile = false;
        bool isModelFile = false;
        bool statusIsError = false;
        std::string statusMessage;
        std::string errorByteDump;
        std::filesystem::path errorByteDumpPath;
        std::filesystem::file_time_type errorByteDumpWriteTime{};
        char cacheName[128] = {};

        ShaderAssetData bundledShaderData;

        MaterialAssetData materialData;
        char materialName[128] = {};
        char materialShaderAsset[256] = {};
        char materialTexture[256] = {};
        char materialBaseColorTex[256] = {};
        char materialRoughnessTex[256] = {};
        char materialMetalRoughTex[256] = {};
        char materialNormalTex[256] = {};
        char materialHeightTex[256] = {};
        char materialEmissiveTex[256] = {};
        char materialOcclusionTex[256] = {};
        MaterialObjectData materialObjectData;
        char materialObjectName[128] = {};
        char materialObjectAssetRef[256] = {};
        ModelAssetData modelAssetData;
        char modelAssetName[128] = {};
        char modelAssetSource[256] = {};
        char modelAssetMaterialRef[256] = {};
        std::vector<MtlMaterialDefinition> mtlMaterials;
        int selectedMtlMaterialIndex = 0;

        std::shared_ptr<FrameBuffer> previewFrameBuffer;
        std::shared_ptr<Texture> previewTexture;
        std::shared_ptr<ModelPart> previewSphere;
        std::shared_ptr<Camera> previewCamera;
        std::shared_ptr<Environment> previewEnvironment;
        std::shared_ptr<SkyBox> previewSkyBox;
        std::shared_ptr<Material> previewMaterial;
        std::shared_ptr<Model> previewModel;
        bool materialAssetSavePending = false;
        bool previewMaterialDirty = true;
        bool previewModelDirty = true;
        int previewSize = 196;
        float previewOrbitYaw = 45.0f;
        float previewOrbitPitch = 22.5f;
        float previewOrbitDistance = 3.55f;
};

#endif // FILE_PREVIEW_WIDGET_H
