/**
 * @file src/Editor/Widgets/FilePreviewWidget.h
 * @brief Declarations for FilePreviewWidget.
 */

#ifndef FILE_PREVIEW_WIDGET_H
#define FILE_PREVIEW_WIDGET_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Assets/Descriptors/EffectAsset.h"
#include "Assets/Descriptors/ImageAsset.h"
#include "Assets/Descriptors/MaterialAsset.h"
#include "Assets/Descriptors/LensFlareAsset.h"
#include "Assets/Descriptors/ModelAsset.h"
#include "Assets/Descriptors/SkyboxAsset.h"
#include "Assets/Importers/MtlMaterialImporter.h"
#include "Assets/Descriptors/ShaderAsset.h"
#include "Assets/Bundles/AssetBundle.h"

struct FrameBuffer;
class Texture;
struct ModelPart;
struct Model;
struct Camera;
class Material;
class Environment;
class SkyBox;

/// @brief Represents the FilePreviewWidget type.
class FilePreviewWidget {
    public:
        /**
         * @brief Sets the asset root.
         * @param rootPath Filesystem path for root path.
         */
        void setAssetRoot(const std::filesystem::path& rootPath);
        /**
         * @brief Sets the file path.
         * @param path Filesystem path for path.
         */
        void setFilePath(const std::filesystem::path& path);
        /**
         * @brief Draws this object.
         */
        void draw();

    private:
        /**
         * @brief Reloads from disk.
         * @param force Flag controlling force.
         */
        void reloadFromDisk(bool force = false);
        /**
         * @brief Draws bundle asset editor.
         */
        void drawBundleAssetEditor();
        /**
         * @brief Draws shader asset editor.
         */
        void drawShaderAssetEditor();
        /**
         * @brief Draws skybox asset editor.
         */
        void drawSkyboxAssetEditor();
        /**
         * @brief Draws lens flare asset editor.
         */
        void drawLensFlareAssetEditor();
        /**
         * @brief Draws effect asset editor.
         */
        void drawEffectAssetEditor();
        /**
         * @brief Draws material asset editor.
         */
        void drawMaterialAssetEditor();
        /**
         * @brief Draws material object editor.
         */
        void drawMaterialObjectEditor();
        /**
         * @brief Draws model asset editor.
         */
        void drawModelAssetEditor();
        /**
         * @brief Draws mtl file preview.
         */
        void drawMtlFilePreview();
        /**
         * @brief Draws model file preview.
         */
        void drawModelFilePreview();
        /**
         * @brief Draws image file preview.
         */
        void drawImageFilePreview();
        /**
         * @brief Draws image asset editor.
         */
        void drawImageAssetEditor();
        /**
         * @brief Draws generic info.
         */
        void drawGenericInfo() const;
        /**
         * @brief Draws error byte dump if needed.
         */
        void drawErrorByteDumpIfNeeded();
        /**
         * @brief Refreshes error byte dump.
         */
        void refreshErrorByteDump();
        /**
         * @brief Ensures material preview resources.
         * @param size Number of elements or bytes.
         */
        void ensureMaterialPreviewResources(int size);
        /**
         * @brief Updates preview camera from orbit.
         */
        void updatePreviewCameraFromOrbit();
        /**
         * @brief Checks whether handle preview orbit input.
         * @return True when the operation succeeds; otherwise false.
         */
        bool handlePreviewOrbitInput();
        /**
         * @brief Renders material preview.
         * @param material Value for material.
         */
        void renderMaterialPreview(const std::shared_ptr<Material>& material);
        /**
         * @brief Renders model preview.
         * @param model Mode or type selector.
         */
        void renderModelPreview(const std::shared_ptr<Model>& model);
        /**
         * @brief Checks whether import selected mtl material.
         * @return True when the operation succeeds; otherwise false.
         */
        bool importSelectedMtlMaterial();
        /**
         * @brief Checks whether import current model as asset.
         * @return True when the operation succeeds; otherwise false.
         */
        bool importCurrentModelAsAsset();
        /**
         * @brief Checks whether import current image as asset.
         * @return True when the operation succeeds; otherwise false.
         */
        bool importCurrentImageAsAsset();

        std::filesystem::path assetRoot;
        std::filesystem::path filePath;
        std::filesystem::file_time_type lastWriteTime{};
        int lastExternalWriteTimeValidationFrame = -100000;
        bool hasLoadedData = false;
        bool isBundleAssetFile = false;
        bool isShaderAssetFile = false;
        bool isSkyboxAssetFile = false;
        bool isLensFlareAssetFile = false;
        bool isEffectAssetFile = false;
        bool isMaterialAssetFile = false;
        bool isMaterialObjectFile = false;
        bool isModelAssetFile = false;
        bool isImageAssetFile = false;
        bool isMtlFile = false;
        bool isModelFile = false;
        bool isImageFile = false;
        bool statusIsError = false;
        std::string statusMessage;
        std::string errorByteDump;
        std::filesystem::path errorByteDumpPath;
        std::filesystem::file_time_type errorByteDumpWriteTime{};
        char cacheName[128] = {};
        std::shared_ptr<AssetBundle> bundleAsset;
        char bundleAlias[128] = {};
        char bundleRootEntry[256] = {};
        char bundleAddEntryPath[256] = {};
        char bundleAddSourceRef[256] = {};
        bool bundleAssetSavePending = false;

        ShaderAssetData bundledShaderData;
        SkyboxAssetData skyboxData;
        char skyboxName[128] = {};
        char skyboxRightFace[256] = {};
        char skyboxLeftFace[256] = {};
        char skyboxTopFace[256] = {};
        char skyboxBottomFace[256] = {};
        char skyboxFrontFace[256] = {};
        char skyboxBackFace[256] = {};
        LensFlareAssetData lensFlareData;
        char lensFlareName[128] = {};
        char lensFlareTexture[256] = {};
        EffectAssetData effectAssetData;
        char effectAssetName[128] = {};
        char effectAssetVertex[256] = {};
        char effectAssetFragment[256] = {};

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
        ImageAssetData imageAssetData;
        char imageAssetName[128] = {};
        char imageAssetSource[256] = {};
        bool imageAssetSavePending = false;
        bool imageImportPopupOpen = false;
        ImageAssetData imageImportData;
        char imageImportAssetPath[256] = {};
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
        bool skyboxAssetSavePending = false;
        bool lensFlareAssetSavePending = false;
        bool effectAssetSavePending = false;
        bool materialAssetSavePending = false;
        bool previewMaterialDirty = true;
        bool previewModelDirty = true;
        int previewSize = 196;
        float previewOrbitYaw = 45.0f;
        float previewOrbitPitch = 22.5f;
        float previewOrbitDistance = 3.55f;
};

#endif // FILE_PREVIEW_WIDGET_H
