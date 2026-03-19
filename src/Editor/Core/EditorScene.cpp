/**
 * @file src/Editor/Core/EditorScene.cpp
 * @brief Implementation for EditorScene.
 */

#include "Editor/Core/EditorScene.h"

// EditorScene is an editor host/wrapper scene: it renders and edits a contained target scene.
// The editor viewport navigation camera is owned by EditorScene; target-scene cameras are
// preview/edit targets and can be designated as the target scene's runtime current camera.

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

#include "ECS/Core/ECSComponents.h"
#include "Foundation/IO/File.h"
#include "Editor/Core/EditorAssetUI.h"
#include "Editor/Core/ImGuiLayer.h"
#include "Editor/Widgets/BoundsEditState.h"
#include "Engine/Core/GameEngine.h"
#include "Foundation/Logging/Logbot.h"
#include "Foundation/Util/StringUtils.h"
#include "Platform/Window/RenderWindow.h"
#include "Assets/Core/Asset.h"
#include "Assets/Descriptors/SkyboxAsset.h"
#include "Scene/LoadedScene.h"
#include "Serialization/IO/EntitySnapshotIO.h"
#include "Serialization/IO/PrefabIO.h"
#include "Serialization/IO/SceneIO.h"
#include "Serialization/Schema/ComponentSerializationRegistry.h"
#include "Rendering/Lighting/ShadowRenderer.h"
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include "neoecs.hpp"
#include <cctype>
#include <cstdlib>
#include <system_error>

namespace {
    constexpr float kToolbarHeight = 64.0f;
    constexpr float kPanelGap = 4.0f;
    constexpr float kSplitterThickness = 6.0f;
    constexpr float kMinLeftPanelWidth = 180.0f;
    constexpr float kMinRightPanelWidth = 220.0f;
    constexpr float kMinCenterPanelWidth = 260.0f;
    constexpr float kMinBottomPanelHeight = 140.0f;
    constexpr float kMinTopPanelHeight = 180.0f;
    constexpr float kIoStatusDurationSeconds = 6.0f;
    constexpr float kHelperCenterPickRadiusPx = 18.0f;
    constexpr size_t kMaxEditHistoryEntries = 64;
    constexpr const char* kEditorSessionDocumentType = "editor_session";
    constexpr int kEditorSessionDocumentVersion = 1;

    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;

    bool endsWithIgnoreCase(const std::string& value, const std::string& suffix){
        return StringUtils::EndsWith(StringUtils::ToLowerCase(value), StringUtils::ToLowerCase(suffix));
    }

    std::string sanitizeFileStem(const std::string& value, const std::string& fallback){
        std::string out;
        out.reserve(value.size());
        for(char c : value){
            const unsigned char uc = static_cast<unsigned char>(c);
            if(std::isalnum(uc) || c == '_' || c == '-'){
                out.push_back(c);
            }else if(std::isspace(uc)){
                out.push_back('_');
            }
        }
        if(out.empty()){
            out = fallback;
        }
        return out;
    }

    bool isPathWithExtension(const std::filesystem::path& path, const std::string& extension){
        if(path.empty() || extension.empty()){
            return false;
        }
        return endsWithIgnoreCase(path.generic_string(), extension);
    }

    std::filesystem::path makeUniquePath(const std::filesystem::path& desiredPath){
        std::error_code ec;
        if(!std::filesystem::exists(desiredPath, ec)){
            return desiredPath;
        }

        const std::filesystem::path parent = desiredPath.parent_path();
        const std::string stem = desiredPath.stem().string();
        const std::string ext = desiredPath.extension().string();
        int suffix = 1;
        std::filesystem::path candidate;
        do{
            candidate = parent / (stem + "_" + std::to_string(suffix) + ext);
            suffix++;
        }while(std::filesystem::exists(candidate, ec));
        if(ec){
            return desiredPath;
        }
        return candidate;
    }

    void copyFixedString(char* dst, size_t dstSize, const std::string& value){
        if(!dst || dstSize == 0){
            return;
        }
        std::memset(dst, 0, dstSize);
        if(value.empty()){
            return;
        }
        std::strncpy(dst, value.c_str(), dstSize - 1);
        dst[dstSize - 1] = '\0';
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

    bool tryParseStableEntityId(const std::string& value, std::uint64_t& outId){
        if(value.empty()){
            return false;
        }
        for(char c : value){
            if(!std::isdigit(static_cast<unsigned char>(c))){
                return false;
            }
        }
        try{
            outId = static_cast<std::uint64_t>(std::stoull(value));
            return outId != 0;
        }catch(...){
            return false;
        }
    }

    std::uint64_t hashStableEntityId(const std::string& value){
        constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;
        std::uint64_t hash = kFnvOffset;
        for(unsigned char c : value){
            hash ^= static_cast<std::uint64_t>(c);
            hash *= kFnvPrime;
        }
        return (hash == 0) ? 1 : hash;
    }

    bool componentRecordListsEqual(const std::vector<JsonSchema::EntitySnapshotSchemaBase::ComponentRecord>& a,
                                   const std::vector<JsonSchema::EntitySnapshotSchemaBase::ComponentRecord>& b){
        if(a.size() != b.size()){
            return false;
        }
        for(size_t i = 0; i < a.size(); ++i){
            if(a[i].type != b[i].type ||
               a[i].version != b[i].version ||
               a[i].payloadJson != b[i].payloadJson){
                return false;
            }
        }
        return true;
    }

    const char* serializedComponentTypeName(const NeoECS::ECSComponent* component){
        if(dynamic_cast<const TransformComponent*>(component)){ return "TransformComponent"; }
        if(dynamic_cast<const EntityPropertiesComponent*>(component)){ return "EntityPropertiesComponent"; }
        if(dynamic_cast<const MeshRendererComponent*>(component)){ return "MeshRendererComponent"; }
        if(dynamic_cast<const LightComponent*>(component)){ return "LightComponent"; }
        if(dynamic_cast<const BoundsComponent*>(component)){ return "BoundsComponent"; }
        if(dynamic_cast<const ColliderComponent*>(component)){ return "ColliderComponent"; }
        if(dynamic_cast<const RigidBodyComponent*>(component)){ return "RigidBodyComponent"; }
        if(dynamic_cast<const CameraComponent*>(component)){ return "CameraComponent"; }
        if(dynamic_cast<const SkyboxComponent*>(component)){ return "SkyboxComponent"; }
        if(dynamic_cast<const EnvironmentComponent*>(component)){ return "EnvironmentComponent"; }
        if(dynamic_cast<const SSAOComponent*>(component)){ return "SSAOComponent"; }
        if(dynamic_cast<const DepthOfFieldComponent*>(component)){ return "DepthOfFieldComponent"; }
        if(dynamic_cast<const BloomComponent*>(component)){ return "BloomComponent"; }
        if(dynamic_cast<const LensFlareComponent*>(component)){ return "LensFlareComponent"; }
        if(dynamic_cast<const AutoExposureComponent*>(component)){ return "AutoExposureComponent"; }
        if(dynamic_cast<const AntiAliasingComponent*>(component)){ return "AntiAliasingComponent"; }
        if(dynamic_cast<const ScriptComponent*>(component)){ return "ScriptComponent"; }
        return nullptr;
    }

    bool removeSerializedComponentByType(NeoECS::ECSComponentManager* manager,
                                         NeoECS::ECSEntity* entity,
                                         const std::string& typeName){
        if(!manager || !entity){
            return false;
        }
        if(typeName == "TransformComponent" || typeName == "EntityPropertiesComponent"){
            return false;
        }
        if(typeName == "MeshRendererComponent"){ manager->removeECSComponent<MeshRendererComponent>(entity); return true; }
        if(typeName == "LightComponent"){ manager->removeECSComponent<LightComponent>(entity); return true; }
        if(typeName == "BoundsComponent"){ manager->removeECSComponent<BoundsComponent>(entity); return true; }
        if(typeName == "ColliderComponent"){ manager->removeECSComponent<ColliderComponent>(entity); return true; }
        if(typeName == "RigidBodyComponent"){ manager->removeECSComponent<RigidBodyComponent>(entity); return true; }
        if(typeName == "CameraComponent"){ manager->removeECSComponent<CameraComponent>(entity); return true; }
        if(typeName == "SkyboxComponent"){ manager->removeECSComponent<SkyboxComponent>(entity); return true; }
        if(typeName == "EnvironmentComponent"){ manager->removeECSComponent<EnvironmentComponent>(entity); return true; }
        if(typeName == "SSAOComponent"){ manager->removeECSComponent<SSAOComponent>(entity); return true; }
        if(typeName == "DepthOfFieldComponent"){ manager->removeECSComponent<DepthOfFieldComponent>(entity); return true; }
        if(typeName == "BloomComponent"){ manager->removeECSComponent<BloomComponent>(entity); return true; }
        if(typeName == "LensFlareComponent"){ manager->removeECSComponent<LensFlareComponent>(entity); return true; }
        if(typeName == "AutoExposureComponent"){ manager->removeECSComponent<AutoExposureComponent>(entity); return true; }
        if(typeName == "AntiAliasingComponent"){ manager->removeECSComponent<AntiAliasingComponent>(entity); return true; }
        if(typeName == "ScriptComponent"){ manager->removeECSComponent<ScriptComponent>(entity); return true; }
        return false;
    }

    std::vector<JsonSchema::EntitySnapshotSchemaBase::EntityRecord> collectSnapshotSubtreeRecords(
        const std::vector<JsonSchema::EntitySnapshotSchemaBase::EntityRecord>& entities,
        std::uint64_t rootId
    ){
        std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> childrenByParent;
        childrenByParent.reserve(entities.size());
        for(const auto& entity : entities){
            if(entity.hasParentId){
                childrenByParent[entity.parentId].push_back(entity.id);
            }
        }

        std::unordered_set<std::uint64_t> subtreeIds;
        std::vector<std::uint64_t> stack;
        stack.push_back(rootId);
        while(!stack.empty()){
            const std::uint64_t currentId = stack.back();
            stack.pop_back();
            if(!subtreeIds.insert(currentId).second){
                continue;
            }
            auto childIt = childrenByParent.find(currentId);
            if(childIt == childrenByParent.end()){
                continue;
            }
            for(std::uint64_t childId : childIt->second){
                stack.push_back(childId);
            }
        }

        std::vector<JsonSchema::EntitySnapshotSchemaBase::EntityRecord> out;
        out.reserve(subtreeIds.size());
        for(const auto& entity : entities){
            if(subtreeIds.find(entity.id) != subtreeIds.end()){
                out.push_back(entity);
            }
        }
        return out;
    }

    bool pathWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root){
        if(path.empty() || root.empty()){
            return false;
        }

        std::error_code ec;
        std::filesystem::path normalizedRoot = std::filesystem::weakly_canonical(root, ec);
        if(ec){
            normalizedRoot = root.lexically_normal();
            ec.clear();
        }

        std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, ec);
        if(ec){
            normalizedPath = path.lexically_normal();
        }

        const std::filesystem::path rel = normalizedPath.lexically_relative(normalizedRoot);
        if(rel.empty()){
            return normalizedPath == normalizedRoot;
        }
        return !StringUtils::BeginsWith(rel.generic_string(), "..");
    }

    bool areTransformsEquivalent(const Math3D::Transform& a, const Math3D::Transform& b, float epsilon = 0.0001f){
        if(Math3D::Vec3::distance(a.position, b.position) > epsilon){
            return false;
        }
        if(Math3D::Vec3::distance(a.scale, b.scale) > epsilon){
            return false;
        }
        const Math3D::Vec3 aEuler = a.rotation.ToEuler();
        const Math3D::Vec3 bEuler = b.rotation.ToEuler();
        return Math3D::Vec3::distance(aEuler, bEuler) <= epsilon;
    }

    bool tryResolveValidSceneAssetPath(const PAsset& asset, const std::filesystem::path& assetRoot, std::filesystem::path& outPath){
        outPath.clear();
        if(!asset){
            return false;
        }

        std::unique_ptr<File>& fileHandle = asset->getFileHandle();
        if(!fileHandle || fileHandle->isInMemoryFile()){
            return false;
        }

        std::filesystem::path candidate = std::filesystem::path(fileHandle->getPath()).lexically_normal();
        if(candidate.empty() || !isPathWithExtension(candidate, ".scene") || !pathWithinRoot(candidate, assetRoot)){
            return false;
        }

        std::error_code ec;
        if(!std::filesystem::exists(candidate, ec) || ec){
            return false;
        }
        if(std::filesystem::is_directory(candidate, ec) || ec){
            return false;
        }

        outPath = candidate;
        return true;
    }

    std::string trimCopy(const std::string& value){
        return StringUtils::Trim(value);
    }

    /// @brief Represents Editor Input Blocker behavior.
    class EditorInputBlocker : public IEventHandler {
        public:
            explicit EditorInputBlocker(EditorScene* owner, std::function<bool()> shouldBlock)
                : owner(owner), shouldBlock(std::move(shouldBlock)) {}

            bool onKeyUp(int keyCode, InputManager&) override {
                if(owner && keyCode == SDL_SCANCODE_ESCAPE){
                    if(owner->handleQuitRequest()){
                        return true;
                    }
                }
                return shouldBlock();
            }

            bool onKeyDown(int keyCode, InputManager&) override {
                if(owner && keyCode == SDL_SCANCODE_ESCAPE){
                    if(owner->handleQuitRequest()){
                        return true;
                    }
                }
                return shouldBlock();
            }

            bool onMousePressed(int, InputManager&) override { return shouldBlock(); }
            bool onMouseReleased(int, InputManager&) override { return shouldBlock(); }
            bool onMouseMoved(int, int, InputManager&) override { return shouldBlock(); }
            bool onMouseScroll(float, InputManager&) override { return shouldBlock(); }

        private:
            EditorScene* owner = nullptr;
            std::function<bool()> shouldBlock;
    };

    bool screenPointInViewport(const Math3D::Vec3& screenPos, const TransformWidget::Viewport& viewport){
        if(screenPos.z < 0.0f || screenPos.z > 1.0f){
            return false;
        }
        if(screenPos.x < viewport.x || screenPos.x > (viewport.x + viewport.w)){
            return false;
        }
        if(screenPos.y < viewport.y || screenPos.y > (viewport.y + viewport.h)){
            return false;
        }
        return true;
    }

    struct ScenePerformanceOverlayCounts {
        int entityCount = 0;
        int meshCount = 0;
        int lightCount = 0;
        int cameraCount = 0;
    };

    ScenePerformanceOverlayCounts collectScenePerformanceOverlayCounts(const Scene* scene){
        ScenePerformanceOverlayCounts counts{};
        if(!scene || !scene->getECS()){
            return counts;
        }

        auto* entityManager = scene->getECS()->getEntityManager();
        auto* componentManager = scene->getECS()->getComponentManager();
        if(!entityManager || !componentManager){
            return counts;
        }

        const auto& entities = entityManager->getEntities();
        for(const auto& entityPtr : entities){
            auto* entity = entityPtr.get();
            if(!entity || scene->isSceneRootEntity(entity)){
                continue;
            }

            counts.entityCount++;
            if(auto* mesh = componentManager->getECSComponent<MeshRendererComponent>(entity); mesh && IsComponentActive(mesh)){
                counts.meshCount++;
            }
            if(auto* light = componentManager->getECSComponent<LightComponent>(entity); light && IsComponentActive(light)){
                counts.lightCount++;
            }
            if(auto* camera = componentManager->getECSComponent<CameraComponent>(entity); camera && IsComponentActive(camera)){
                counts.cameraCount++;
            }
        }

        return counts;
    }

    const char* engineRenderStrategyLabel(EngineRenderStrategy strategy){
        switch(strategy){
            case EngineRenderStrategy::Deferred:
                return "Deferred";
            case EngineRenderStrategy::Forward:
            default:
                return "Forward";
        }
    }

    float drawViewportInfoPanel(ImDrawList* drawList,
                                const TransformWidget::Viewport& viewport,
                                float topY,
                                const std::string& text,
                                ImU32 fillColor,
                                ImU32 borderColor){
        if(!drawList || !viewport.valid || text.empty()){
            return topY;
        }

        constexpr float kPanelMarginX = 12.0f;
        constexpr float kPanelPadX = 8.0f;
        constexpr float kPanelPadY = 6.0f;
        constexpr float kPanelCornerRadius = 6.0f;
        constexpr float kPanelGapY = 8.0f;
        const ImVec2 panelMin(viewport.x + kPanelMarginX, topY);
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 panelMax(
            panelMin.x + textSize.x + (kPanelPadX * 2.0f),
            panelMin.y + textSize.y + (kPanelPadY * 2.0f)
        );

        drawList->AddRectFilled(panelMin, panelMax, fillColor, kPanelCornerRadius);
        drawList->AddRect(panelMin, panelMax, borderColor, kPanelCornerRadius, 0, 1.0f);
        drawList->AddText(
            ImVec2(panelMin.x + kPanelPadX, panelMin.y + kPanelPadY - 1.0f),
            IM_COL32(245, 247, 250, 235),
            text.c_str()
        );
        return panelMax.y + kPanelGapY;
    }

    bool applySceneSettingsRawJsonLocal(
        PScene scene,
        const JsonSchema::RawJsonValue& settings,
        const std::unordered_map<std::uint64_t, NeoECS::ECSEntity*>& snapshotIdToEntity,
        std::string* outError
    ){
        if(!scene || !scene->getECS() || !settings.hasValue || settings.json.empty()){
            return true;
        }

        JsonUtils::Document doc;
        if(!JsonUtils::LoadDocumentFromText(settings.json, doc, outError)){
            if(outError && !outError->empty()){
                *outError = "Failed to parse sceneSettings JSON: " + *outError;
            }
            return false;
        }
        JsonUtils::JsonVal* root = doc.root();
        if(!root || !yyjson_is_obj(root)){
            if(outError){
                *outError = "sceneSettings must be a JSON object.";
            }
            return false;
        }

        bool outlineEnabled = scene->isOutlineEnabled();
        JsonUtils::TryGetBool(root, "outlineEnabled", outlineEnabled);
        scene->setOutlineEnabled(outlineEnabled);

        std::uint64_t selectedSnapshotId = 0;
        if(JsonUtils::TryGetUInt64(root, "selectedEntitySnapshotId", selectedSnapshotId)){
            auto selectedIt = snapshotIdToEntity.find(selectedSnapshotId);
            if(selectedIt != snapshotIdToEntity.end() && selectedIt->second){
                scene->setSelectedEntityId(selectedIt->second->getNodeUniqueID());
            }
        }else{
            std::string selectedEntityId;
            if(JsonUtils::TryGetString(root, "selectedEntityId", selectedEntityId) && !selectedEntityId.empty()){
                auto* selectedEntity = scene->getECS()->getEntityManager()->getSpecificEntity(selectedEntityId);
                if(selectedEntity){
                    scene->setSelectedEntityId(selectedEntityId);
                }
            }
        }

        std::uint64_t preferredCameraSnapshotId = 0;
        if(JsonUtils::TryGetUInt64(root, "preferredCameraSnapshotId", preferredCameraSnapshotId)){
            auto preferredIt = snapshotIdToEntity.find(preferredCameraSnapshotId);
            if(preferredIt != snapshotIdToEntity.end() && preferredIt->second){
                auto* cameraComp = scene->getECS()->getComponentManager()->getECSComponent<CameraComponent>(preferredIt->second);
                if(cameraComp && IsComponentActive(cameraComp) && cameraComp->camera){
                    scene->setPreferredCamera(cameraComp->camera, true);
                }
            }
        }

        return true;
    }

    void ensurePreferredCameraAfterSceneLoad(PScene scene){
        if(!scene || scene->getPreferredCamera() || !scene->getECS()){
            return;
        }
        auto* manager = scene->getECS()->getComponentManager();
        auto* entityManager = scene->getECS()->getEntityManager();
        if(!manager || !entityManager){
            return;
        }

        const auto& entities = entityManager->getEntities();
        for(const auto& entityPtr : entities){
            NeoECS::ECSEntity* entity = entityPtr.get();
            if(!entity){
                continue;
            }
            auto* cameraComp = manager->getECSComponent<CameraComponent>(entity);
            if(cameraComp && cameraComp->camera && IsComponentActive(cameraComp)){
                scene->setPreferredCamera(cameraComp->camera, true);
                return;
            }
        }
    }

}

EditorScene::EditorScene(RenderWindow* window, PScene targetScene)
    : Scene(window),
      targetScene(std::move(targetScene)) {
}

EditorScene::EditorScene(RenderWindow* window, PScene targetScene, std::function<PScene(RenderWindow*)> factory)
    : Scene(window),
      targetScene(std::move(targetScene)),
      targetFactory(std::move(factory)) {
}

std::filesystem::path EditorScene::resolveEditorCameraPrefabPath() const{
    const char* appDataRaw = std::getenv("APPDATA");
    if(!appDataRaw || !appDataRaw[0]){
        return {};
    }
    return std::filesystem::path(appDataRaw) / "RenderThingy" / "Editor" / "Camera.prefab";
}

std::filesystem::path EditorScene::resolveEditorSessionPath() const{
    const char* appDataRaw = std::getenv("APPDATA");
    if(!appDataRaw || !appDataRaw[0]){
        return {};
    }
    return std::filesystem::path(appDataRaw) / "RenderThingy" / "Editor" / "Session.json";
}

bool EditorScene::buildSceneHistoryEditorStateRawJson(JsonSchema::RawJsonValue& outEditorState,
                                                      std::string* outError) const{
    outEditorState.hasValue = true;
    outEditorState.json = "{}";

    JsonUtils::MutableDocument doc;
    JsonUtils::JsonMutVal* root = doc.setRootObject();
    if(!root){
        if(outError){
            *outError = "Failed to allocate editor state JSON root.";
        }
        return false;
    }

    JsonUtils::JsonMutVal* shadowDebug = yyjson_mut_obj_add_obj(doc.get(), root, "shadowDebug");
    if(!shadowDebug){
        if(outError){
            *outError = "Failed to allocate editorState.shadowDebug.";
        }
        return false;
    }

    const bool globalOverrideEnabled = ShadowRenderer::GetGlobalDebugOverrideEnabled();
    const int globalOverrideMode = Math3D::Clamp(ShadowRenderer::GetGlobalDebugOverrideMode(), 1, 3);
    const float cascadeKernelMarginTexels = ShadowRenderer::GetDirectionalCascadeKernelMarginTexels();
    const float receiverNormalBlend = ShadowRenderer::GetShadowReceiverNormalBlend();
    if(!JsonUtils::MutObjAddBool(doc.get(), shadowDebug, "globalOverrideEnabled", globalOverrideEnabled) ||
       !JsonUtils::MutObjAddInt(doc.get(), shadowDebug, "globalOverrideMode", globalOverrideMode) ||
       !JsonUtils::MutObjAddFloat(doc.get(), shadowDebug, "cascadeKernelMarginTexels", cascadeKernelMarginTexels) ||
       !JsonUtils::MutObjAddFloat(doc.get(), shadowDebug, "receiverNormalBlend", receiverNormalBlend)){
        if(outError){
            *outError = "Failed to serialize editor shadow debug state.";
        }
        return false;
    }

    JsonUtils::JsonMutVal* sceneView = yyjson_mut_obj_add_obj(doc.get(), root, "sceneView");
    if(!sceneView){
        if(outError){
            *outError = "Failed to allocate editorState.sceneView.";
        }
        return false;
    }

    if(!JsonUtils::MutObjAddBool(doc.get(), sceneView, "showGrid", showSceneGrid) ||
       !JsonUtils::MutObjAddBool(doc.get(), sceneView, "showGizmos", showSceneGizmos) ||
       !JsonUtils::MutObjAddBool(doc.get(), sceneView, "showPerformanceInfo", showScenePerformanceInfo)){
        if(outError){
            *outError = "Failed to serialize editor scene view state.";
        }
        return false;
    }

    if(!JsonUtils::WriteDocumentToString(doc, outEditorState.json, outError, false)){
        if(outError && !outError->empty()){
            *outError = "Failed to encode editor state JSON: " + *outError;
        }
        return false;
    }

    outEditorState.hasValue = true;
    return true;
}

void EditorScene::applySceneHistoryEditorStateRawJson(const JsonSchema::RawJsonValue& editorState){
    if(!editorState.hasValue || editorState.json.empty()){
        return;
    }

    JsonUtils::Document doc;
    std::string error;
    if(!JsonUtils::LoadDocumentFromText(editorState.json, doc, &error)){
        LogBot.Log(LOG_WARN, "Failed to parse editor state JSON: %s", error.c_str());
        return;
    }

    JsonUtils::JsonVal* root = doc.root();
    if(!root || !yyjson_is_obj(root)){
        LogBot.Log(LOG_WARN, "Editor state JSON root must be an object.");
        return;
    }

    if(JsonUtils::JsonVal* shadowDebug = JsonUtils::ObjGetObject(root, "shadowDebug")){
        bool globalOverrideEnabled = ShadowRenderer::GetGlobalDebugOverrideEnabled();
        int globalOverrideMode = Math3D::Clamp(ShadowRenderer::GetGlobalDebugOverrideMode(), 1, 3);
        float cascadeKernelMarginTexels = ShadowRenderer::GetDirectionalCascadeKernelMarginTexels();
        float receiverNormalBlend = ShadowRenderer::GetShadowReceiverNormalBlend();

        JsonUtils::TryGetBool(shadowDebug, "globalOverrideEnabled", globalOverrideEnabled);
        JsonUtils::TryGetInt(shadowDebug, "globalOverrideMode", globalOverrideMode);
        JsonUtils::TryGetFloat(shadowDebug, "cascadeKernelMarginTexels", cascadeKernelMarginTexels);
        JsonUtils::TryGetFloat(shadowDebug, "receiverNormalBlend", receiverNormalBlend);

        ShadowRenderer::SetGlobalDebugOverrideEnabled(globalOverrideEnabled);
        ShadowRenderer::SetGlobalDebugOverrideMode(Math3D::Clamp(globalOverrideMode, 1, 3));
        ShadowRenderer::SetDirectionalCascadeKernelMarginTexels(Math3D::Clamp(cascadeKernelMarginTexels, 0.0f, 32.0f));
        ShadowRenderer::SetShadowReceiverNormalBlend(Math3D::Clamp(receiverNormalBlend, 0.0f, 1.0f));
    }

    if(JsonUtils::JsonVal* sceneView = JsonUtils::ObjGetObject(root, "sceneView")){
        JsonUtils::TryGetBool(sceneView, "showGrid", showSceneGrid);
        JsonUtils::TryGetBool(sceneView, "showGizmos", showSceneGizmos);
        JsonUtils::TryGetBool(sceneView, "showPerformanceInfo", showScenePerformanceInfo);
    }
}

bool EditorScene::buildCurrentEditSnapshot(JsonSchema::SceneSchema& outSchema,
                                           std::string* outJson,
                                           std::string* outError) const{
    if(!targetScene || !targetScene->getECS()){
        if(outError){
            *outError = "Target scene is unavailable.";
        }
        return false;
    }

    SceneIO::SceneSaveOptions options;
    options.registry = &Serialization::DefaultComponentSerializationRegistry();
    options.includeEditorState = true;
    if(!buildSceneHistoryEditorStateRawJson(options.editorState, outError)){
        return false;
    }

    const std::filesystem::path sourcePath = resolveCurrentSceneSourcePath();
    options.metadata.name = sourcePath.stem().string();
    if(options.metadata.name.empty()){
        options.metadata.name = "EditorSessionScene";
    }

    if(!SceneIO::BuildSchemaFromScene(targetScene, outSchema, options, outError)){
        return false;
    }

    if(outSchema.sceneSettings.hasValue && !outSchema.sceneSettings.json.empty()){
        JsonUtils::Document sceneSettingsDoc;
        if(!JsonUtils::LoadDocumentFromText(outSchema.sceneSettings.json, sceneSettingsDoc, outError)){
            return false;
        }

        JsonUtils::MutableDocument mutableSceneSettings;
        if(!mutableSceneSettings.copyFrom(sceneSettingsDoc)){
            if(outError){
                *outError = "Failed to copy scene settings JSON for edit-history normalization.";
            }
            return false;
        }

        JsonUtils::JsonMutVal* root = mutableSceneSettings.root();
        if(root && yyjson_mut_is_obj(root)){
            (void)yyjson_mut_obj_remove_key(root, "selectedEntitySnapshotId");
            (void)yyjson_mut_obj_remove_key(root, "selectedEntityId");
            if(!JsonUtils::WriteDocumentToString(mutableSceneSettings, outSchema.sceneSettings.json, outError, false)){
                return false;
            }
        }
    }

    if(outJson && !outSchema.WriteToString(*outJson, outError, false)){
        return false;
    }
    return true;
}

bool EditorScene::applyEditSnapshot(const JsonSchema::SceneSchema& schema, std::string* outError){
    if(!targetScene || !targetScene->getECS()){
        if(outError){
            *outError = "Target scene is unavailable.";
        }
        return false;
    }

    const std::string preservedSelectionId = selectedEntityId;
    cancelViewportPrefabDragPreview();
    setEditorCameraSettingsOpen(false);
    selectedAssetPath.clear();
    transformWidget.reset();
    lightWidget.reset();
    cameraWidget.reset();
    boundsWidget.reset();
    BoundsEditState::Deactivate();
    previewTexture.reset();
    previewCamera.reset();
    focusActive = false;

    SceneIO::SceneLoadOptions options;
    options.registry = &Serialization::DefaultComponentSerializationRegistry();
    options.clearExistingScene = true;
    options.applySceneSettings = true;

    SceneIO::SceneLoadResult result;
    if(!SceneIO::ApplySchemaToScene(targetScene, schema, options, &result, outError)){
        return false;
    }

    applySceneHistoryEditorStateRawJson(result.editorState);
    if(!preservedSelectionId.empty() && findEntityById(preservedSelectionId)){
        targetScene->setSelectedEntityId(preservedSelectionId);
    }else{
        targetScene->setSelectedEntityId("");
    }
    selectedEntityId = targetScene->getSelectedEntityId();
    targetCamera = targetScene->getPreferredCamera();
    if(!targetCamera){
        if(auto mainScreen = targetScene->getMainScreen()){
            targetCamera = mainScreen->getCamera();
        }
    }
    viewportCamera = editorCamera ? editorCamera : targetCamera;
    targetScene->refreshRenderState();
    return true;
}

void EditorScene::resetEditHistoryToCurrentScene(){
    editHistoryChanges.clear();
    editHistoryIndex = 0;
    pendingDeletedSubtreeSnapshots.clear();
    editHistoryApplying = false;
    resetTrackedEntityObservation();
    refreshStableEntityMappings();
    markEditorSessionDirty(0.0f);
}

void EditorScene::resetTrackedEntityObservation(){
    trackedEntityObservationValid = false;
    trackedEntityChangePending = false;
    lastObservedTrackedEntityState = EditorEntityState{};
    pendingTrackedEntityStateBefore = EditorEntityState{};
}

std::uint64_t EditorScene::computeStableEntityId(const std::string& runtimeId) const{
    std::uint64_t stableId = 0;
    if(tryParseStableEntityId(runtimeId, stableId)){
        return stableId;
    }
    return hashStableEntityId(runtimeId);
}

std::uint64_t EditorScene::computeStableEntityId(NeoECS::ECSEntity* entity) const{
    return entity ? computeStableEntityId(entity->getNodeUniqueID()) : 0;
}

void EditorScene::refreshStableEntityMappings(){
    stableEntityRuntimeIds.clear();
    if(!targetScene || !targetScene->getECS()){
        return;
    }

    const auto& entities = targetScene->getECS()->getEntityManager()->getEntities();
    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(!entity){
            continue;
        }
        stableEntityRuntimeIds[computeStableEntityId(entity)] = entity->getNodeUniqueID();
    }
}

NeoECS::ECSEntity* EditorScene::findEntityByStableId(std::uint64_t stableId) const{
    if(stableId == 0 || !targetScene || !targetScene->getECS()){
        return nullptr;
    }

    auto it = stableEntityRuntimeIds.find(stableId);
    if(it != stableEntityRuntimeIds.end()){
        if(NeoECS::ECSEntity* mappedEntity = findEntityById(it->second)){
            if(computeStableEntityId(mappedEntity) == stableId){
                return mappedEntity;
            }
        }
        auto* self = const_cast<EditorScene*>(this);
        self->stableEntityRuntimeIds.erase(stableId);
    }

    const auto& entities = targetScene->getECS()->getEntityManager()->getEntities();
    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(!entity){
            continue;
        }
        if(computeStableEntityId(entity) == stableId){
            auto* self = const_cast<EditorScene*>(this);
            self->stableEntityRuntimeIds[stableId] = entity->getNodeUniqueID();
            return entity;
        }
    }
    return nullptr;
}

bool EditorScene::captureEntityState(NeoECS::ECSEntity* entity,
                                     EditorEntityState& outState,
                                     std::string* outError) const{
    outState = EditorEntityState{};
    if(!targetScene || !targetScene->getECS() || !entity){
        if(outError){
            *outError = "Cannot capture entity state: scene/entity is unavailable.";
        }
        return false;
    }

    outState.stableId = computeStableEntityId(entity);
    outState.name = entity->getName();
    auto* manager = targetScene->getECS()->getComponentManager();
    if(!Serialization::DefaultComponentSerializationRegistry().serializeEntityComponents(
            manager,
            entity,
            outState.components,
            outError)){
        return false;
    }

    auto* self = const_cast<EditorScene*>(this);
    self->stableEntityRuntimeIds[outState.stableId] = entity->getNodeUniqueID();
    return true;
}

bool EditorScene::captureEntityStateByStableId(std::uint64_t stableId,
                                               EditorEntityState& outState,
                                               std::string* outError) const{
    NeoECS::ECSEntity* entity = findEntityByStableId(stableId);
    if(!entity){
        if(outError){
            *outError = "Target entity was not found.";
        }
        return false;
    }
    return captureEntityState(entity, outState, outError);
}

bool EditorScene::applyEntityState(const EditorEntityState& state, std::string* outError){
    if(!targetScene || !targetScene->getECS()){
        if(outError){
            *outError = "Target scene is unavailable.";
        }
        return false;
    }

    NeoECS::ECSEntity* entity = findEntityByStableId(state.stableId);
    if(!entity){
        if(outError){
            *outError = "Target entity for undo/redo was not found.";
        }
        return false;
    }

    auto* ecs = targetScene->getECS();
    auto* manager = ecs->getComponentManager();
    const auto existingComponents = manager->getECSComponents(entity);

    std::unordered_set<std::string> desiredTypes;
    desiredTypes.reserve(state.components.size());
    for(const auto& component : state.components){
        desiredTypes.insert(component.type);
    }

    for(NeoECS::ECSComponent* existing : existingComponents){
        const char* typeName = serializedComponentTypeName(existing);
        if(!typeName){
            continue;
        }
        if(desiredTypes.find(typeName) == desiredTypes.end()){
            (void)removeSerializedComponentByType(manager, entity, typeName);
        }
    }

    entity->setName(state.name);
    if(!Serialization::DefaultComponentSerializationRegistry().deserializeEntityComponents(
            ecs->getContext(),
            manager,
            entity,
            state.components,
            outError)){
        return false;
    }

    stableEntityRuntimeIds[state.stableId] = entity->getNodeUniqueID();
    return true;
}

bool EditorScene::captureSubtreeSnapshot(const std::vector<NeoECS::ECSEntity*>& roots,
                                         EditorSubtreeSnapshot& outSnapshot,
                                         std::string* outError) const{
    outSnapshot = EditorSubtreeSnapshot{};
    if(!targetScene || !targetScene->getECS()){
        if(outError){
            *outError = "Target scene is unavailable.";
        }
        return false;
    }
    if(roots.empty()){
        if(outError){
            *outError = "Cannot capture an empty subtree snapshot.";
        }
        return false;
    }

    Serialization::SnapshotIO::SnapshotBuildResult snapshotBuild;
    Serialization::SnapshotIO::SnapshotBuildOptions options;
    options.registry = &Serialization::DefaultComponentSerializationRegistry();
    if(!Serialization::SnapshotIO::BuildSnapshotFromEntityRoots(targetScene, roots, snapshotBuild, options, outError)){
        return false;
    }

    outSnapshot.entities = std::move(snapshotBuild.entities);
    outSnapshot.rootIds = std::move(snapshotBuild.rootEntityIds);
    std::unordered_map<std::uint64_t, std::uint64_t> rootParentIdsByRootId;
    rootParentIdsByRootId.reserve(roots.size());

    auto* self = const_cast<EditorScene*>(this);
    for(const auto& entityToId : snapshotBuild.entityToSnapshotId){
        if(entityToId.first){
            self->stableEntityRuntimeIds[entityToId.second] = entityToId.first->getNodeUniqueID();
        }
    }

    for(NeoECS::ECSEntity* root : roots){
        if(!root){
            continue;
        }
        const auto snapshotIdIt = snapshotBuild.entityToSnapshotId.find(root);
        if(snapshotIdIt == snapshotBuild.entityToSnapshotId.end()){
            continue;
        }
        NeoECS::ECSEntity* parent = root->getParent();
        if(parent && !targetScene->isSceneRootEntity(parent)){
            rootParentIdsByRootId[snapshotIdIt->second] = computeStableEntityId(parent);
        }else{
            rootParentIdsByRootId[snapshotIdIt->second] = 0;
        }
    }

    outSnapshot.rootParentIds.reserve(outSnapshot.rootIds.size());
    for(std::uint64_t rootId : outSnapshot.rootIds){
        auto parentIt = rootParentIdsByRootId.find(rootId);
        outSnapshot.rootParentIds.push_back(parentIt != rootParentIdsByRootId.end() ? parentIt->second : 0);
    }

    return true;
}

bool EditorScene::applySubtreePresence(const EditorSubtreeSnapshot& snapshot,
                                       bool present,
                                       std::string* outError){
    if(!targetScene || !targetScene->getECS()){
        if(outError){
            *outError = "Target scene is unavailable.";
        }
        return false;
    }

    auto* ecs = targetScene->getECS();
    auto* ctx = ecs->getContext();

    for(std::uint64_t rootId : snapshot.rootIds){
        if(NeoECS::ECSEntity* existingRoot = findEntityByStableId(rootId)){
            if(!targetScene->isSceneRootEntity(existingRoot)){
                std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(ctx, existingRoot));
                if(wrapper){
                    targetScene->destroyECSGameObject(wrapper.get());
                }
            }
        }
    }

    if(!present){
        for(const auto& entityRecord : snapshot.entities){
            stableEntityRuntimeIds.erase(entityRecord.id);
            pendingDeletedSubtreeSnapshots.erase(entityRecord.id);
        }
        return true;
    }

    for(size_t i = 0; i < snapshot.rootIds.size(); ++i){
        const std::uint64_t rootId = snapshot.rootIds[i];
        const std::uint64_t parentStableId =
            (i < snapshot.rootParentIds.size()) ? snapshot.rootParentIds[i] : 0;

        std::unique_ptr<NeoECS::GameObject> parentWrapper;
        if(parentStableId != 0){
            NeoECS::ECSEntity* parentEntity = findEntityByStableId(parentStableId);
            if(!parentEntity){
                if(outError){
                    *outError = "Undo/redo parent entity was not found.";
                }
                return false;
            }
            parentWrapper.reset(NeoECS::GameObject::CreateFromECSEntity(ctx, parentEntity));
            if(!parentWrapper){
                if(outError){
                    *outError = "Failed to resolve undo/redo parent wrapper.";
                }
                return false;
            }
        }

        Serialization::SnapshotIO::SnapshotInstantiateResult instantiateResult;
        Serialization::SnapshotIO::SnapshotInstantiateOptions options;
        options.destinationParent = parentWrapper.get();
        options.registry = &Serialization::DefaultComponentSerializationRegistry();
        const auto subtreeRecords = collectSnapshotSubtreeRecords(snapshot.entities, rootId);
        if(!Serialization::SnapshotIO::InstantiateSnapshotIntoScene(
                targetScene,
                subtreeRecords,
                std::vector<std::uint64_t>{rootId},
                instantiateResult,
                options,
                outError)){
            return false;
        }

        for(const auto& snapshotIdToEntity : instantiateResult.snapshotIdToEntity){
            if(snapshotIdToEntity.second){
                stableEntityRuntimeIds[snapshotIdToEntity.first] =
                    snapshotIdToEntity.second->getNodeUniqueID();
            }
        }
    }

    return true;
}

bool EditorScene::applyEntityReparent(std::uint64_t entityStableId,
                                      std::uint64_t parentStableId,
                                      std::string* outError){
    if(!targetScene || !targetScene->getECS()){
        if(outError){
            *outError = "Target scene is unavailable.";
        }
        return false;
    }

    NeoECS::ECSEntity* entity = findEntityByStableId(entityStableId);
    if(!entity){
        if(outError){
            *outError = "Target entity for reparent was not found.";
        }
        return false;
    }

    NeoECS::ECSEntity* parentEntity = targetScene->getSceneRootEntity();
    if(parentStableId != 0){
        parentEntity = findEntityByStableId(parentStableId);
        if(!parentEntity){
            if(outError){
                *outError = "Target parent entity for reparent was not found.";
            }
            return false;
        }
    }

    auto* ctx = targetScene->getECS()->getContext();
    std::unique_ptr<NeoECS::GameObject> entityWrapper(NeoECS::GameObject::CreateFromECSEntity(ctx, entity));
    std::unique_ptr<NeoECS::GameObject> parentWrapper(NeoECS::GameObject::CreateFromECSEntity(ctx, parentEntity));
    if(!entityWrapper || !parentWrapper){
        if(outError){
            *outError = "Failed to create game-object wrappers for reparent.";
        }
        return false;
    }
    if(!entityWrapper->setParent(parentWrapper.get())){
        if(outError){
            *outError = "Reparent operation failed.";
        }
        return false;
    }

    stableEntityRuntimeIds[entityStableId] = entity->getNodeUniqueID();
    return true;
}

bool EditorScene::applyEditHistoryChange(const EditorSceneChange& change,
                                         bool applyAfterState,
                                         std::string* outError){
    switch(change.kind){
        case EditorSceneChange::Kind::EntityState:
            return applyEntityState(applyAfterState ? change.afterEntityState : change.beforeEntityState, outError);
        case EditorSceneChange::Kind::EntityReparent:
            return applyEntityReparent(
                change.targetStableId,
                applyAfterState ? change.afterParentStableId : change.beforeParentStableId,
                outError
            );
        case EditorSceneChange::Kind::SubtreePresence:
            return applySubtreePresence(
                change.subtreeSnapshot,
                applyAfterState ? change.subtreePresentAfter : change.subtreePresentBefore,
                outError
            );
    }

    if(outError){
        *outError = "Unsupported edit-history change kind.";
    }
    return false;
}

void EditorScene::pushEditHistoryChange(EditorSceneChange change){
    if(change.kind == EditorSceneChange::Kind::EntityState){
        const bool identicalState =
            change.beforeEntityState.stableId == change.afterEntityState.stableId &&
            change.beforeEntityState.name == change.afterEntityState.name &&
            componentRecordListsEqual(change.beforeEntityState.components, change.afterEntityState.components);
        if(identicalState){
            return;
        }
    }else if(change.kind == EditorSceneChange::Kind::EntityReparent){
        if(change.beforeParentStableId == change.afterParentStableId){
            return;
        }
    }else if(change.kind == EditorSceneChange::Kind::SubtreePresence){
        if(change.subtreePresentBefore == change.subtreePresentAfter || change.subtreeSnapshot.rootIds.empty()){
            return;
        }
    }

    if(editHistoryIndex < editHistoryChanges.size()){
        using HistoryDiff = std::vector<EditorSceneChange>::difference_type;
        editHistoryChanges.erase(
            editHistoryChanges.begin() + static_cast<HistoryDiff>(editHistoryIndex),
            editHistoryChanges.end()
        );
    }

    editHistoryChanges.push_back(std::move(change));
    if(editHistoryChanges.size() > kMaxEditHistoryEntries){
        editHistoryChanges.erase(editHistoryChanges.begin());
        if(editHistoryIndex > 0){
            --editHistoryIndex;
        }
    }
    editHistoryIndex = editHistoryChanges.size();
    markEditorSessionDirty(0.25f);
}

void EditorScene::observeCurrentEditState(bool interactionActive){
    if(playState != PlayState::Edit || editHistoryApplying || !targetScene || !targetScene->getECS()){
        resetTrackedEntityObservation();
        return;
    }

    NeoECS::ECSEntity* trackedEntity = findEntityById(selectedEntityId);
    if(!trackedEntity){
        if(!interactionActive){
            resetTrackedEntityObservation();
        }
        return;
    }

    EditorEntityState currentState;
    std::string error;
    if(!captureEntityState(trackedEntity, currentState, &error)){
        LogBot.Log(LOG_WARN, "Failed to observe edit state: %s", error.c_str());
        return;
    }

    if(!trackedEntityObservationValid || currentState.stableId != lastObservedTrackedEntityState.stableId){
        lastObservedTrackedEntityState = currentState;
        trackedEntityObservationValid = true;
        trackedEntityChangePending = false;
        pendingTrackedEntityStateBefore = EditorEntityState{};
        return;
    }

    const bool stateUnchanged =
        currentState.name == lastObservedTrackedEntityState.name &&
        componentRecordListsEqual(currentState.components, lastObservedTrackedEntityState.components);
    if(stateUnchanged){
        if(!interactionActive){
            trackedEntityChangePending = false;
            pendingTrackedEntityStateBefore = EditorEntityState{};
        }
        return;
    }

    if(interactionActive){
        if(!trackedEntityChangePending){
            pendingTrackedEntityStateBefore = lastObservedTrackedEntityState;
            trackedEntityChangePending = true;
        }
        return;
    }

    EditorSceneChange change;
    change.kind = EditorSceneChange::Kind::EntityState;
    change.label = "Entity edited";
    change.valuePath = "entity.state";
    change.targetStableId = currentState.stableId;
    change.beforeEntityState = trackedEntityChangePending ? pendingTrackedEntityStateBefore : lastObservedTrackedEntityState;
    change.afterEntityState = currentState;
    pushEditHistoryChange(std::move(change));

    lastObservedTrackedEntityState = currentState;
    trackedEntityObservationValid = true;
    trackedEntityChangePending = false;
    pendingTrackedEntityStateBefore = EditorEntityState{};
}

void EditorScene::flushEditHistoryObservation(bool forceCommit){
    if(playState != PlayState::Edit || editHistoryApplying || !targetScene || !targetScene->getECS()){
        resetTrackedEntityObservation();
        return;
    }

    if(!forceCommit && !trackedEntityChangePending){
        return;
    }

    NeoECS::ECSEntity* trackedEntity = findEntityById(selectedEntityId);
    if(!trackedEntity){
        resetTrackedEntityObservation();
        return;
    }

    EditorEntityState currentState;
    std::string error;
    if(!captureEntityState(trackedEntity, currentState, &error)){
        LogBot.Log(LOG_WARN, "Failed to flush edit state: %s", error.c_str());
        resetTrackedEntityObservation();
        return;
    }

    if(!trackedEntityObservationValid || currentState.stableId != lastObservedTrackedEntityState.stableId){
        lastObservedTrackedEntityState = currentState;
        trackedEntityObservationValid = true;
        trackedEntityChangePending = false;
        pendingTrackedEntityStateBefore = EditorEntityState{};
        return;
    }

    const bool stateUnchanged =
        currentState.name == lastObservedTrackedEntityState.name &&
        componentRecordListsEqual(currentState.components, lastObservedTrackedEntityState.components);
    if(stateUnchanged){
        trackedEntityChangePending = false;
        pendingTrackedEntityStateBefore = EditorEntityState{};
        return;
    }

    EditorSceneChange change;
    change.kind = EditorSceneChange::Kind::EntityState;
    change.label = "Entity edited";
    change.valuePath = "entity.state";
    change.targetStableId = currentState.stableId;
    change.beforeEntityState = trackedEntityChangePending ? pendingTrackedEntityStateBefore : lastObservedTrackedEntityState;
    change.afterEntityState = currentState;
    pushEditHistoryChange(std::move(change));

    lastObservedTrackedEntityState = currentState;
    trackedEntityObservationValid = true;
    trackedEntityChangePending = false;
    pendingTrackedEntityStateBefore = EditorEntityState{};
}

bool EditorScene::canUndoEditHistory() const{
    return playState == PlayState::Edit && editHistoryIndex > 0;
}

bool EditorScene::canRedoEditHistory() const{
    return playState == PlayState::Edit && editHistoryIndex < editHistoryChanges.size();
}

bool EditorScene::performUndo(){
    if(playState != PlayState::Edit){
        setIoStatus("Undo is only available in Edit mode.", true);
        return false;
    }

    flushEditHistoryObservation(true);
    if(!canUndoEditHistory()){
        return false;
    }

    const size_t targetIndex = editHistoryIndex - 1;
    const std::uint64_t preferredSelectedStableId =
        selectedEntityId.empty() ? 0 : computeStableEntityId(selectedEntityId);
    prepareForSceneMutationTracking();
    editHistoryApplying = true;
    std::string error;
    const bool ok = applyEditHistoryChange(editHistoryChanges[targetIndex], false, &error);
    editHistoryApplying = false;
    if(!ok){
        setIoStatus("Undo failed: " + error, true);
        return false;
    }

    editHistoryIndex = targetIndex;
    syncTargetSceneAfterEditHistoryApply(preferredSelectedStableId);
    markEditorSessionDirty(0.0f);
    setIoStatus("Undo applied.", false);
    return true;
}

bool EditorScene::performRedo(){
    if(playState != PlayState::Edit){
        setIoStatus("Redo is only available in Edit mode.", true);
        return false;
    }

    flushEditHistoryObservation(true);
    if(!canRedoEditHistory()){
        return false;
    }

    const size_t targetIndex = editHistoryIndex;
    const std::uint64_t preferredSelectedStableId =
        selectedEntityId.empty() ? 0 : computeStableEntityId(selectedEntityId);
    prepareForSceneMutationTracking();
    editHistoryApplying = true;
    std::string error;
    const bool ok = applyEditHistoryChange(editHistoryChanges[targetIndex], true, &error);
    editHistoryApplying = false;
    if(!ok){
        setIoStatus("Redo failed: " + error, true);
        return false;
    }

    editHistoryIndex = targetIndex + 1;
    syncTargetSceneAfterEditHistoryApply(preferredSelectedStableId);
    markEditorSessionDirty(0.0f);
    setIoStatus("Redo applied.", false);
    return true;
}

void EditorScene::beginPendingDeletedSubtreeCapture(const std::string& entityId){
    if(playState != PlayState::Edit || editHistoryApplying || entityId.empty()){
        return;
    }

    flushEditHistoryObservation(true);

    NeoECS::ECSEntity* entity = findEntityById(entityId);
    if(!entity || (targetScene && targetScene->isSceneRootEntity(entity))){
        return;
    }

    EditorSubtreeSnapshot snapshot;
    std::string error;
    if(!captureSubtreeSnapshot(std::vector<NeoECS::ECSEntity*>{entity}, snapshot, &error)){
        LogBot.Log(LOG_WARN, "Failed to capture pending delete snapshot: %s", error.c_str());
        return;
    }

    pendingDeletedSubtreeSnapshots[computeStableEntityId(entity)] = std::move(snapshot);
}

void EditorScene::commitPendingDeletedSubtreeCapture(const std::string& entityId){
    if(playState != PlayState::Edit || editHistoryApplying || entityId.empty()){
        return;
    }

    const std::uint64_t stableId = computeStableEntityId(entityId);
    auto it = pendingDeletedSubtreeSnapshots.find(stableId);
    if(it == pendingDeletedSubtreeSnapshots.end()){
        return;
    }

    EditorSceneChange change;
    change.kind = EditorSceneChange::Kind::SubtreePresence;
    change.label = "Entity deleted";
    change.valuePath = "entity.delete";
    change.targetStableId = stableId;
    change.subtreeSnapshot = std::move(it->second);
    change.subtreePresentBefore = true;
    change.subtreePresentAfter = false;
    pendingDeletedSubtreeSnapshots.erase(it);
    pushEditHistoryChange(std::move(change));
    resetTrackedEntityObservation();
}

void EditorScene::recordCreatedSubtreeChange(const std::vector<NeoECS::ECSEntity*>& roots,
                                             const std::string& label,
                                             const std::string& valuePath){
    if(playState != PlayState::Edit || editHistoryApplying || roots.empty()){
        return;
    }

    flushEditHistoryObservation(true);

    EditorSubtreeSnapshot snapshot;
    std::string error;
    if(!captureSubtreeSnapshot(roots, snapshot, &error)){
        LogBot.Log(LOG_WARN, "Failed to capture created subtree snapshot: %s", error.c_str());
        return;
    }

    EditorSceneChange change;
    change.kind = EditorSceneChange::Kind::SubtreePresence;
    change.label = label;
    change.valuePath = valuePath;
    change.targetStableId = snapshot.rootIds.empty() ? 0 : snapshot.rootIds.front();
    change.subtreeSnapshot = std::move(snapshot);
    change.subtreePresentBefore = false;
    change.subtreePresentAfter = true;
    pushEditHistoryChange(std::move(change));
    resetTrackedEntityObservation();
}

void EditorScene::recordCreatedEntityChange(const std::string& entityId,
                                            const std::string& label,
                                            const std::string& valuePath){
    if(entityId.empty()){
        return;
    }

    NeoECS::ECSEntity* entity = findEntityById(entityId);
    if(!entity){
        return;
    }

    recordCreatedSubtreeChange(std::vector<NeoECS::ECSEntity*>{entity}, label, valuePath);
}

void EditorScene::recordRenamedEntityChange(const std::string& entityId,
                                            const std::string& oldName,
                                            const std::string& newName){
    if(playState != PlayState::Edit || editHistoryApplying || entityId.empty() || oldName == newName){
        return;
    }

    NeoECS::ECSEntity* entity = findEntityById(entityId);
    if(!entity){
        return;
    }

    EditorEntityState afterState;
    std::string error;
    if(!captureEntityState(entity, afterState, &error)){
        LogBot.Log(LOG_WARN, "Failed to capture renamed entity state: %s", error.c_str());
        return;
    }

    EditorSceneChange change;
    change.kind = EditorSceneChange::Kind::EntityState;
    change.label = "Entity renamed";
    change.valuePath = "entity.name";
    change.targetStableId = afterState.stableId;
    change.afterEntityState = afterState;
    change.beforeEntityState = afterState;
    change.beforeEntityState.name = oldName;
    change.afterEntityState.name = newName;
    pushEditHistoryChange(std::move(change));
    resetTrackedEntityObservation();
}

void EditorScene::recordReparentedEntityChange(const std::string& entityId,
                                               const std::string& oldParentId,
                                               const std::string& newParentId){
    if(playState != PlayState::Edit || editHistoryApplying || entityId.empty() || oldParentId == newParentId){
        return;
    }

    flushEditHistoryObservation(true);

    NeoECS::ECSEntity* entity = findEntityById(entityId);
    if(!entity){
        return;
    }

    EditorSceneChange change;
    change.kind = EditorSceneChange::Kind::EntityReparent;
    change.label = "Entity reparented";
    change.valuePath = "entity.parent";
    change.targetStableId = computeStableEntityId(entity);
    change.beforeParentStableId = oldParentId.empty() ? 0 : computeStableEntityId(oldParentId);
    change.afterParentStableId = newParentId.empty() ? 0 : computeStableEntityId(newParentId);
    pushEditHistoryChange(std::move(change));
    resetTrackedEntityObservation();
}

void EditorScene::prepareForSceneMutationTracking(){
    cancelViewportPrefabDragPreview();
    transformWidget.reset();
    lightWidget.reset();
    cameraWidget.reset();
    boundsWidget.reset();
    BoundsEditState::Deactivate();
    focusActive = false;
}

void EditorScene::syncTargetSceneAfterEditHistoryApply(std::uint64_t preferredSelectedStableId){
    if(!targetScene){
        return;
    }

    refreshStableEntityMappings();

    std::string resolvedSelectedId;
    if(preferredSelectedStableId != 0){
        if(NeoECS::ECSEntity* entity = findEntityByStableId(preferredSelectedStableId)){
            resolvedSelectedId = entity->getNodeUniqueID();
        }
    }

    selectedEntityId = resolvedSelectedId;
    targetScene->setSelectedEntityId(selectedEntityId);

    targetCamera = targetScene->getPreferredCamera();
    if(!targetCamera){
        if(auto mainScreen = targetScene->getMainScreen()){
            targetCamera = mainScreen->getCamera();
        }
    }
    viewportCamera = editorCamera ? editorCamera : targetCamera;
    targetScene->refreshRenderState();
    resetTrackedEntityObservation();
}

void EditorScene::observeTransientEditorSessionState(){
    if(!observedSceneViewStateValid ||
       showSceneGrid != lastObservedShowSceneGrid ||
       showSceneGizmos != lastObservedShowSceneGizmos ||
       showScenePerformanceInfo != lastObservedShowScenePerformanceInfo){
        lastObservedShowSceneGrid = showSceneGrid;
        lastObservedShowSceneGizmos = showSceneGizmos;
        lastObservedShowScenePerformanceInfo = showScenePerformanceInfo;
        observedSceneViewStateValid = true;
        markEditorSessionDirty();
    }

    const PropertiesPanel::State propertiesState = propertiesPanel.captureState();
    if(!observedPropertiesPanelStateValid ||
       propertiesState.showHiddenComponents != lastObservedPropertiesPanelState.showHiddenComponents){
        lastObservedPropertiesPanelState = propertiesState;
        observedPropertiesPanelStateValid = true;
        markEditorSessionDirty();
    }

    Math3D::Transform currentCameraTransform;
    bool hasCameraTransform = false;
    if(editorCamera){
        currentCameraTransform = editorCamera->transform();
        hasCameraTransform = true;
    }else if(editorCameraTransform){
        currentCameraTransform = editorCameraTransform->local;
        hasCameraTransform = true;
    }

    if(hasCameraTransform){
        if(!observedEditorCameraStateValid ||
           !areTransformsEquivalent(currentCameraTransform, lastObservedEditorCameraTransform) ||
           !Math3D::AreClose(editorYaw, lastObservedEditorYaw, 0.0001f) ||
           !Math3D::AreClose(editorPitch, lastObservedEditorPitch, 0.0001f)){
            lastObservedEditorCameraTransform = currentCameraTransform;
            lastObservedEditorYaw = editorYaw;
            lastObservedEditorPitch = editorPitch;
            observedEditorCameraStateValid = true;
            markEditorSessionDirty();
        }
    }
}

void EditorScene::markEditorSessionDirty(float saveDelaySeconds){
    editorSessionDirty = true;
    editorSessionSaveRequested = false;
    if(editorSessionSaveDelaySeconds <= 0.0f){
        editorSessionSaveDelaySeconds = Math3D::Max(0.0f, saveDelaySeconds);
    }else{
        editorSessionSaveDelaySeconds = Math3D::Min(editorSessionSaveDelaySeconds, Math3D::Max(0.0f, saveDelaySeconds));
    }
}

void EditorScene::advanceEditorSessionAutosave(float deltaTime){
    if(!editorSessionDirty){
        return;
    }
    if(editorSessionSaveRequested){
        return;
    }

    editorSessionSaveDelaySeconds = std::max(0.0f, editorSessionSaveDelaySeconds - deltaTime);
    if(editorSessionSaveDelaySeconds <= 0.0f){
        editorSessionSaveRequested = true;
    }
}

void EditorScene::flushEditorSessionAutosave(bool force, bool interactionActive){
    if(!editorSessionDirty){
        return;
    }
    if(!force){
        if(!editorSessionSaveRequested || interactionActive){
            return;
        }
    }

    flushEditHistoryObservation(force);

    std::string error;
    if(!saveEditorSessionToDisk(&error)){
        LogBot.Log(LOG_WARN, "Failed to save editor session: %s", error.c_str());
        if(force){
            setIoStatus("Save editor session failed: " + error, true);
        }
        return;
    }

    editorSessionDirty = false;
    editorSessionSaveRequested = false;
    editorSessionSaveDelaySeconds = 0.0f;
}

bool EditorScene::saveEditorSessionToDisk(std::string* outError){
    const std::filesystem::path sessionPath = resolveEditorSessionPath();
    if(sessionPath.empty()){
        if(outError){
            *outError = "APPDATA is unavailable; editor session path cannot be resolved.";
        }
        return false;
    }

    std::string sceneEditsJson;
    if(targetScene && targetScene->getECS()){
        JsonSchema::SceneSchema sceneSchema;
        if(!buildCurrentEditSnapshot(sceneSchema, &sceneEditsJson, outError)){
            return false;
        }
    }

    JsonUtils::MutableDocument doc;
    JsonUtils::StandardDocumentRefs refs;
    if(!JsonUtils::CreateStandardDocument(doc, kEditorSessionDocumentType, kEditorSessionDocumentVersion, refs, outError)){
        return false;
    }
    if(!refs.payload){
        if(outError){
            *outError = "Failed to allocate editor session payload.";
        }
        return false;
    }

    const std::filesystem::path sourcePath = resolveCurrentSceneSourcePath();
    if(!JsonUtils::MutObjAddString(doc.get(), refs.payload, "lastLoadedScenePath", sourcePath.generic_string()) ||
       !JsonUtils::MutObjAddString(doc.get(), refs.payload, "sceneEditsJson", sceneEditsJson)){
        if(outError){
            *outError = "Failed to write editor session scene state.";
        }
        return false;
    }

    JsonUtils::JsonMutVal* propertiesObj = yyjson_mut_obj_add_obj(doc.get(), refs.payload, "propertiesPanel");
    if(!propertiesObj){
        if(outError){
            *outError = "Failed to allocate propertiesPanel session object.";
        }
        return false;
    }
    const PropertiesPanel::State propertiesState = propertiesPanel.captureState();
    if(!JsonUtils::MutObjAddBool(doc.get(), propertiesObj, "showHiddenComponents", propertiesState.showHiddenComponents)){
        if(outError){
            *outError = "Failed to serialize properties-panel session state.";
        }
        return false;
    }

    JsonUtils::JsonMutVal* sceneViewObj = yyjson_mut_obj_add_obj(doc.get(), refs.payload, "sceneView");
    if(!sceneViewObj){
        if(outError){
            *outError = "Failed to allocate sceneView session object.";
        }
        return false;
    }
    if(!JsonUtils::MutObjAddBool(doc.get(), sceneViewObj, "showGrid", showSceneGrid) ||
       !JsonUtils::MutObjAddBool(doc.get(), sceneViewObj, "showGizmos", showSceneGizmos) ||
       !JsonUtils::MutObjAddBool(doc.get(), sceneViewObj, "showPerformanceInfo", showScenePerformanceInfo)){
        if(outError){
            *outError = "Failed to serialize scene-view session state.";
        }
        return false;
    }

    Math3D::Transform cameraTransform;
    if(editorCamera){
        cameraTransform = editorCamera->transform();
    }else if(editorCameraTransform){
        cameraTransform = editorCameraTransform->local;
    }

    JsonUtils::JsonMutVal* cameraTransformObj = yyjson_mut_obj_add_obj(doc.get(), refs.payload, "editorCameraTransform");
    if(!cameraTransformObj){
        if(outError){
            *outError = "Failed to allocate editorCameraTransform session object.";
        }
        return false;
    }
    if(!JsonUtils::MutObjAddVec3(doc.get(), cameraTransformObj, "position", cameraTransform.position) ||
       !JsonUtils::MutObjAddVec3(doc.get(), cameraTransformObj, "rotationEuler", cameraTransform.rotation.ToEuler()) ||
       !JsonUtils::MutObjAddVec3(doc.get(), cameraTransformObj, "scale", cameraTransform.scale) ||
       !JsonUtils::MutObjAddFloat(doc.get(), refs.payload, "editorYaw", editorYaw) ||
       !JsonUtils::MutObjAddFloat(doc.get(), refs.payload, "editorPitch", editorPitch)){
        if(outError){
            *outError = "Failed to serialize editor camera session state.";
        }
        return false;
    }

    return JsonUtils::SaveDocumentToAbsolutePath(sessionPath, doc, outError, true);
}

bool EditorScene::loadEditorSessionFromDisk(std::string* outError){
    const std::filesystem::path sessionPath = resolveEditorSessionPath();
    if(sessionPath.empty()){
        if(outError){
            *outError = "APPDATA is unavailable; editor session path cannot be resolved.";
        }
        return false;
    }

    std::error_code ec;
    if(!std::filesystem::exists(sessionPath, ec) || std::filesystem::is_directory(sessionPath, ec)){
        return false;
    }

    JsonUtils::Document doc;
    if(!JsonUtils::LoadDocumentFromAbsolutePath(sessionPath, doc, outError)){
        return false;
    }

    std::string type;
    int version = 0;
    JsonUtils::JsonVal* payload = nullptr;
    if(!JsonUtils::ReadStandardDocumentHeader(doc, type, version, &payload, outError)){
        return false;
    }
    if(type != kEditorSessionDocumentType){
        if(outError){
            *outError = "Editor session type mismatch.";
        }
        return false;
    }
    if(version != kEditorSessionDocumentVersion){
        if(outError){
            *outError = "Unsupported editor session version: " + std::to_string(version);
        }
        return false;
    }
    if(!payload || !yyjson_is_obj(payload)){
        if(outError){
            *outError = "Editor session payload must be an object.";
        }
        return false;
    }

    std::string lastLoadedScenePath;
    std::string sceneEditsJson;
    JsonUtils::TryGetString(payload, "lastLoadedScenePath", lastLoadedScenePath);
    JsonUtils::TryGetString(payload, "sceneEditsJson", sceneEditsJson);

    PropertiesPanel::State propertiesState = propertiesPanel.captureState();
    if(JsonUtils::JsonVal* propertiesObj = JsonUtils::ObjGetObject(payload, "propertiesPanel")){
        JsonUtils::TryGetBool(propertiesObj, "showHiddenComponents", propertiesState.showHiddenComponents);
    }
    bool savedShowSceneGrid = showSceneGrid;
    bool savedShowSceneGizmos = showSceneGizmos;
    bool savedShowScenePerformanceInfo = showScenePerformanceInfo;
    if(JsonUtils::JsonVal* sceneViewObj = JsonUtils::ObjGetObject(payload, "sceneView")){
        JsonUtils::TryGetBool(sceneViewObj, "showGrid", savedShowSceneGrid);
        JsonUtils::TryGetBool(sceneViewObj, "showGizmos", savedShowSceneGizmos);
        JsonUtils::TryGetBool(sceneViewObj, "showPerformanceInfo", savedShowScenePerformanceInfo);
    }

    Math3D::Transform savedCameraTransform;
    bool hasSavedCameraTransform = false;
    if(JsonUtils::JsonVal* cameraTransformObj = JsonUtils::ObjGetObject(payload, "editorCameraTransform")){
        hasSavedCameraTransform = true;
        JsonUtils::TryGetVec3(cameraTransformObj, "position", savedCameraTransform.position);
        Math3D::Vec3 savedRotationEuler = savedCameraTransform.rotation.ToEuler();
        JsonUtils::TryGetVec3(cameraTransformObj, "rotationEuler", savedRotationEuler);
        savedCameraTransform.setRotation(savedRotationEuler);
        JsonUtils::TryGetVec3(cameraTransformObj, "scale", savedCameraTransform.scale);
    }

    float savedEditorYaw = editorYaw;
    float savedEditorPitch = editorPitch;
    JsonUtils::TryGetFloat(payload, "editorYaw", savedEditorYaw);
    JsonUtils::TryGetFloat(payload, "editorPitch", savedEditorPitch);

    if(!lastLoadedScenePath.empty()){
        std::filesystem::path sourcePath(lastLoadedScenePath);
        std::error_code sourceEc;
        if(std::filesystem::exists(sourcePath, sourceEc) && !std::filesystem::is_directory(sourcePath, sourceEc)){
            loadSceneFromAbsolutePath(sourcePath);
        }
    }

    bool loadedSnapshot = false;
    if(!sceneEditsJson.empty()){
        JsonSchema::SceneSchema sceneSchema;
        std::string sceneError;
        if(sceneSchema.LoadFromText(sceneEditsJson, &sceneError) && applyEditSnapshot(sceneSchema, &sceneError)){
            resetEditHistoryToCurrentScene();
            loadedSnapshot = true;
        }else{
            LogBot.Log(LOG_WARN, "Failed to restore editor session scene edits: %s", sceneError.c_str());
        }
    }

    if(!loadedSnapshot){
        resetEditHistoryToCurrentScene();
    }

    showSceneGrid = savedShowSceneGrid;
    showSceneGizmos = savedShowSceneGizmos;
    showScenePerformanceInfo = savedShowScenePerformanceInfo;
    propertiesPanel.applyState(propertiesState);

    if(hasSavedCameraTransform){
        if(editorCamera){
            editorCamera->setTransform(savedCameraTransform);
        }
        if(editorCameraTransform){
            editorCameraTransform->local = savedCameraTransform;
        }
        editorYaw = savedEditorYaw;
        editorPitch = savedEditorPitch;
        editorMoveVelocity = Math3D::Vec3::zero();
        editorZoomVelocity = 0.0f;
        viewportCamera = editorCamera ? editorCamera : viewportCamera;
    }

    lastObservedPropertiesPanelState = propertiesPanel.captureState();
    observedPropertiesPanelStateValid = true;
    lastObservedShowSceneGrid = showSceneGrid;
    lastObservedShowSceneGizmos = showSceneGizmos;
    lastObservedShowScenePerformanceInfo = showScenePerformanceInfo;
    observedSceneViewStateValid = true;
    if(hasSavedCameraTransform){
        lastObservedEditorCameraTransform = savedCameraTransform;
        lastObservedEditorYaw = editorYaw;
        lastObservedEditorPitch = editorPitch;
        observedEditorCameraStateValid = true;
    }else{
        observedEditorCameraStateValid = false;
    }

    editorSessionDirty = false;
    editorSessionSaveRequested = false;
    editorSessionSaveDelaySeconds = 0.0f;
    return true;
}

bool EditorScene::saveEditorCameraToPrefab(std::string* outError) const{
    if(!editorCameraObject || !editorCameraObject->gameobject()){
        if(outError){
            *outError = "Editor camera object is missing.";
        }
        return false;
    }

    const std::filesystem::path prefabPath = resolveEditorCameraPrefabPath();
    if(prefabPath.empty()){
        if(outError){
            *outError = "APPDATA is unavailable; editor camera prefab path cannot be resolved.";
        }
        return false;
    }

    PrefabIO::PrefabSaveOptions options;
    options.metadata.name = "EditorCamera";
    PScene selfScene(const_cast<EditorScene*>(this), [](Scene*) {});
    return PrefabIO::SaveEntitySubtreeToAbsolutePath(selfScene, editorCameraObject->gameobject(), prefabPath, options, outError);
}

bool EditorScene::loadEditorCameraFromPrefab(std::string* outError){
    const std::filesystem::path prefabPath = resolveEditorCameraPrefabPath();
    if(prefabPath.empty()){
        if(outError){
            *outError = "APPDATA is unavailable; editor camera prefab path cannot be resolved.";
        }
        return false;
    }

    std::error_code ec;
    if(!std::filesystem::exists(prefabPath, ec) || std::filesystem::is_directory(prefabPath, ec)){
        if(outError){
            *outError = "Editor camera prefab does not exist.";
        }
        return false;
    }

    PrefabIO::PrefabInstantiateOptions options;
    options.parent = getSceneRootGameObject();

    PrefabIO::PrefabInstantiateResult result;
    PScene selfScene(this, [](Scene*) {});
    if(!PrefabIO::InstantiateFromAbsolutePath(selfScene, prefabPath, options, &result, outError)){
        return false;
    }

    if(result.rootObjects.empty() || !result.rootObjects[0] || !result.rootObjects[0]->gameobject()){
        if(outError){
            *outError = "Loaded editor camera prefab produced no root object.";
        }
        return false;
    }

    editorCameraObject = result.rootObjects[0];
    return true;
}

bool EditorScene::createDefaultEditorCameraObject(){
    editorCameraObject = createECSGameObject("EditorCamera");
    if(!editorCameraObject){
        return false;
    }

    editorCameraObject->addComponent<TransformComponent>();
    editorCameraObject->addComponent<CameraComponent>();
    editorCameraObject->addComponent<BoundsComponent>();
    editorCameraObject->addComponent<EnvironmentComponent>();
    return true;
}

void EditorScene::syncEditorCameraEnvironmentFromActiveScene(){
    if(!editorCameraObject || !editorCameraObject->gameobject() || !getECS()){
        return;
    }

    auto* editorManager = getECS()->getComponentManager();
    NeoECS::ECSEntity* editorEntity = editorCameraObject->gameobject();
    if(!editorManager || !editorEntity){
        return;
    }

    auto* editorEnvironment = editorManager->getECSComponent<EnvironmentComponent>(editorEntity);
    if(!editorEnvironment && editorCameraObject->addComponent<EnvironmentComponent>()){
        editorEnvironment = editorManager->getECSComponent<EnvironmentComponent>(editorEntity);
    }
    if(!editorEnvironment){
        return;
    }

    bool copiedFromSceneEnvironmentComponent = false;
    if(targetScene && targetScene->getECS()){
        auto* sourceManager = targetScene->getECS()->getComponentManager();
        auto* sourceEntityManager = targetScene->getECS()->getEntityManager();
        if(sourceManager && sourceEntityManager){
            const auto& entities = sourceEntityManager->getEntities();
            for(const auto& entityPtr : entities){
                auto* entity = entityPtr.get();
                if(!entity){
                    continue;
                }

                auto* sourceEnvironment = sourceManager->getECSComponent<EnvironmentComponent>(entity);
                if(!IsComponentActive(sourceEnvironment)){
                    continue;
                }

                editorEnvironment->environmentAssetRef = sourceEnvironment->environmentAssetRef;
                editorEnvironment->loadedEnvironmentAssetRef = sourceEnvironment->loadedEnvironmentAssetRef;
                editorEnvironment->skyboxAssetRef = sourceEnvironment->skyboxAssetRef;
                editorEnvironment->loadedSkyboxAssetRef.clear();
                editorEnvironment->runtimeSkyBox.reset();
                editorEnvironment->settings = sourceEnvironment->settings;
                sanitizeEnvironmentSettings(editorEnvironment->settings);
                copiedFromSceneEnvironmentComponent = true;
                break;
            }
        }
    }

    if(copiedFromSceneEnvironmentComponent){
        return;
    }

    editorEnvironment->environmentAssetRef.clear();
    editorEnvironment->loadedEnvironmentAssetRef.clear();
    editorEnvironment->skyboxAssetRef.clear();
    editorEnvironment->loadedSkyboxAssetRef.clear();
    editorEnvironment->runtimeSkyBox.reset();

    EnvironmentSettings fallbackSettings;
    if(targetScene){
        if(auto mainScreen = targetScene->getMainScreen()){
            if(auto environment = mainScreen->getEnvironment()){
                fallbackSettings = environment->getSettings();
            }
        }
    }
    sanitizeEnvironmentSettings(fallbackSettings);
    editorEnvironment->settings = fallbackSettings;
}

void EditorScene::setEditorCameraSettingsOpen(bool open){
    if(showEditorCameraSettings == open){
        return;
    }

    if(showEditorCameraSettings && !open){
        std::string saveError;
        if(!saveEditorCameraToPrefab(&saveError)){
            LogBot.Log(LOG_WARN, "Failed to save editor camera prefab on panel close: %s", saveError.c_str());
        }
    }

    if(open){
        syncEditorCameraEnvironmentFromActiveScene();
    }

    showEditorCameraSettings = open;
}

void EditorScene::setActiveScene(PScene scene){
    if(targetScene == scene){
        return;
    }

    cancelViewportPrefabDragPreview();

    if(targetScene){
        targetScene->dispose();
    }

    targetScene = std::move(scene);
    targetInitialized = false;
    targetFactory = nullptr;
    activeScenePath.clear();
    targetCamera.reset();
    viewportCamera = editorCamera;
    selectedEntityId.clear();
    transformWidget.reset();
    cameraWidget.reset();
    boundsWidget.reset();
    BoundsEditState::Deactivate();
    previewTexture.reset();
    previewCamera.reset();
    focusActive = false;
}

void EditorScene::init(){
    assetRoot = std::filesystem::path(File::GetCWD()) / "res";
    workspacePanel.setAssetRoot(assetRoot);
    startupBootstrapPending = true;
    startupUiFramePresented = false;
    startupLoadingOverlayActive = true;
    startupSessionParsed = false;
    startupSessionHasCachedScene = false;
    startupCachedScenePath.clear();
    clearStartupCachedSceneLoadState();
}

void EditorScene::ensureTargetInitialized(){
    if(!targetScene || targetInitialized) return;

    if(getWindow()){
        targetScene->attachWindow(getWindow());
    }
    if(inputManager){
        targetScene->setInputManager(inputManager);
    }

    targetScene->init();
    applyActiveSceneState();
}

void EditorScene::applyActiveSceneState(){
    if(!targetScene){
        return;
    }

    targetInitialized = true;
    if(std::shared_ptr<LoadedScene> loadedScene = std::dynamic_pointer_cast<LoadedScene>(targetScene)){
        activeScenePath = loadedScene->getSourceScenePath();
    }
    targetScene->setOutlineEnabled(true);
    targetCamera = targetScene->getPreferredCamera();
    if(!targetCamera){
        if(auto mainScreen = targetScene->getMainScreen()){
            targetCamera = mainScreen->getCamera();
        }
    }
    selectedEntityId = targetScene->getSelectedEntityId();

    if(!editorCameraObject){
        std::string loadError;
        if(!loadEditorCameraFromPrefab(&loadError)){
            if(!createDefaultEditorCameraObject()){
                LogBot.Log(LOG_ERRO, "Failed to create default editor camera object.");
            }else{
                std::string saveError;
                if(!saveEditorCameraToPrefab(&saveError)){
                    LogBot.Log(LOG_WARN, "Failed to save default editor camera prefab: %s", saveError.c_str());
                }
            }
        }
    }

    if(editorCameraObject){
        auto* manager = getECS()->getComponentManager();
        NeoECS::ECSEntity* editorEntity = editorCameraObject->gameobject();

        if(manager && editorEntity){
            if(!manager->getECSComponent<TransformComponent>(editorEntity)){
                editorCameraObject->addComponent<TransformComponent>();
            }
            if(!manager->getECSComponent<CameraComponent>(editorEntity)){
                editorCameraObject->addComponent<CameraComponent>();
            }
            if(!manager->getECSComponent<BoundsComponent>(editorEntity)){
                editorCameraObject->addComponent<BoundsComponent>();
            }
            if(!manager->getECSComponent<EnvironmentComponent>(editorEntity)){
                editorCameraObject->addComponent<EnvironmentComponent>();
            }

            // The editor viewport camera always exposes all post-process components,
            // but starts with every effect disabled.
            if(!manager->getECSComponent<SSAOComponent>(editorEntity) && editorCameraObject->addComponent<SSAOComponent>()){
                if(auto* ssao = manager->getECSComponent<SSAOComponent>(editorEntity)){
                    SetComponentActive(ssao, false);
                }
            }
            if(!manager->getECSComponent<BloomComponent>(editorEntity) && editorCameraObject->addComponent<BloomComponent>()){
                if(auto* bloom = manager->getECSComponent<BloomComponent>(editorEntity)){
                    SetComponentActive(bloom, false);
                }
            }
            if(!manager->getECSComponent<LensFlareComponent>(editorEntity) && editorCameraObject->addComponent<LensFlareComponent>()){
                if(auto* lensFlare = manager->getECSComponent<LensFlareComponent>(editorEntity)){
                    SetComponentActive(lensFlare, true);
                }
            }else if(auto* lensFlare = manager->getECSComponent<LensFlareComponent>(editorEntity)){
                // Edit mode renders through the editor camera, so keep flare preview enabled there.
                SetComponentActive(lensFlare, true);
            }
            if(!manager->getECSComponent<AutoExposureComponent>(editorEntity) && editorCameraObject->addComponent<AutoExposureComponent>()){
                if(auto* autoExposure = manager->getECSComponent<AutoExposureComponent>(editorEntity)){
                    SetComponentActive(autoExposure, false);
                }
            }
            if(!manager->getECSComponent<DepthOfFieldComponent>(editorEntity) && editorCameraObject->addComponent<DepthOfFieldComponent>()){
                if(auto* dof = manager->getECSComponent<DepthOfFieldComponent>(editorEntity)){
                    SetComponentActive(dof, false);
                }
            }
            if(!manager->getECSComponent<AntiAliasingComponent>(editorEntity) && editorCameraObject->addComponent<AntiAliasingComponent>()){
                if(auto* aa = manager->getECSComponent<AntiAliasingComponent>(editorEntity)){
                    aa->preset = AntiAliasingPreset::Off;
                }
            }
        }

        if(!editorCameraComponent && manager && editorEntity){
            editorCameraTransform = manager->getECSComponent<TransformComponent>(editorEntity);
            editorCameraComponent = manager->getECSComponent<CameraComponent>(editorEntity);
            auto* boundsComp = manager->getECSComponent<BoundsComponent>(editorEntity);
            if(editorCameraComponent && !editorCameraComponent->camera){
                RenderWindow* window = getWindow();
                float w = window ? (float)window->getWindowWidth() : 1280.0f;
                float h = window ? (float)window->getWindowHeight() : 720.0f;
                editorCameraComponent->camera = Camera::CreatePerspective(45.0f, Math3D::Vec2(w, h), 0.1f, 2000.0f);
            }

            if(editorCameraComponent){
                editorCamera = editorCameraComponent->camera;
                viewportCamera = editorCamera;
            }

            if(boundsComp){
                boundsComp->type = BoundsType::Sphere;
                boundsComp->radius = 0.5f;
            }

            if(targetCamera && editorCameraTransform){
                editorCameraTransform->local = targetCamera->transform();
                Math3D::Vec3 euler = targetCamera->transform().rotation.ToEuler();
                editorPitch = euler.x;
                editorYaw = euler.y;
            }
        }
    }
}

bool EditorScene::parseStartupSessionState(std::string* outError){
    startupSessionHasCachedScene = false;
    startupCachedScenePath.clear();
    startupSessionPropertiesState = propertiesPanel.captureState();
    startupSessionHasCameraTransform = false;
    startupSessionCameraTransform = Math3D::Transform();
    startupSessionEditorYaw = editorYaw;
    startupSessionEditorPitch = editorPitch;
    startupSessionShowSceneGrid = showSceneGrid;
    startupSessionShowSceneGizmos = showSceneGizmos;
    startupSessionShowScenePerformanceInfo = showScenePerformanceInfo;

    const std::filesystem::path sessionPath = resolveEditorSessionPath();
    if(sessionPath.empty()){
        if(outError){
            *outError = "APPDATA is unavailable; editor session path cannot be resolved.";
        }
        return false;
    }

    std::error_code ec;
    if(!std::filesystem::exists(sessionPath, ec) || std::filesystem::is_directory(sessionPath, ec)){
        return false;
    }

    JsonUtils::Document doc;
    if(!JsonUtils::LoadDocumentFromAbsolutePath(sessionPath, doc, outError)){
        return false;
    }

    std::string type;
    int version = 0;
    JsonUtils::JsonVal* payload = nullptr;
    if(!JsonUtils::ReadStandardDocumentHeader(doc, type, version, &payload, outError)){
        return false;
    }
    if(type != kEditorSessionDocumentType){
        if(outError){
            *outError = "Editor session type mismatch.";
        }
        return false;
    }
    if(version != kEditorSessionDocumentVersion){
        if(outError){
            *outError = "Unsupported editor session version: " + std::to_string(version);
        }
        return false;
    }
    if(!payload || !yyjson_is_obj(payload)){
        if(outError){
            *outError = "Editor session payload must be an object.";
        }
        return false;
    }

    std::string lastLoadedScenePath;
    JsonUtils::TryGetString(payload, "lastLoadedScenePath", lastLoadedScenePath);

    if(JsonUtils::JsonVal* propertiesObj = JsonUtils::ObjGetObject(payload, "propertiesPanel")){
        JsonUtils::TryGetBool(propertiesObj, "showHiddenComponents", startupSessionPropertiesState.showHiddenComponents);
    }
    if(JsonUtils::JsonVal* sceneViewObj = JsonUtils::ObjGetObject(payload, "sceneView")){
        JsonUtils::TryGetBool(sceneViewObj, "showGrid", startupSessionShowSceneGrid);
        JsonUtils::TryGetBool(sceneViewObj, "showGizmos", startupSessionShowSceneGizmos);
        JsonUtils::TryGetBool(sceneViewObj, "showPerformanceInfo", startupSessionShowScenePerformanceInfo);
    }

    if(JsonUtils::JsonVal* cameraTransformObj = JsonUtils::ObjGetObject(payload, "editorCameraTransform")){
        startupSessionHasCameraTransform = true;
        JsonUtils::TryGetVec3(cameraTransformObj, "position", startupSessionCameraTransform.position);
        Math3D::Vec3 savedRotationEuler = startupSessionCameraTransform.rotation.ToEuler();
        JsonUtils::TryGetVec3(cameraTransformObj, "rotationEuler", savedRotationEuler);
        startupSessionCameraTransform.setRotation(savedRotationEuler);
        JsonUtils::TryGetVec3(cameraTransformObj, "scale", startupSessionCameraTransform.scale);
    }

    JsonUtils::TryGetFloat(payload, "editorYaw", startupSessionEditorYaw);
    JsonUtils::TryGetFloat(payload, "editorPitch", startupSessionEditorPitch);

    if(!lastLoadedScenePath.empty()){
        std::filesystem::path sourcePath(lastLoadedScenePath);
        std::error_code sourceEc;
        if(std::filesystem::exists(sourcePath, sourceEc) &&
           !std::filesystem::is_directory(sourcePath, sourceEc)){
            startupSessionHasCachedScene = true;
            startupCachedScenePath = sourcePath.lexically_normal();
        }
    }

    return true;
}

void EditorScene::applyStartupSessionUiState(){
    showSceneGrid = startupSessionShowSceneGrid;
    showSceneGizmos = startupSessionShowSceneGizmos;
    showScenePerformanceInfo = startupSessionShowScenePerformanceInfo;
    propertiesPanel.applyState(startupSessionPropertiesState);

    if(startupSessionHasCameraTransform){
        if(editorCamera){
            editorCamera->setTransform(startupSessionCameraTransform);
        }
        if(editorCameraTransform){
            editorCameraTransform->local = startupSessionCameraTransform;
        }
        editorYaw = startupSessionEditorYaw;
        editorPitch = startupSessionEditorPitch;
        editorMoveVelocity = Math3D::Vec3::zero();
        editorZoomVelocity = 0.0f;
        viewportCamera = editorCamera ? editorCamera : viewportCamera;
    }

    lastObservedPropertiesPanelState = propertiesPanel.captureState();
    observedPropertiesPanelStateValid = true;
    lastObservedShowSceneGrid = showSceneGrid;
    lastObservedShowSceneGizmos = showSceneGizmos;
    lastObservedShowScenePerformanceInfo = showScenePerformanceInfo;
    observedSceneViewStateValid = true;
    if(startupSessionHasCameraTransform){
        lastObservedEditorCameraTransform = startupSessionCameraTransform;
        lastObservedEditorYaw = editorYaw;
        lastObservedEditorPitch = editorPitch;
        observedEditorCameraStateValid = true;
    }else{
        observedEditorCameraStateValid = false;
    }

    editorSessionDirty = false;
    editorSessionSaveRequested = false;
    editorSessionSaveDelaySeconds = 0.0f;
}

void EditorScene::clearStartupCachedSceneLoadState(){
    startupLoadPhase = StartupLoadPhase::None;
    startupLoadScene.reset();
    startupLoadScenePath.clear();
    startupLoadSchema.Clear();
    startupLoadRecordsById.clear();
    startupLoadPendingRecords.clear();
    startupLoadGameObjectsBySnapshotId.clear();
    startupLoadEntitiesBySnapshotId.clear();
    startupLoadDeserializeIndex = 0;
}

bool EditorScene::beginStartupCachedSceneLoad(const std::filesystem::path& scenePath, std::string* outError){
    clearStartupCachedSceneLoadState();

    if(scenePath.empty()){
        if(outError){
            *outError = "Startup cached scene path is empty.";
        }
        return false;
    }

    std::filesystem::path normalizedPath = scenePath.lexically_normal();
    if(!isPathWithExtension(normalizedPath, ".scene")){
        normalizedPath += ".scene";
    }

    std::error_code ec;
    if(!std::filesystem::exists(normalizedPath, ec) || std::filesystem::is_directory(normalizedPath, ec)){
        if(outError){
            *outError = "Startup cached scene does not exist.";
        }
        return false;
    }

    const std::filesystem::path sceneDirectory = normalizedPath.parent_path();
    startupLoadScene = std::make_shared<LoadedScene>(getWindow(), normalizedPath.generic_string(), sceneDirectory);
    if(!startupLoadScene){
        if(outError){
            *outError = "Failed to allocate startup cached scene.";
        }
        return false;
    }
    if(getWindow()){
        startupLoadScene->attachWindow(getWindow());
    }
    if(inputManager){
        startupLoadScene->setInputManager(inputManager);
    }
    if(auto mainScreen = startupLoadScene->getMainScreen()){
        if(auto env = mainScreen->getEnvironment()){
            env->setLightingEnabled(true);
        }
    }

    if(!startupLoadSchema.LoadFromAbsolutePath(normalizedPath, outError)){
        clearStartupCachedSceneLoadState();
        return false;
    }

    startupLoadRecordsById.reserve(startupLoadSchema.entities.size());
    startupLoadPendingRecords.reserve(startupLoadSchema.entities.size());
    for(const auto& record : startupLoadSchema.entities){
        if(record.id == 0){
            if(outError){
                *outError = "Snapshot entity has invalid id (0).";
            }
            clearStartupCachedSceneLoadState();
            return false;
        }
        if(startupLoadRecordsById.find(record.id) != startupLoadRecordsById.end()){
            if(outError){
                *outError = "Snapshot contains duplicate entity id: " + std::to_string(record.id);
            }
            clearStartupCachedSceneLoadState();
            return false;
        }
        startupLoadRecordsById[record.id] = &record;
        startupLoadPendingRecords.push_back(&record);
    }

    startupLoadGameObjectsBySnapshotId.reserve(startupLoadSchema.entities.size());
    startupLoadEntitiesBySnapshotId.reserve(startupLoadSchema.entities.size());
    startupLoadScenePath = normalizedPath;
    startupLoadDeserializeIndex = 0;
    startupLoadPhase = StartupLoadPhase::CreateEntities;
    return true;
}

bool EditorScene::stepStartupCachedSceneLoad(std::string* outError){
    if(startupLoadPhase == StartupLoadPhase::None || !startupLoadScene || !startupLoadScene->getECS()){
        return false;
    }

    if(startupLoadPhase == StartupLoadPhase::CreateEntities){
        constexpr size_t kCreateBudgetPerUpdate = 64;
        size_t createdThisUpdate = 0;
        while(createdThisUpdate < kCreateBudgetPerUpdate && !startupLoadPendingRecords.empty()){
            bool progressed = false;
            for(size_t i = 0; i < startupLoadPendingRecords.size() && createdThisUpdate < kCreateBudgetPerUpdate;){
                const auto* record = startupLoadPendingRecords[i];
                NeoECS::GameObject* parentObject = nullptr;
                if(record->hasParentId){
                    auto parentCreated = startupLoadGameObjectsBySnapshotId.find(record->parentId);
                    if(parentCreated == startupLoadGameObjectsBySnapshotId.end()){
                        auto parentExists = startupLoadRecordsById.find(record->parentId);
                        if(parentExists == startupLoadRecordsById.end()){
                            if(outError){
                                *outError = "Snapshot entity references missing parent id: " + std::to_string(record->parentId);
                            }
                            return false;
                        }
                        ++i;
                        continue;
                    }
                    parentObject = parentCreated->second;
                }

                const std::string entityName = record->name.empty() ? std::string("GameObject") : record->name;
                NeoECS::GameObject* createdObject = startupLoadScene->createECSGameObject(entityName, parentObject);
                if(!createdObject || !createdObject->gameobject()){
                    if(outError){
                        *outError = "Failed to instantiate startup snapshot entity: " + entityName;
                    }
                    return false;
                }

                startupLoadGameObjectsBySnapshotId[record->id] = createdObject;
                startupLoadEntitiesBySnapshotId[record->id] = createdObject->gameobject();
                startupLoadPendingRecords.erase(startupLoadPendingRecords.begin() + static_cast<std::ptrdiff_t>(i));
                ++createdThisUpdate;
                progressed = true;
            }

            if(!progressed){
                if(outError){
                    *outError = "Failed to resolve parent dependencies while instantiating startup snapshot.";
                }
                return false;
            }
        }

        if(startupLoadPendingRecords.empty()){
            startupLoadPhase = StartupLoadPhase::DeserializeComponents;
        }
        return true;
    }

    if(startupLoadPhase == StartupLoadPhase::DeserializeComponents){
        auto* manager = startupLoadScene->getECS()->getComponentManager();
        auto* context = startupLoadScene->getECS()->getContext();
        if(!manager || !context){
            if(outError){
                *outError = "Startup cached scene ECS is unavailable.";
            }
            return false;
        }

        constexpr size_t kDeserializeBudgetPerUpdate = 1;
        size_t deserializedThisUpdate = 0;
        const auto& registry = Serialization::DefaultComponentSerializationRegistry();
        while(deserializedThisUpdate < kDeserializeBudgetPerUpdate &&
              startupLoadDeserializeIndex < startupLoadSchema.entities.size()){
            const auto& record = startupLoadSchema.entities[startupLoadDeserializeIndex];
            auto entityIt = startupLoadEntitiesBySnapshotId.find(record.id);
            if(entityIt == startupLoadEntitiesBySnapshotId.end() || !entityIt->second){
                if(outError){
                    *outError = "Missing runtime entity for startup snapshot id: " + std::to_string(record.id);
                }
                return false;
            }

            if(!registry.deserializeEntityComponents(
                    context,
                    manager,
                    entityIt->second,
                    record.components,
                    outError)){
                if(outError && !outError->empty()){
                    *outError = "Entity '" + record.name + "' component restore failed: " + *outError;
                }
                return false;
            }

            ++startupLoadDeserializeIndex;
            ++deserializedThisUpdate;
        }

        if(startupLoadDeserializeIndex >= startupLoadSchema.entities.size()){
            startupLoadPhase = StartupLoadPhase::Finalize;
        }
        return true;
    }

    if(startupLoadPhase == StartupLoadPhase::Finalize){
        PScene loadedScene = std::static_pointer_cast<Scene>(startupLoadScene);
        if(!applySceneSettingsRawJsonLocal(loadedScene, startupLoadSchema.sceneSettings, startupLoadEntitiesBySnapshotId, outError)){
            return false;
        }
        ensurePreferredCameraAfterSceneLoad(loadedScene);
        startupLoadScene->setSourceScenePath(startupLoadScenePath);

        std::error_code ec;
        if(std::filesystem::exists(startupLoadScenePath, ec) &&
           !ec &&
           !std::filesystem::is_directory(startupLoadScenePath, ec) &&
           !ec){
            AssetManager::Instance.unmanageAsset(startupLoadScenePath.generic_string());
            startupLoadScene->setSourceSceneAsset(AssetManager::Instance.getOrLoad(startupLoadScenePath.generic_string()));
        }

        const std::filesystem::path normalizedPath = startupLoadScenePath.lexically_normal();
        const std::filesystem::path sceneDirectory = normalizedPath.parent_path();
        std::shared_ptr<LoadedScene> readyScene = startupLoadScene;
        setActiveScene(readyScene);
        targetInitialized = true;
        targetFactory = [normalizedPath, sceneDirectory](RenderWindow* window) -> PScene {
            return std::make_shared<LoadedScene>(window, normalizedPath.generic_string(), sceneDirectory);
        };
        applyActiveSceneState();
        activeScenePath = normalizedPath;
        selectedAssetPath = normalizedPath;
        resetEditHistoryToCurrentScene();
        clearStartupCachedSceneLoadState();
        return true;
    }

    return false;
}

void EditorScene::setInputManager(std::shared_ptr<InputManager> manager){
    inputManager = manager;

    if(inputManager && !inputBlockerRegistered){
        auto shouldBlock = [this](){
            ImGuiIO& io = ImGui::GetIO();
            if(playState != PlayState::Play) return true;
            if(inputManager && inputManager->getMouseCaptureMode() == MouseLockMode::LOCKED){
                return false;
            }
            if(isMouseInViewport()) return false;
            return io.WantCaptureMouse || io.WantCaptureKeyboard;
        };
        inputBlocker = std::make_shared<EditorInputBlocker>(this, shouldBlock);
        inputManager->addEventHandler(inputBlocker);
        inputBlockerRegistered = true;
    }

    if(targetScene){
        targetScene->setInputManager(manager);
    }
}

void EditorScene::update(float deltaTime){
    if(startupBootstrapPending){
        if(!startupUiFramePresented){
            return;
        }

        startupLoadingOverlayActive = true;

        if(!startupSessionParsed){
            startupSessionParsed = true;
            std::string sessionError;
            if(!parseStartupSessionState(&sessionError) && !sessionError.empty()){
                LogBot.Log(LOG_WARN, "Failed to restore editor session: %s", sessionError.c_str());
            }

            if(startupSessionHasCachedScene){
                std::string loadError;
                if(!beginStartupCachedSceneLoad(startupCachedScenePath, &loadError)){
                    if(!loadError.empty()){
                        LogBot.Log(LOG_WARN, "Failed to begin cached startup scene load: %s", loadError.c_str());
                    }
                    clearStartupCachedSceneLoadState();
                }
            }
        }

        if(startupLoadPhase != StartupLoadPhase::None){
            std::string loadError;
            if(!stepStartupCachedSceneLoad(&loadError)){
                if(!loadError.empty()){
                    LogBot.Log(LOG_WARN, "Cached startup scene load failed: %s", loadError.c_str());
                }
                clearStartupCachedSceneLoadState();
            }
            if(startupLoadPhase != StartupLoadPhase::None){
                return;
            }
        }

        if(!targetInitialized){
            ensureTargetInitialized();
        }

        resetEditHistoryToCurrentScene();
        applyStartupSessionUiState();

        startupBootstrapPending = false;
        startupLoadingOverlayActive = false;
        return;
    }

    ensureTargetInitialized();
    if(!targetScene) return;

    advanceEditorSessionAutosave(deltaTime);

    if(ioStatusTimeRemaining > 0.0f){
        ioStatusTimeRemaining = std::max(0.0f, ioStatusTimeRemaining - deltaTime);
        if(ioStatusTimeRemaining <= 0.0f){
            ioStatusMessage.clear();
            ioStatusIsError = false;
        }
    }

    if(playState == PlayState::Play){
        playModePanelRefreshAccum += deltaTime;
        if(playModePanelRefreshAccum >= playModePanelRefreshInterval){
            playModePanelRefreshAccum = 0.0f;
            playModeHeavyPanelsRefreshDue = true;
        }
    }else{
        playModePanelRefreshAccum = 0.0f;
        playModeHeavyPanelsRefreshDue = true;
    }

    if(auto preferred = targetScene->getPreferredCamera()){
        if(preferred != targetCamera){
            targetCamera = preferred;
        }
    }

    if(resetCompleted.exchange(false)){
        restoreSelectionAfterReset();
    }

    if(playState == PlayState::Play){
        if(maximizeOnPlay){
            if(auto* window = getWindow()){
                viewportRect.x = 0.0f;
                viewportRect.y = 0.0f;
                viewportRect.w = (float)window->getWindowWidth();
                viewportRect.h = (float)window->getWindowHeight();
                viewportRect.valid = (viewportRect.w > 1.0f && viewportRect.h > 1.0f);
                viewportHovered = true;
            }
        }

        if(auto* window = getWindow()){
            bool shouldConstrainViewportMouse = false;
            MouseLockMode mouseMode = MouseLockMode::FREE;
            if(inputManager){
                mouseMode = inputManager->getMouseCaptureMode();
                shouldConstrainViewportMouse = (mouseMode != MouseLockMode::FREE) && viewportRect.valid;
            }

            if(shouldConstrainViewportMouse){
                const int rectX = (int)std::floor(viewportRect.x);
                const int rectY = (int)std::floor(viewportRect.y);
                const int rectW = std::max(1, (int)std::ceil(viewportRect.w));
                const int rectH = std::max(1, (int)std::ceil(viewportRect.h));
                SDL_Rect mouseRect{rectX, rectY, rectW, rectH};
                SDL_SetWindowMouseRect(window->getWindowPtr(), &mouseRect);
                playViewportMouseRectConstrained = true;

                // For visible captured cursor mode, clamp immediately in case capture began outside the viewport.
                if(mouseMode == MouseLockMode::CAPTURED){
                    float mx = 0.0f;
                    float my = 0.0f;
                    SDL_GetMouseState(&mx, &my);
                    const float minX = (float)rectX;
                    const float minY = (float)rectY;
                    const float maxX = (float)(rectX + rectW - 1);
                    const float maxY = (float)(rectY + rectH - 1);
                    const float clampedX = Math3D::Clamp(mx, minX, maxX);
                    const float clampedY = Math3D::Clamp(my, minY, maxY);
                    if(!Math3D::AreClose(mx, clampedX) || !Math3D::AreClose(my, clampedY)){
                        SDL_WarpMouseInWindow(window->getWindowPtr(), clampedX, clampedY);
                    }
                }
            }else if(playViewportMouseRectConstrained){
                SDL_SetWindowMouseRect(window->getWindowPtr(), nullptr);
                playViewportMouseRectConstrained = false;
            }
        }

        viewportCamera = targetCamera ? targetCamera : editorCamera;
        if(auto mainScreen = targetScene->getMainScreen()){
            if(viewportCamera){
                mainScreen->setCamera(viewportCamera);
            }
        }
        bool mouseInViewport = isMouseInViewport();
        if(inputManager && inputManager->getMouseCaptureMode() != MouseLockMode::FREE){
            mouseInViewport = true;
        }
        ImGuiLayer::SetInputEnabled(!mouseInViewport);
        if(targetScene->consumeCloseRequest()){
            handleQuitRequest();
            return;
        }
        targetScene->updateECS(deltaTime);
        targetScene->update(deltaTime);
    }else{
        if(playViewportMouseRectConstrained){
            if(auto* window = getWindow()){
                SDL_SetWindowMouseRect(window->getWindowPtr(), nullptr);
            }
            playViewportMouseRectConstrained = false;
        }
        if(inputManager){
            bool rmb = inputManager->isRMBDown();
            bool rmbPressed = rmb && !prevRmb;
            prevRmb = rmb;
            bool mouseInViewport = isMouseInViewport();
            bool mouseOverPreviewWindow = false;
            if(playState == PlayState::Edit && previewCamera && viewportRect.valid){
                Math3D::Vec2 mousePos = inputManager->getMousePosition();
                float previewAbsX = viewportRect.x + previewWindowLocalPos.x;
                float previewAbsY = viewportRect.y + previewWindowLocalPos.y;
                float previewW = previewWindowSize.x;
                float previewH = previewWindowSize.y;
                mouseOverPreviewWindow =
                    (mousePos.x >= previewAbsX && mousePos.x <= (previewAbsX + previewW) &&
                     mousePos.y >= previewAbsY && mousePos.y <= (previewAbsY + previewH));
            }
            ImGuiIO& io = ImGui::GetIO();
            const bool uiOverlayCapturingMouse = io.WantCaptureMouse && !viewportHovered;
            bool mouseInViewportInteractive = mouseInViewport && !mouseOverPreviewWindow && !uiOverlayCapturingMouse;
            bool lmb = inputManager->isLMBDown();
            bool lmbPressed = lmb && !prevLmb;
            bool lmbReleased = !lmb && prevLmb;
            prevLmb = lmb;

            if(!rmb){
                editorCameraActive = false;
            }else if(rmbPressed && mouseInViewportInteractive){
                editorCameraActive = true;
            }

            bool allowControl = editorCameraActive && rmb;
            inputManager->setMouseCaptureMode(allowControl ? MouseLockMode::LOCKED : MouseLockMode::FREE);
            ImGuiLayer::SetInputEnabled(!allowControl);

            viewportCamera = editorCamera;

            if(!allowControl){
                inputManager->consumeMouseAxisDelta();
            }

            if(editorCamera && allowControl){
                focusActive = false;
                if(viewportRect.valid){
                    int cx = (int)(viewportRect.x + (viewportRect.w * 0.5f));
                    int cy = (int)(viewportRect.y + (viewportRect.h * 0.5f));
                    RenderWindow* window = getWindow();
                    if(window){
                        SDL_WarpMouseInWindow(window->getWindowPtr(), (float)cx, (float)cy);
                    }
                }
                Math3D::Vec2 delta = inputManager->consumeMouseAxisDelta();
                float dx = delta.x * editorLookSensitivity;
                float dy = -delta.y * editorLookSensitivity;

                editorYaw -= dx;
                editorPitch += dy;
                editorPitch = Math3D::Clamp(editorPitch, -89.0f, 89.0f);

                auto transform = editorCamera->transform();
                transform.rotation = Math3D::Quat::FromEuler(Math3D::Vec3(editorPitch, editorYaw, 0.0f));

                float moveSpeed = editorMoveSpeed;
                if(inputManager->isKeyDown(SDL_SCANCODE_LSHIFT)){
                    moveSpeed *= editorFastScale;
                }

                Math3D::Vec3 moveDir = Math3D::Vec3::zero();
                if(inputManager->isKeyDown(SDL_SCANCODE_W)) moveDir -= transform.forward();
                if(inputManager->isKeyDown(SDL_SCANCODE_S)) moveDir += transform.forward();
                if(inputManager->isKeyDown(SDL_SCANCODE_A)) moveDir -= transform.right();
                if(inputManager->isKeyDown(SDL_SCANCODE_D)) moveDir += transform.right();
                if(inputManager->isKeyDown(SDL_SCANCODE_LCTRL)) moveDir -= transform.up();
                if(inputManager->isKeyDown(SDL_SCANCODE_SPACE)) moveDir += transform.up();

                if(moveDir.length() > Math3D::EPSILON){
                    moveDir = moveDir.normalize();
                }

                Math3D::Vec3 desiredMoveVelocity = moveDir * moveSpeed;
                float moveAlpha = 1.0f - std::exp(-editorMoveSmoothing * deltaTime);
                editorMoveVelocity = Math3D::Lerp(editorMoveVelocity, desiredMoveVelocity, moveAlpha);
                transform.position += editorMoveVelocity * deltaTime;

                float scrollDelta = inputManager->consumeScrollDelta();
                if(!Math3D::AreClose(scrollDelta, 0.0f)){
                    float zoomImpulse = editorZoomImpulse;
                    if(inputManager->isKeyDown(SDL_SCANCODE_LSHIFT)){
                        zoomImpulse *= editorFastScale;
                    }
                    editorZoomVelocity += scrollDelta * zoomImpulse;
                }

                if(!Math3D::AreClose(editorZoomVelocity, 0.0f)){
                    transform.position -= transform.forward() * (editorZoomVelocity * deltaTime);
                }

                float zoomAlpha = 1.0f - std::exp(-editorZoomDamping * deltaTime);
                editorZoomVelocity = Math3D::Lerp(editorZoomVelocity, 0.0f, zoomAlpha);

                editorCamera->setTransform(transform);
            }

            if(editorCameraTransform && editorCamera){
                editorCameraTransform->local = editorCamera->transform();
            }

            if(!allowControl && playState == PlayState::Edit){
                bool wDown = inputManager->isKeyDown(SDL_SCANCODE_W);
                bool eDown = inputManager->isKeyDown(SDL_SCANCODE_E);
                bool rDown = inputManager->isKeyDown(SDL_SCANCODE_R);

                if(wDown && !prevKeyW){
                    transformWidget.setMode(TransformWidget::Mode::Translate);
                }
                if(eDown && !prevKeyE){
                    transformWidget.setMode(TransformWidget::Mode::Rotate);
                }
                if(rDown && !prevKeyR){
                    transformWidget.setMode(TransformWidget::Mode::Scale);
                }

                prevKeyW = wDown;
                prevKeyE = eDown;
                prevKeyR = rDown;
            }

            bool widgetConsumed = false;
            if(playState == PlayState::Edit && viewportCamera && targetScene && targetScene->getECS()){
                auto* entity = findEntityById(selectedEntityId); // Maybe refactor this later to allow light widgets to always draw on entities with light components.
                if(entity){
                    auto* components = targetScene->getECS()->getComponentManager();
                    if(auto* transformComp = components->getECSComponent<TransformComponent>(entity)){
                        Math3D::Mat4 world = buildWorldMatrix(entity, components);
                        Math3D::Vec3 worldPos = world.getPosition();
                        Math3D::Transform worldTx = Math3D::Transform::fromMat4(world);
                        Math3D::Vec3 worldForward = worldTx.forward();
                        TransformWidget::Viewport viewport{viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, viewportRect.valid};
                        auto* boundsComp = components->getECSComponent<BoundsComponent>(entity);
                        const bool boundsModeActive = boundsComp && BoundsEditState::IsActiveForEntity(selectedEntityId);

                        if(boundsModeActive){
                            widgetConsumed = boundsWidget.update(
                                this,
                                inputManager.get(),
                                viewportCamera,
                                viewport,
                                world,
                                *boundsComp,
                                mouseInViewportInteractive && !allowControl,
                                lmbPressed,
                                lmb,
                                lmbReleased
                            );
                        }else{
                            widgetConsumed = transformWidget.update(
                                this,
                                inputManager.get(),
                                viewportCamera,
                                viewport,
                                worldPos,
                                transformComp->local,
                                mouseInViewportInteractive && !allowControl,
                                lmbPressed,
                                lmb,
                                lmbReleased
                            );
                            if(auto* lightComp = components->getECSComponent<LightComponent>(entity)){
                                if(!widgetConsumed){
                                    widgetConsumed = lightWidget.update(
                                        this,
                                        inputManager.get(),
                                        viewportCamera,
                                        viewport,
                                        worldPos,
                                        worldForward,
                                        lightComp->light,
                                        lightComp->syncTransform,
                                        lightComp->syncDirection,
                                        mouseInViewportInteractive && !allowControl,
                                        lmbPressed,
                                        lmb,
                                        lmbReleased
                                    );
                                    if(lightComp->light.type == LightType::POINT){
                                        ensurePointLightBounds(entity, lightComp->light.range);
                                    }
                                }
                            }
                            if(auto* cameraComp = components->getECSComponent<CameraComponent>(entity)){
                                if(!widgetConsumed && cameraComp->camera){
                                    widgetConsumed = cameraWidget.update(
                                        this,
                                        inputManager.get(),
                                        viewportCamera,
                                        viewport,
                                        worldPos,
                                        worldForward,
                                        cameraComp->camera->getSettings(),
                                        mouseInViewportInteractive && !allowControl,
                                        lmbPressed,
                                        lmb,
                                        lmbReleased
                                    );
                                }
                            }
                        }
                    }
                }
            }

            if(playState != PlayState::Play && lmbPressed && mouseInViewportInteractive && !allowControl){
                if(viewportCamera){
                    Math3D::Vec2 mouse = inputManager->getMousePosition();
                    std::string picked = pickEntityIdAtScreen(mouse.x, mouse.y, viewportCamera);
                    if(!picked.empty() && picked != selectedEntityId){
                        selectEntity(picked);
                    }else if(!picked.empty() && !widgetConsumed){
                        // Keep viewport behavior aligned with the ECS tree:
                        // clicking an already-selected entity focuses/frames it.
                        selectEntity(picked);
                    }else if(picked.empty() && !widgetConsumed){
                        selectEntity("");
                    }
                }
            }
        }else{
            viewportCamera = editorCamera;
        }

        if(focusActive){
            if(viewportCamera && editorCamera && viewportCamera == editorCamera){
                auto transform = editorCamera->transform();
                Math3D::Vec3 desired = focusTarget - (focusForward * focusDistance);
                Math3D::Vec3 pos = transform.position;
                float t = Math3D::Clamp(deltaTime * focusSpeed, 0.0f, 1.0f);
                pos = pos + (desired - pos) * t;
                transform.setPosition(pos);
                editorCamera->setTransform(transform);
                if(editorCameraTransform){
                    editorCameraTransform->local = transform;
                }
            }else{
                focusActive = false;
            }
        }

        if(viewportCamera && viewportRect.valid){
            viewportCamera->resize(viewportRect.w, viewportRect.h);
        }

        if(auto mainScreen = targetScene->getMainScreen()){
            if(viewportCamera){
                mainScreen->setCamera(viewportCamera);
            }
        }

        targetScene->refreshRenderState();
    }
}

void EditorScene::render(){
    if(!startupBootstrapPending){
        ensureTargetInitialized();
    }
    if(resetRequested.exchange(false)){
        performStop();
        resetCompleted.store(true);
        return;
    }
    if(targetScene && targetInitialized){
        if(playState == PlayState::Edit){
            previewCamera = resolveSelectedTargetCamera();
            auto mainScreen = targetScene->getMainScreen();
            NeoECS::ECSEntity* previewEntity = findEntityById(selectedEntityId);

            auto applyCameraEffects = [&](PScene effectScene, NeoECS::ECSEntity* cameraEntity){
                if(!mainScreen || !effectScene){
                    return;
                }
                effectScene->applyCameraEffectsToScreen(mainScreen, cameraEntity, true, targetScene.get());
            };

            // Render the selected target-scene camera into a small preview texture first.
            if(mainScreen && previewCamera && previewEntity){
                applyCameraEffects(targetScene, previewEntity);
                mainScreen->setCamera(previewCamera);
                targetScene->renderViewportContents();
                auto sourceBuffer = mainScreen->getDisplayBuffer();
                if(sourceBuffer){
                    int srcW = sourceBuffer->getWidth();
                    int srcH = sourceBuffer->getHeight();
                    if(srcW > 0 && srcH > 0){
                        bool needsResize = !previewCaptureBuffer ||
                                           !previewTexture ||
                                           previewCaptureBuffer->getWidth() != srcW ||
                                           previewCaptureBuffer->getHeight() != srcH ||
                                           previewTexture->getWidth() != srcW ||
                                           previewTexture->getHeight() != srcH;
                        if(needsResize){
                            previewCaptureBuffer = FrameBuffer::Create(srcW, srcH);
                            previewTexture = Texture::CreateEmpty(srcW, srcH);
                            if(previewCaptureBuffer && previewTexture){
                                previewCaptureBuffer->attachTexture(previewTexture);
                            }
                        }

                        if(previewCaptureBuffer && previewTexture){
                            GLint prevReadFbo = 0;
                            GLint prevDrawFbo = 0;
                            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
                            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);

                            glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceBuffer->getID());
                            glReadBuffer(GL_COLOR_ATTACHMENT0);
                            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previewCaptureBuffer->getID());
                            glDrawBuffer(GL_COLOR_ATTACHMENT0);
                            glBlitFramebuffer(
                                0, 0, srcW, srcH,
                                0, 0, srcW, srcH,
                                GL_COLOR_BUFFER_BIT,
                                GL_NEAREST
                            );

                            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevReadFbo);
                            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDrawFbo);
                        }else{
                            previewTexture.reset();
                        }
                    }else{
                        previewTexture.reset();
                    }
                }else{
                    previewTexture.reset();
                }
            }else{
                previewTexture.reset();
            }

            // Keep the editor viewport render driven by the editor camera in edit mode.
            PCamera mainEditCamera = viewportCamera ? viewportCamera : editorCamera;
            if(mainScreen && mainEditCamera){
                PScene effectScene = nullptr;
                NeoECS::ECSEntity* effectEntity = nullptr;

                if(mainEditCamera == editorCamera && editorCameraObject && getECS()){
                    effectScene = std::static_pointer_cast<Scene>(shared_from_this());
                    effectEntity = editorCameraObject->gameobject();
                }else if(previewEntity && previewCamera && targetScene && targetScene->getECS()){
                    effectScene = targetScene;
                    effectEntity = previewEntity;
                }

                applyCameraEffects(effectScene, effectEntity);
                mainScreen->setCamera(mainEditCamera);
            }
            targetScene->renderViewportContents();
        }else{
            previewTexture.reset();
            previewCamera.reset();
            targetScene->renderViewportContents();
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;
    const bool maximizeViewport = (maximizeOnPlay && playState == PlayState::Play);

    if(!maximizeViewport){
        drawToolbar(width, kToolbarHeight);
    }

    if(maximizeViewport){
        viewportRect.x = 0.0f;
        viewportRect.y = 0.0f;
        viewportRect.w = width;
        viewportRect.h = height;
        viewportRect.valid = (width > 1.0f && height > 1.0f);
        viewportHovered = true;
        startupUiFramePresented = true;
        return;
    }

    const float panelsTop = kToolbarHeight + kPanelGap;
    const float availableHeight = std::max(0.0f, height - panelsTop - kPanelGap);
    const float availableWidth = std::max(0.0f, width - (kSplitterThickness * 2.0f));

    float maxBottom = std::max(kMinBottomPanelHeight, availableHeight - kMinTopPanelHeight - kSplitterThickness);
    bottomPanelHeight = std::clamp(bottomPanelHeight, kMinBottomPanelHeight, maxBottom);
    float topPanelsHeight = availableHeight - bottomPanelHeight - kSplitterThickness;
    if(topPanelsHeight < kMinTopPanelHeight){
        topPanelsHeight = kMinTopPanelHeight;
        bottomPanelHeight = std::max(kMinBottomPanelHeight, availableHeight - topPanelsHeight - kSplitterThickness);
    }
    if(bottomPanelHeight < kMinBottomPanelHeight){
        bottomPanelHeight = kMinBottomPanelHeight;
        topPanelsHeight = std::max(0.0f, availableHeight - bottomPanelHeight - kSplitterThickness);
    }
    if(availableHeight < (kMinTopPanelHeight + kMinBottomPanelHeight + kSplitterThickness)){
        topPanelsHeight = std::max(80.0f, availableHeight * 0.6f);
        bottomPanelHeight = std::max(60.0f, availableHeight - topPanelsHeight - kSplitterThickness);
    }

    float maxLeft = std::max(kMinLeftPanelWidth, availableWidth - kMinRightPanelWidth - kMinCenterPanelWidth);
    leftPanelWidth = std::clamp(leftPanelWidth, kMinLeftPanelWidth, maxLeft);
    float maxRight = std::max(kMinRightPanelWidth, availableWidth - leftPanelWidth - kMinCenterPanelWidth);
    rightPanelWidth = std::clamp(rightPanelWidth, kMinRightPanelWidth, maxRight);

    float centerWidth = availableWidth - leftPanelWidth - rightPanelWidth;
    if(centerWidth < kMinCenterPanelWidth){
        float deficit = kMinCenterPanelWidth - centerWidth;
        float shrinkRight = std::min(deficit, std::max(0.0f, rightPanelWidth - kMinRightPanelWidth));
        rightPanelWidth -= shrinkRight;
        deficit -= shrinkRight;
        float shrinkLeft = std::min(deficit, std::max(0.0f, leftPanelWidth - kMinLeftPanelWidth));
        leftPanelWidth -= shrinkLeft;
        centerWidth = availableWidth - leftPanelWidth - rightPanelWidth;
    }
    if(availableWidth < (kMinLeftPanelWidth + kMinRightPanelWidth + kMinCenterPanelWidth)){
        leftPanelWidth = std::max(120.0f, availableWidth * 0.25f);
        rightPanelWidth = std::max(140.0f, availableWidth * 0.30f);
        centerWidth = std::max(80.0f, availableWidth - leftPanelWidth - rightPanelWidth);
        float total = leftPanelWidth + rightPanelWidth + centerWidth;
        if(total > availableWidth){
            float overflow = total - availableWidth;
            float trimRight = std::min(overflow, std::max(0.0f, rightPanelWidth - 80.0f));
            rightPanelWidth -= trimRight;
            overflow -= trimRight;
            float trimLeft = std::min(overflow, std::max(0.0f, leftPanelWidth - 80.0f));
            leftPanelWidth -= trimLeft;
            overflow -= trimLeft;
            centerWidth = std::max(80.0f, availableWidth - leftPanelWidth - rightPanelWidth);
        }
    }

    const float leftSplitterX = leftPanelWidth;
    const float viewportX = leftSplitterX + kSplitterThickness;
    const float rightSplitterX = viewportX + centerWidth;
    const float propertiesX = rightSplitterX + kSplitterThickness;
    const float bottomTop = panelsTop + topPanelsHeight + kSplitterThickness;

    // Note: immediate-mode placeholder swapping caused visible flicker (scrollbars/windows blinking)
    // in play mode. Keep panels rendering every frame and rely on internal panel/cache optimizations instead.
    bool renderHeavyPanels = true;
    playModeHeavyPanelsRefreshDue = false;

    drawEcsPanel(0.0f, panelsTop, leftPanelWidth, topPanelsHeight, !renderHeavyPanels);
    drawViewportPanel(viewportX, panelsTop, centerWidth, topPanelsHeight);
    drawPropertiesPanel(propertiesX, panelsTop, rightPanelWidth, topPanelsHeight, !renderHeavyPanels);
    drawAssetsPanel(0.0f, bottomTop, width, bottomPanelHeight, !renderHeavyPanels);

    auto drawSplitter = [](const char* windowId, const ImVec2& pos, const ImVec2& size, ImGuiMouseCursor cursor, bool vertical, float& value, float deltaSign, float minValue, float maxValue){
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin(windowId, nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoBringToFrontOnFocus
        );
        ImGui::InvisibleButton("splitter_btn", size);
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        if(hovered || active){
            ImGui::SetMouseCursor(cursor);
        }
        if(active){
            float delta = vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
            value = std::clamp(value + (delta * deltaSign), minValue, maxValue);
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 color = active ? IM_COL32(102, 160, 255, 255)
                             : (hovered ? IM_COL32(84, 124, 188, 210) : IM_COL32(56, 68, 92, 180));
        drawList->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), color, 1.0f);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    };

    drawSplitter("##LeftRightSplitter", ImVec2(leftSplitterX, panelsTop), ImVec2(kSplitterThickness, topPanelsHeight), ImGuiMouseCursor_ResizeEW, true, leftPanelWidth, +1.0f, kMinLeftPanelWidth, std::max(kMinLeftPanelWidth, availableWidth - kMinRightPanelWidth - kMinCenterPanelWidth));
    drawSplitter("##RightPropsSplitter", ImVec2(rightSplitterX, panelsTop), ImVec2(kSplitterThickness, topPanelsHeight), ImGuiMouseCursor_ResizeEW, true, rightPanelWidth, -1.0f, kMinRightPanelWidth, std::max(kMinRightPanelWidth, availableWidth - leftPanelWidth - kMinCenterPanelWidth));
    drawSplitter("##BottomSplitter", ImVec2(0.0f, panelsTop + topPanelsHeight), ImVec2(width, kSplitterThickness), ImGuiMouseCursor_ResizeNS, false, bottomPanelHeight, -1.0f, kMinBottomPanelHeight, std::max(kMinBottomPanelHeight, availableHeight - kMinTopPanelHeight - kSplitterThickness));
    processDeferredToolbarCommands();
    drawSceneFileDialog();

    const bool interactionActive =
        propertiesPanel.isInteractionActive() ||
        transformWidget.isDragging() ||
        boundsWidget.isDragging() ||
        cameraWidget.isDragging() ||
        lightWidget.isDragging() ||
        viewportPrefabDragState.active;
    observeCurrentEditState(interactionActive);
    observeTransientEditorSessionState();
    flushEditorSessionAutosave(false, interactionActive);
    startupUiFramePresented = true;
}

void EditorScene::drawToWindow(bool clearWindow, float, float, float, float){
    RenderWindow* window = getWindow();
    if(!window) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window->getWindowWidth(), window->getWindowHeight());

    if(clearWindow){
        glClearColor(0.08f, 0.09f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if(targetScene && targetInitialized && viewportRect.valid){
        targetScene->drawToWindow(false, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
    }
}

void EditorScene::setIoStatus(const std::string& message, bool isError){
    ioStatusMessage = message;
    ioStatusIsError = isError;
    ioStatusTimeRemaining = kIoStatusDurationSeconds;

    if(isError){
        LogBot.Log(LOG_ERRO, "%s", message.c_str());
    }else{
        LogBot.Log(LOG_INFO, "%s", message.c_str());
    }
}

bool EditorScene::openSceneFileDialog(SceneFileDialogMode mode){
    if(mode == SceneFileDialogMode::None){
        return false;
    }
    if(playState != PlayState::Edit){
        const char* action = (mode == SceneFileDialogMode::SaveAs) ? "Save Scene As" : "Load Scene";
        setIoStatus(std::string(action) + " is only available in Edit mode.", true);
        return false;
    }
    if(mode == SceneFileDialogMode::SaveAs && (!targetScene || !targetScene->getECS())){
        setIoStatus("Save Scene As failed: target scene is unavailable.", true);
        return false;
    }

    sceneFileDialogState = SceneFileDialogState{};
    sceneFileDialogState.mode = mode;
    sceneFileDialogState.openRequested = true;
    sceneFileDialogState.focusNameInput = true;

    std::filesystem::path initialDir = workspacePanel.getCurrentDirectory();
    if(initialDir.empty()){
        initialDir = assetRoot;
    }

    std::filesystem::path preferredScenePath;
    if(isPathWithExtension(selectedAssetPath, ".scene")){
        preferredScenePath = selectedAssetPath.lexically_normal();
    }else if(isPathWithExtension(activeScenePath, ".scene")){
        preferredScenePath = activeScenePath.lexically_normal();
    }

    if(!selectedAssetPath.empty() && pathWithinRoot(selectedAssetPath, assetRoot)){
        std::error_code ec;
        if(std::filesystem::is_directory(selectedAssetPath, ec) && !ec){
            initialDir = selectedAssetPath.lexically_normal();
        }
    }

    if(!preferredScenePath.empty()){
        const std::filesystem::path preferredDir = preferredScenePath.parent_path();
        if(!preferredDir.empty() && pathWithinRoot(preferredDir, assetRoot)){
            initialDir = preferredDir;
        }
    }

    if(!pathWithinRoot(initialDir, assetRoot)){
        initialDir = assetRoot;
    }
    sceneFileDialogState.currentDirectory = initialDir.lexically_normal();

    if(!preferredScenePath.empty() && pathWithinRoot(preferredScenePath, assetRoot)){
        if(mode == SceneFileDialogMode::SaveAs){
            sceneFileDialogState.selectedPath = preferredScenePath;
        }else{
            std::error_code ec;
            if(std::filesystem::exists(preferredScenePath, ec) && !std::filesystem::is_directory(preferredScenePath, ec)){
                sceneFileDialogState.selectedPath = preferredScenePath;
            }
        }
    }

    std::string initialFileName;
    if(!sceneFileDialogState.selectedPath.empty()){
        initialFileName = sceneFileDialogState.selectedPath.filename().string();
    }
    if(initialFileName.empty() && !preferredScenePath.empty()){
        initialFileName = preferredScenePath.filename().string();
    }
    if(mode == SceneFileDialogMode::SaveAs && initialFileName.empty()){
        initialFileName = "Main.scene";
    }
    copyFixedString(sceneFileDialogState.fileNameBuffer, sizeof(sceneFileDialogState.fileNameBuffer), initialFileName);
    return true;
}

bool EditorScene::saveSceneFromEditorCommand(){
    pendingPlayAfterSceneSave = false;
    std::filesystem::path sourcePath = resolveCurrentSceneSourcePath();
    if(sourcePath.empty()){
        return openSceneFileDialog(SceneFileDialogMode::SaveAs);
    }
    return saveSceneToAbsolutePath(sourcePath);
}

bool EditorScene::saveSceneAsFromEditorCommand(){
    pendingPlayAfterSceneSave = false;
    return openSceneFileDialog(SceneFileDialogMode::SaveAs);
}

std::filesystem::path EditorScene::resolveCurrentSceneSourcePath() const{
    if(isPathWithExtension(activeScenePath, ".scene")){
        return activeScenePath.lexically_normal();
    }

    if(std::shared_ptr<LoadedScene> loadedScene = std::dynamic_pointer_cast<LoadedScene>(targetScene)){
        const std::filesystem::path sourcePath = loadedScene->getSourceScenePath();
        if(isPathWithExtension(sourcePath, ".scene")){
            return sourcePath.lexically_normal();
        }
    }

    return {};
}

void EditorScene::updateSceneSourceAfterSave(const std::filesystem::path& savePath){
    const std::filesystem::path normalizedPath = savePath.lexically_normal();
    activeScenePath = normalizedPath;
    AssetManager::Instance.unmanageAsset(normalizedPath.generic_string());
    PAsset sourceSceneAsset = AssetManager::Instance.getOrLoad(normalizedPath.generic_string());
    if(std::shared_ptr<LoadedScene> loadedScene = std::dynamic_pointer_cast<LoadedScene>(targetScene)){
        loadedScene->setSceneRefOrPath(normalizedPath.generic_string());
        loadedScene->setBaseDirectory(normalizedPath.parent_path());
        loadedScene->setSourceScenePath(normalizedPath);
        loadedScene->setSourceSceneAsset(sourceSceneAsset);
    }

    const std::filesystem::path sceneDirectory = normalizedPath.parent_path();
    targetFactory = [normalizedPath, sceneDirectory](RenderWindow* window) -> PScene {
        return std::make_shared<LoadedScene>(window, normalizedPath.generic_string(), sceneDirectory);
    };
    markEditorSessionDirty(0.0f);
}

bool EditorScene::beginPlayModeFromEditor(){
    if(playState == PlayState::Pause){
        playState = PlayState::Play;
        return true;
    }
    if(playState != PlayState::Edit){
        return false;
    }

    std::shared_ptr<LoadedScene> loadedScene = std::dynamic_pointer_cast<LoadedScene>(targetScene);
    std::filesystem::path sourcePath;
    if(loadedScene && tryResolveValidSceneAssetPath(loadedScene->getSourceSceneAsset(), assetRoot, sourcePath)){
        if(!saveSceneToAbsolutePath(sourcePath)){
            return false;
        }

        return enterPlayModeFromScenePath(sourcePath);
    }

    pendingPlayAfterSceneSave = false;
    if(!openSceneFileDialog(SceneFileDialogMode::SaveAs)){
        return false;
    }

    pendingPlayAfterSceneSave = true;
    return false;
}

bool EditorScene::enterPlayModeFromScenePath(const std::filesystem::path& scenePath){
    if(scenePath.empty()){
        return false;
    }

    storeSelectionForPlay();
    if(!loadSceneFromAbsolutePath(scenePath)){
        restoreSelectionAfterReset();
        return false;
    }

    playState = PlayState::Play;
    return true;
}

void EditorScene::processDeferredToolbarCommands(){
    if(ImGui::IsAnyItemActive()){
        return;
    }

    if(pendingToolbarUndoCommand){
        pendingToolbarUndoCommand = false;
        performUndo();
    }

    if(pendingToolbarRedoCommand){
        pendingToolbarRedoCommand = false;
        performRedo();
    }

    if(pendingToolbarSaveSceneCommand){
        pendingToolbarSaveSceneCommand = false;
        saveSceneFromEditorCommand();
    }

    if(pendingToolbarSaveSceneAsCommand){
        pendingToolbarSaveSceneAsCommand = false;
        saveSceneAsFromEditorCommand();
    }

    if(pendingToolbarLoadSceneCommand){
        pendingToolbarLoadSceneCommand = false;
        loadSceneFromEditorCommand();
    }

    if(pendingToolbarPlayCommand){
        pendingToolbarPlayCommand = false;
        beginPlayModeFromEditor();
    }
}

bool EditorScene::saveSceneToAbsolutePath(const std::filesystem::path& savePath){
    if(playState != PlayState::Edit){
        setIoStatus("Save Scene is only available in Edit mode.", true);
        return false;
    }
    if(!targetScene || !targetScene->getECS()){
        setIoStatus("Save Scene failed: target scene is unavailable.", true);
        return false;
    }

    if(savePath.empty()){
        setIoStatus("Save Scene failed: choose a valid scene file name.", true);
        return false;
    }

    std::filesystem::path normalizedPath = savePath.lexically_normal();
    if(!isPathWithExtension(normalizedPath, ".scene")){
        normalizedPath += ".scene";
    }
    if(normalizedPath.filename().empty()){
        setIoStatus("Save Scene failed: choose a valid scene file name.", true);
        return false;
    }

    std::filesystem::path saveDirectory = normalizedPath.parent_path();
    if(saveDirectory.empty()){
        saveDirectory = assetRoot;
        normalizedPath = saveDirectory / normalizedPath.filename();
    }
    if(!pathWithinRoot(saveDirectory, assetRoot)){
        setIoStatus("Save Scene failed: scene files must stay inside the asset workspace.", true);
        return false;
    }

    std::error_code ec;
    if(!std::filesystem::exists(saveDirectory, ec) || !std::filesystem::is_directory(saveDirectory, ec)){
        setIoStatus("Save Scene failed: target directory does not exist.", true);
        return false;
    }

    SceneIO::SceneSaveOptions options;
    options.registry = &Serialization::DefaultComponentSerializationRegistry();
    options.metadata.name = normalizedPath.stem().string();
    if(options.metadata.name.empty()){
        options.metadata.name = "Scene";
    }

    std::string error;
    if(!SceneIO::SaveSceneToAbsolutePath(targetScene, normalizedPath, options, &error)){
        setIoStatus("Save Scene failed: " + error, true);
        return false;
    }

    updateSceneSourceAfterSave(normalizedPath);
    selectedAssetPath = normalizedPath;
    markEditorSessionDirty(0.0f);
    setIoStatus("Scene saved: " + normalizedPath.generic_string(), false);
    return true;
}

bool EditorScene::loadSceneFromEditorCommand(){
    pendingPlayAfterSceneSave = false;
    return openSceneFileDialog(SceneFileDialogMode::Load);
}

bool EditorScene::loadSceneFromAbsolutePath(const std::filesystem::path& loadPath){
    if(playState != PlayState::Edit){
        setIoStatus("Load Scene is only available in Edit mode.", true);
        return false;
    }

    cancelViewportPrefabDragPreview();

    if(loadPath.empty()){
        setIoStatus("Load Scene failed: choose a .scene file.", true);
        return false;
    }

    std::filesystem::path normalizedPath = loadPath.lexically_normal();
    if(!isPathWithExtension(normalizedPath, ".scene")){
        normalizedPath += ".scene";
    }
    if(!pathWithinRoot(normalizedPath, assetRoot)){
        setIoStatus("Load Scene failed: scene files must stay inside the asset workspace.", true);
        return false;
    }

    std::error_code ec;
    if(!std::filesystem::exists(normalizedPath, ec) || std::filesystem::is_directory(normalizedPath, ec)){
        setIoStatus("Load Scene failed: scene file does not exist.", true);
        return false;
    }

    const std::filesystem::path sceneDirectory = normalizedPath.parent_path();
    auto loadedSceneFactory = [normalizedPath, sceneDirectory](RenderWindow* window) -> PScene {
        return std::make_shared<LoadedScene>(window, normalizedPath.generic_string(), sceneDirectory);
    };

    std::shared_ptr<LoadedScene> loadedScene = std::dynamic_pointer_cast<LoadedScene>(loadedSceneFactory(getWindow()));
    if(!loadedScene){
        setIoStatus("Load Scene failed: could not create loaded scene instance.", true);
        return false;
    }

    if(getWindow()){
        loadedScene->attachWindow(getWindow());
    }
    if(inputManager){
        loadedScene->setInputManager(inputManager);
    }
    loadedScene->init();
    if(!loadedScene->didLoadSuccessfully()){
        const std::string error = loadedScene->getLastLoadError().empty()
            ? std::string("Unknown load error.")
            : loadedScene->getLastLoadError();
        loadedScene->dispose();
        setIoStatus("Load Scene failed: " + error, true);
        return false;
    }

    setActiveScene(loadedScene);
    targetInitialized = true;
    targetFactory = loadedSceneFactory;
    applyActiveSceneState();
    activeScenePath = normalizedPath;
    selectedAssetPath = normalizedPath;
    resetEditHistoryToCurrentScene();
    setIoStatus("Scene loaded: " + normalizedPath.generic_string(), false);
    return true;
}

void EditorScene::drawSceneFileDialog(){
    if(sceneFileDialogState.mode == SceneFileDialogMode::None){
        return;
    }

    constexpr const char* kPopupId = "Scene File Dialog##EditorScene";
    if(sceneFileDialogState.openRequested){
        ImGui::OpenPopup(kPopupId);
        sceneFileDialogState.openRequested = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if(viewport){
        const float modalWidth = std::clamp(viewport->Size.x * 0.46f, 540.0f, 900.0f);
        const float modalHeight = std::clamp(viewport->Size.y * 0.58f, 360.0f, 680.0f);
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(modalWidth, modalHeight), ImGuiCond_Appearing);
    }

    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.82f));
    if(!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)){
        if(!ImGui::IsPopupOpen(kPopupId)){
            sceneFileDialogState = SceneFileDialogState{};
            pendingPlayAfterSceneSave = false;
        }
        ImGui::PopStyleColor();
        return;
    }

    if(sceneFileDialogState.currentDirectory.empty() || !pathWithinRoot(sceneFileDialogState.currentDirectory, assetRoot)){
        sceneFileDialogState.currentDirectory = assetRoot;
    }

    const bool isSaveMode = (sceneFileDialogState.mode == SceneFileDialogMode::SaveAs);
    ImGui::TextUnformatted(isSaveMode ? "Save Scene As" : "Load Scene");
    ImGui::TextDisabled("Directory: %s", sceneFileDialogState.currentDirectory.generic_string().c_str());

    if(!sceneFileDialogState.errorMessage.empty()){
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", sceneFileDialogState.errorMessage.c_str());
        ImGui::PopStyleColor();
    }

    if(ImGui::Button("Root")){
        sceneFileDialogState.currentDirectory = assetRoot;
        sceneFileDialogState.selectedPath.clear();
        sceneFileDialogState.errorMessage.clear();
    }
    ImGui::SameLine();
    const bool canGoUp =
        !sceneFileDialogState.currentDirectory.empty() &&
        sceneFileDialogState.currentDirectory != assetRoot &&
        pathWithinRoot(sceneFileDialogState.currentDirectory.parent_path(), assetRoot);
    ImGui::BeginDisabled(!canGoUp);
    if(ImGui::Button("Up") && canGoUp){
        sceneFileDialogState.currentDirectory = sceneFileDialogState.currentDirectory.parent_path();
        sceneFileDialogState.selectedPath.clear();
        sceneFileDialogState.errorMessage.clear();
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    const float footerReserve = ImGui::GetFrameHeight() * (isSaveMode ? 4.8f : 4.0f);
    ImGui::BeginChild("SceneFileDialogEntries", ImVec2(0.0f, -footerReserve), true);
    if(std::filesystem::exists(sceneFileDialogState.currentDirectory)){
        /// @brief Represents Entry data.
        struct Entry {
            std::filesystem::path path;
            bool isDirectory = false;
        };

        std::vector<Entry> entries;
        std::error_code ec;
        for(const auto& dirEntry : std::filesystem::directory_iterator(
                sceneFileDialogState.currentDirectory,
                std::filesystem::directory_options::skip_permission_denied,
                ec)){
            const std::filesystem::path entryPath = dirEntry.path().lexically_normal();
            const bool isDirectory = dirEntry.is_directory();
            if(isDirectory){
                entries.push_back({entryPath, true});
                continue;
            }
            if(isPathWithExtension(entryPath, ".scene")){
                entries.push_back({entryPath, false});
            }
        }

        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b){
            if(a.isDirectory != b.isDirectory){
                return a.isDirectory > b.isDirectory;
            }
            return a.path.filename().string() < b.path.filename().string();
        });

        for(const Entry& entry : entries){
            std::string label = entry.path.filename().string();
            if(entry.isDirectory){
                label = "[DIR] " + label;
            }

            const bool isSelected = (!entry.isDirectory && sceneFileDialogState.selectedPath == entry.path);
            if(ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)){
                sceneFileDialogState.errorMessage.clear();
                if(entry.isDirectory){
                    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){
                        sceneFileDialogState.currentDirectory = entry.path;
                    }
                    sceneFileDialogState.selectedPath.clear();
                }else{
                    sceneFileDialogState.selectedPath = entry.path;
                    copyFixedString(
                        sceneFileDialogState.fileNameBuffer,
                        sizeof(sceneFileDialogState.fileNameBuffer),
                        entry.path.filename().string()
                    );
                }
            }
        }
    }else{
        ImGui::TextDisabled("Directory missing.");
    }
    ImGui::EndChild();

    bool submitRequested = false;
    if(sceneFileDialogState.focusNameInput){
        ImGui::SetKeyboardFocusHere();
        sceneFileDialogState.focusNameInput = false;
    }
    if(ImGui::InputText("Scene File", sceneFileDialogState.fileNameBuffer, sizeof(sceneFileDialogState.fileNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)){
        submitRequested = true;
    }

    auto buildCandidatePath = [&]() -> std::filesystem::path {
        std::string fileName = trimCopy(sceneFileDialogState.fileNameBuffer);
        if(fileName.empty() && !sceneFileDialogState.selectedPath.empty()){
            fileName = sceneFileDialogState.selectedPath.filename().string();
        }
        if(fileName.empty()){
            return {};
        }

        std::filesystem::path candidate = sceneFileDialogState.currentDirectory / fileName;
        if(!isPathWithExtension(candidate, ".scene")){
            candidate += ".scene";
        }
        return candidate.lexically_normal();
    };

    const std::filesystem::path candidatePath = buildCandidatePath();
    if(!candidatePath.empty()){
        ImGui::TextDisabled("%s", candidatePath.generic_string().c_str());
    }else{
        ImGui::TextDisabled("%s", isSaveMode ? "Enter a scene file name." : "Select or enter a .scene file.");
    }

    const bool canConfirm = !candidatePath.empty();
    if(!canConfirm){
        ImGui::BeginDisabled();
    }
    if(ImGui::Button(isSaveMode ? "Save As" : "Load") || (submitRequested && canConfirm)){
        sceneFileDialogState.errorMessage.clear();
        bool success = false;
        if(isSaveMode){
            success = saveSceneToAbsolutePath(candidatePath);
        }else{
            success = loadSceneFromAbsolutePath(candidatePath);
        }

        if(success){
            if(isSaveMode && pendingPlayAfterSceneSave){
                success = enterPlayModeFromScenePath(candidatePath);
            }
        }

        if(success){
            pendingPlayAfterSceneSave = false;
            ImGui::CloseCurrentPopup();
            sceneFileDialogState = SceneFileDialogState{};
            ImGui::EndPopup();
            ImGui::PopStyleColor();
            return;
        }

        if(sceneFileDialogState.errorMessage.empty()){
            sceneFileDialogState.errorMessage = ioStatusMessage.empty()
                ? (isSaveMode ? "Save Scene As failed." : "Load Scene failed.")
                : ioStatusMessage;
        }
    }
    if(!canConfirm){
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if(ImGui::Button("Cancel")){
        pendingPlayAfterSceneSave = false;
        ImGui::CloseCurrentPopup();
        sceneFileDialogState = SceneFileDialogState{};
        ImGui::EndPopup();
        ImGui::PopStyleColor();
        return;
    }

    ImGui::EndPopup();
    ImGui::PopStyleColor();
}

bool EditorScene::exportEntityAsPrefabToDirectory(const std::string& entityId, const std::filesystem::path& directoryPath){
    if(playState != PlayState::Edit){
        setIoStatus("Create Prefab is only available in Edit mode.", true);
        return false;
    }
    if(!targetScene || !targetScene->getECS()){
        setIoStatus("Create Prefab failed: target scene is unavailable.", true);
        return false;
    }

    NeoECS::ECSEntity* entity = findEntityById(entityId);
    if(!entity){
        setIoStatus("Create Prefab failed: selected entity was not found.", true);
        return false;
    }
    if(targetScene->isSceneRootEntity(entity)){
        setIoStatus("Create Prefab failed: scene root cannot be exported.", true);
        return false;
    }

    std::filesystem::path exportDirectory = directoryPath;
    if(exportDirectory.empty()){
        exportDirectory = assetRoot;
    }
    std::error_code ec;
    if(!std::filesystem::exists(exportDirectory, ec)){
        if(!std::filesystem::create_directories(exportDirectory, ec)){
            setIoStatus("Create Prefab failed: could not create export directory.", true);
            return false;
        }
    }else if(!std::filesystem::is_directory(exportDirectory, ec)){
        setIoStatus("Create Prefab failed: export path is not a directory.", true);
        return false;
    }

    exportDirectory = exportDirectory.lexically_normal();
    const std::string safeStem = sanitizeFileStem(entity->getName(), "Prefab");
    std::filesystem::path prefabPath = makeUniquePath(exportDirectory / (safeStem + ".prefab"));

    PrefabIO::PrefabSaveOptions options;
    options.registry = &Serialization::DefaultComponentSerializationRegistry();
    options.metadata.name = entity->getName().empty() ? safeStem : entity->getName();

    std::string error;
    if(!PrefabIO::SaveEntitySubtreeToAbsolutePath(targetScene, entity, prefabPath, options, &error)){
        setIoStatus("Create Prefab failed: " + error, true);
        return false;
    }

    selectedAssetPath = prefabPath;
    setIoStatus("Prefab created: " + prefabPath.generic_string(), false);
    return true;
}

bool EditorScene::exportEntityAsPrefabToWorkspaceDirectory(const std::string& entityId){
    return exportEntityAsPrefabToDirectory(entityId, workspacePanel.getCurrentDirectory());
}

bool EditorScene::instantiatePrefabFromAbsolutePath(const std::filesystem::path& prefabPath,
                                                    NeoECS::GameObject* parentObject,
                                                    PrefabIO::PrefabInstantiateResult* outResult,
                                                    std::string* outError){
    if(!targetScene || !targetScene->getECS()){
        if(outError){
            *outError = "Target scene is unavailable.";
        }
        return false;
    }
    if(!isPathWithExtension(prefabPath, ".prefab")){
        if(outError){
            *outError = "Dropped asset is not a .prefab file.";
        }
        return false;
    }

    std::error_code ec;
    if(!std::filesystem::exists(prefabPath, ec) || std::filesystem::is_directory(prefabPath, ec)){
        if(outError){
            *outError = "Prefab file does not exist.";
        }
        return false;
    }

    PrefabIO::PrefabInstantiateOptions options;
    options.parent = parentObject;
    options.registry = &Serialization::DefaultComponentSerializationRegistry();
    if(!PrefabIO::InstantiateFromAbsolutePath(targetScene, prefabPath, options, outResult, outError)){
        return false;
    }
    if(outResult && outResult->rootObjects.empty()){
        if(outError){
            *outError = "Prefab instantiated with no root objects.";
        }
        return false;
    }
    return true;
}

bool EditorScene::instantiatePrefabUnderParentEntity(const std::filesystem::path& prefabPath, const std::string& parentEntityId){
    if(playState != PlayState::Edit){
        setIoStatus("Instantiate Prefab is only available in Edit mode.", true);
        return false;
    }
    if(!targetScene || !targetScene->getECS()){
        setIoStatus("Instantiate Prefab failed: target scene is unavailable.", true);
        return false;
    }

    NeoECS::GameObject* parentObject = nullptr;
    std::unique_ptr<NeoECS::GameObject> parentWrapper;
    if(!parentEntityId.empty()){
        NeoECS::ECSEntity* parentEntity = findEntityById(parentEntityId);
        if(!parentEntity){
            setIoStatus("Instantiate Prefab failed: target parent entity was not found.", true);
            return false;
        }
        parentWrapper.reset(NeoECS::GameObject::CreateFromECSEntity(targetScene->getECS()->getContext(), parentEntity));
        if(!parentWrapper){
            setIoStatus("Instantiate Prefab failed: could not resolve target parent object.", true);
            return false;
        }
        parentObject = parentWrapper.get();
    }

    flushEditHistoryObservation(true);

    PrefabIO::PrefabInstantiateResult result;
    std::string error;
    const std::filesystem::path normalizedPath = prefabPath.lexically_normal();
    if(!instantiatePrefabFromAbsolutePath(normalizedPath, parentObject, &result, &error)){
        setIoStatus("Instantiate Prefab failed: " + error, true);
        return false;
    }

    std::vector<NeoECS::ECSEntity*> createdRoots;
    createdRoots.reserve(result.rootObjects.size());
    for(auto* rootObject : result.rootObjects){
        if(rootObject && rootObject->gameobject()){
            createdRoots.push_back(rootObject->gameobject());
        }
    }
    if(!createdRoots.empty()){
        recordCreatedSubtreeChange(createdRoots, "Prefab instantiated", "entity.prefab.create");
    }

    if(!result.rootObjects.empty() && result.rootObjects.front() && result.rootObjects.front()->gameobject()){
        selectEntity(result.rootObjects.front()->gameobject()->getNodeUniqueID());
    }

    setIoStatus("Prefab instantiated: " + normalizedPath.generic_string(), false);
    return true;
}

bool EditorScene::beginViewportPrefabDragPreview(const std::filesystem::path& prefabPath){
    cancelViewportPrefabDragPreview();
    if(playState != PlayState::Edit){
        return false;
    }

    PrefabIO::PrefabInstantiateResult result;
    std::string error;
    const std::filesystem::path normalizedPath = prefabPath.lexically_normal();
    if(!instantiatePrefabFromAbsolutePath(normalizedPath, nullptr, &result, &error)){
        setIoStatus("Prefab drag failed: " + error, true);
        return false;
    }
    if(result.rootObjects.empty()){
        setIoStatus("Prefab drag failed: instantiated no root objects.", true);
        return false;
    }

    viewportPrefabDragState.active = true;
    viewportPrefabDragState.prefabPath = normalizedPath;
    viewportPrefabDragState.rootEntityIds.clear();
    viewportPrefabDragState.rootOffsets.clear();

    Math3D::Vec3 anchor = Math3D::Vec3::zero();
    bool anchorInitialized = false;
    for(auto* rootObject : result.rootObjects){
        if(!rootObject || !rootObject->gameobject()){
            continue;
        }
        NeoECS::ECSEntity* entity = rootObject->gameobject();
        const std::string entityId = entity->getNodeUniqueID();
        viewportPrefabDragState.rootEntityIds.push_back(entityId);
        Math3D::Vec3 worldPos = targetScene->getWorldPosition(entity);
        if(!anchorInitialized){
            anchor = worldPos;
            anchorInitialized = true;
        }
        viewportPrefabDragState.rootOffsets.push_back(worldPos - anchor);
    }

    if(!anchorInitialized || viewportPrefabDragState.rootEntityIds.empty()){
        cancelViewportPrefabDragPreview();
        setIoStatus("Prefab drag failed: could not resolve instantiated root objects.", true);
        return false;
    }

    viewportPrefabDragState.placementPlaneY = anchor.y;
    selectEntity(viewportPrefabDragState.rootEntityIds.front());
    return true;
}

bool EditorScene::computeViewportMousePlacement(float planeY, Math3D::Vec3& outWorldPosition) const{
    if(!inputManager || !viewportCamera || !viewportRect.valid){
        return false;
    }

    Math3D::Vec2 mouse = inputManager->getMousePosition();
    Math3D::Vec3 nearPoint = screenToWorld(viewportCamera, mouse.x, mouse.y, 0.0f, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
    Math3D::Vec3 farPoint = screenToWorld(viewportCamera, mouse.x, mouse.y, 1.0f, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
    Math3D::Vec3 rayDir = farPoint - nearPoint;
    if(rayDir.length() <= Math3D::EPSILON){
        return false;
    }
    rayDir = rayDir.normalize();

    float t = 4.0f;
    if(std::fabs(rayDir.y) > 0.0001f){
        t = (planeY - nearPoint.y) / rayDir.y;
        if(t < 0.0f){
            t = 4.0f;
        }
    }

    outWorldPosition = nearPoint + (rayDir * t);
    outWorldPosition.y = planeY;
    return true;
}

void EditorScene::updateViewportPrefabDragPreviewFromMouse(){
    if(!viewportPrefabDragState.active || !targetScene || !targetScene->getECS()){
        return;
    }

    Math3D::Vec3 anchorPos = Math3D::Vec3::zero();
    if(!computeViewportMousePlacement(viewportPrefabDragState.placementPlaneY, anchorPos)){
        return;
    }

    auto* manager = targetScene->getECS()->getComponentManager();
    const size_t count = std::min(viewportPrefabDragState.rootEntityIds.size(), viewportPrefabDragState.rootOffsets.size());
    for(size_t i = 0; i < count; ++i){
        NeoECS::ECSEntity* entity = findEntityById(viewportPrefabDragState.rootEntityIds[i]);
        if(!entity){
            continue;
        }
        auto* transformComp = manager->getECSComponent<TransformComponent>(entity);
        if(!transformComp){
            continue;
        }
        transformComp->local.position = anchorPos + viewportPrefabDragState.rootOffsets[i];
    }
}

void EditorScene::finalizeViewportPrefabDragPreview(){
    if(!viewportPrefabDragState.active){
        return;
    }
    std::filesystem::path placedPath = viewportPrefabDragState.prefabPath;
    std::vector<NeoECS::ECSEntity*> createdRoots;
    createdRoots.reserve(viewportPrefabDragState.rootEntityIds.size());
    for(const std::string& rootId : viewportPrefabDragState.rootEntityIds){
        if(NeoECS::ECSEntity* entity = findEntityById(rootId)){
            createdRoots.push_back(entity);
        }
    }
    viewportPrefabDragState = ViewportPrefabDragState{};
    if(!createdRoots.empty()){
        recordCreatedSubtreeChange(createdRoots, "Prefab instantiated", "entity.prefab.create");
    }
    setIoStatus("Prefab instantiated: " + placedPath.generic_string(), false);
}

void EditorScene::cancelViewportPrefabDragPreview(){
    if(!viewportPrefabDragState.active){
        return;
    }
    if(targetScene && targetScene->getECS()){
        for(const std::string& rootId : viewportPrefabDragState.rootEntityIds){
            NeoECS::ECSEntity* entity = findEntityById(rootId);
            if(!entity || targetScene->isSceneRootEntity(entity)){
                continue;
            }
            std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(targetScene->getECS()->getContext(), entity));
            if(wrapper){
                targetScene->destroyECSGameObject(wrapper.get());
            }
        }
    }
    viewportPrefabDragState = ViewportPrefabDragState{};
    if(!selectedEntityId.empty() && !findEntityById(selectedEntityId)){
        selectEntity("");
    }
}

void EditorScene::drawToolbar(float width, float height){
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("##Toolbar", nullptr, kPanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar);

    bool saveSceneClicked = false;
    bool saveSceneAsClicked = false;
    bool loadSceneClicked = false;
    bool undoClicked = false;
    bool redoClicked = false;
    if(ImGui::BeginMenuBar()){
        if(ImGui::BeginMenu("File")){
            saveSceneClicked = ImGui::MenuItem("Save Scene", "Ctrl+S", false, playState == PlayState::Edit);
            saveSceneAsClicked = ImGui::MenuItem("Save Scene As", "Ctrl+Shift+S", false, playState == PlayState::Edit);
            loadSceneClicked = ImGui::MenuItem("Load Scene", "Ctrl+O", false, playState == PlayState::Edit);
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Edit")){
            undoClicked = ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndoEditHistory());
            redoClicked = ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedoEditHistory());
            ImGui::Separator();
            if(ImGui::MenuItem("Editor Camera Settings", "Ctrl+F12")){
                setEditorCameraSettingsOpen(true);
                selectedAssetPath.clear();
            }
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("View")){
            ImGui::MenuItem("Maximize On Play", nullptr, &maximizeOnPlay);
            if(ImGui::BeginMenu("Scene")){
                ImGui::MenuItem("Show Grid", nullptr, &showSceneGrid);
                ImGui::MenuItem("Gizmos", nullptr, &showSceneGizmos);
                ImGui::MenuItem("Scene Performance Information", nullptr, &showScenePerformanceInfo);
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Debug")){
                if(ImGui::BeginMenu("Shadows")){
                    bool shadowDebugEnabled = ShadowRenderer::GetGlobalDebugOverrideEnabled();
                    if(ImGui::MenuItem("Debug Mode", nullptr, shadowDebugEnabled)){
                        ShadowRenderer::SetGlobalDebugOverrideEnabled(!shadowDebugEnabled);
                    }
                    const char* debugModeLabels[] = {"Visibility", "Cascade Index", "Projection Bounds"};
                    int globalMode = Math3D::Clamp(ShadowRenderer::GetGlobalDebugOverrideMode(), 1, 3);
                    if(!shadowDebugEnabled){
                        ImGui::BeginDisabled();
                    }
                    for(int i = 0; i < IM_ARRAYSIZE(debugModeLabels); ++i){
                        const bool selected = (globalMode == (i + 1));
                        if(ImGui::MenuItem(debugModeLabels[i], nullptr, selected)){
                            ShadowRenderer::SetGlobalDebugOverrideMode(i + 1);
                        }
                    }
                    if(!shadowDebugEnabled){
                        ImGui::EndDisabled();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGuiIO& io = ImGui::GetIO();
    if(playState == PlayState::Edit && !io.WantTextInput){
        if(canUndoEditHistory() && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)){
            undoClicked = true;
        }
        if(canRedoEditHistory() && io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                                                  (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))){
            redoClicked = true;
        }
        if(io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)){
            saveSceneAsClicked = true;
        }else if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)){
            saveSceneClicked = true;
        }
        if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)){
            loadSceneClicked = true;
        }
        if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F12, false)){
            setEditorCameraSettingsOpen(true);
            selectedAssetPath.clear();
        }
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

    bool playClicked = ImGui::Button(playState == PlayState::Play ? "Playing" : "Play");
    ImGui::SameLine();
    bool pauseClicked = ImGui::Button(playState == PlayState::Pause ? "Paused" : "Pause");
    ImGui::SameLine();
    bool stopClicked = ImGui::Button("Stop");

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(playState == PlayState::Edit ? "Edit Mode" : (playState == PlayState::Play ? "Play Mode" : "Paused"));

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(playState == PlayState::Edit ? "File > Save/Save As/Load Scene" : "Scene IO locked during play");
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    auto modeButton = [&](const char* label, TransformWidget::Mode mode){
        bool active = (transformWidget.getMode() == mode);
        if(active){
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.45f, 0.6f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.5f, 0.68f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.4f, 0.55f, 1.0f));
        }
        if(ImGui::Button(label)){
            transformWidget.setMode(mode);
        }
        if(active){
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();
    };
    modeButton("All", TransformWidget::Mode::Combined);

    if(!ioStatusMessage.empty()){
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        const ImVec4 statusColor = ioStatusIsError ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                                                   : ImVec4(0.45f, 0.95f, 0.55f, 1.0f);
        ImGui::TextColored(statusColor, "%s", ioStatusMessage.c_str());
    }

    if(playClicked){
        pendingToolbarPlayCommand = true;
    }

    if(undoClicked){
        pendingToolbarUndoCommand = true;
    }
    if(redoClicked){
        pendingToolbarRedoCommand = true;
    }

    if(pauseClicked){
        if(playState == PlayState::Play){
            playState = PlayState::Pause;
        }else if(playState == PlayState::Pause){
            playState = PlayState::Play;
        }
    }

    if(stopClicked){
        if(playState != PlayState::Edit){
            resetRequested.store(true);
        }
    }

    if(saveSceneClicked){
        pendingToolbarSaveSceneCommand = true;
    }
    if(saveSceneAsClicked){
        pendingToolbarSaveSceneAsCommand = true;
    }
    if(loadSceneClicked){
        pendingToolbarLoadSceneCommand = true;
    }

    ImGui::End();
}

bool EditorScene::isMouseInViewport() const{
    if(!inputManager || !viewportRect.valid) return false;
    Math3D::Vec2 mouse = inputManager->getMousePosition();
    return (mouse.x >= viewportRect.x && mouse.x <= (viewportRect.x + viewportRect.w) &&
            mouse.y >= viewportRect.y && mouse.y <= (viewportRect.y + viewportRect.h));
}

void EditorScene::selectEntity(const std::string& id){
    if(!id.empty() && id == selectedEntityId){
        focusOnEntity(id);
        return;
    }
    setEditorCameraSettingsOpen(false);
    selectedEntityId = id;
    if(!id.empty()){
        selectedAssetPath.clear();
    }
    transformWidget.reset();
    cameraWidget.reset();
    boundsWidget.reset();
    BoundsEditState::Deactivate();
    if(targetScene){
        targetScene->setSelectedEntityId(id);
    }
}

void EditorScene::focusOnEntity(const std::string& id){
    PCamera focusCamera = viewportCamera ? viewportCamera : editorCamera;
    if(id.empty() || !targetScene || !focusCamera) return;

    auto* ecs = targetScene->getECS();
    if(!ecs) return;
    auto* entity = findEntityById(id);
    if(!entity) return;

    auto* components = ecs->getComponentManager();
    auto* transformComp = components->getECSComponent<TransformComponent>(entity);
    auto* boundsComp = components->getECSComponent<BoundsComponent>(entity);
    if(!transformComp) return;

    Math3D::Mat4 world = buildWorldMatrix(entity, components);
    Math3D::Vec3 target = world.getPosition();
    float radius = 1.0f;
    if(boundsComp){
        target = Math3D::Transform::transformPoint(world, boundsComp->offset);
        switch(boundsComp->type){
            case BoundsType::Box:
                radius = Math3D::Vec3(boundsComp->size.x, boundsComp->size.y, boundsComp->size.z).length();
                break;
            case BoundsType::Sphere:
                radius = boundsComp->radius;
                break;
            case BoundsType::Capsule:
                radius = boundsComp->radius + (boundsComp->height * 0.5f);
                break;
        }
    }else{
        float scale = (transformComp->local.scale.x + transformComp->local.scale.y + transformComp->local.scale.z) / 3.0f;
        radius = std::max(0.5f, scale);
    }

    float fov = focusCamera->getSettings().fov;
    float distance = radius / std::tan(glm::radians(fov * 0.5f));
    distance *= 1.5f;

    auto transform = focusCamera->transform();
    focusTarget = target;
    focusForward = transform.forward() * -1.0f;
    focusDistance = distance;
    focusActive = true;
}

std::string EditorScene::pickEntityIdAtScreen(float x, float y, PCamera cam){
    if(!targetScene || !cam || !viewportRect.valid) return "";

    Math3D::Vec3 nearPoint = screenToWorld(cam, x, y, 0.0f, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
    Math3D::Vec3 farPoint = screenToWorld(cam, x, y, 1.0f, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
    Math3D::Vec3 rayDir = (farPoint - nearPoint).normalize();

    auto* ecs = targetScene->getECS();
    if(!ecs) return "";
    auto* componentManager = ecs->getComponentManager();
    const auto& entities = ecs->getEntityManager()->getEntities();

    float bestT = std::numeric_limits<float>::max();
    std::string bestId;

    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(!entity) continue;
        auto* transform = componentManager->getECSComponent<TransformComponent>(entity);
        auto* renderer = componentManager->getECSComponent<MeshRendererComponent>(entity);
        auto* cameraComp = componentManager->getECSComponent<CameraComponent>(entity);
        auto* lightComp = componentManager->getECSComponent<LightComponent>(entity);
        auto* bounds = componentManager->getECSComponent<BoundsComponent>(entity);
        if(!transform) continue;
        if(!renderer && !cameraComp && !lightComp && !bounds) continue;

        Math3D::Mat4 world = buildWorldMatrix(entity, componentManager);
        Math3D::Vec3 pos = world.getPosition();
        if(bounds){
            pos = Math3D::Transform::transformPoint(world, bounds->offset);
        }
        Math3D::Vec3 toPoint = pos - nearPoint;
        float t = Math3D::Vec3::dot(toPoint, rayDir);
        if(t < 0.0f) continue;

        Math3D::Vec3 closest = nearPoint + (rayDir * t);
        float dist = (pos - closest).length();
        float radius = 1.0f;
        if(bounds){
            switch(bounds->type){
                case BoundsType::Box:
                    radius = Math3D::Vec3(bounds->size.x, bounds->size.y, bounds->size.z).length();
                    break;
                case BoundsType::Sphere:
                    radius = bounds->radius;
                    break;
                case BoundsType::Capsule:
                    radius = bounds->radius + (bounds->height * 0.5f);
                    break;
            }
        }else{
            float scale = (transform->local.scale.x + transform->local.scale.y + transform->local.scale.z) / 3.0f;
            radius = std::max(0.5f, scale);
        }

        if(dist <= radius && t < bestT){
            if(!renderer && (cameraComp || lightComp)){
                Math3D::Vec3 screen = worldToScreen(cam, pos, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
                if(!screenPointInViewport(screen, TransformWidget::Viewport{viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, viewportRect.valid})){
                    continue;
                }
                float dx = screen.x - x;
                float dy = screen.y - y;
                float centerDistPx = std::sqrt(dx * dx + dy * dy);
                if(centerDistPx > kHelperCenterPickRadiusPx){
                    continue;
                }
            }
            bestT = t;
            bestId = entity->getNodeUniqueID();
        }
    }

    return bestId;
}

bool EditorScene::handleQuitRequest(){
    if(playState == PlayState::Play || playState == PlayState::Pause){
        LogBot.Log(LOG_WARN, "EditorScene::handleQuitRequest() -> Stop simulation");
        resetRequested.store(true);
        return true;
    }
    return false;
}

void EditorScene::performStop(){
    boundsWidget.reset();
    BoundsEditState::Deactivate();
    cancelViewportPrefabDragPreview();
    playState = PlayState::Edit;
    viewportRect = ViewportRect{};
    viewportHovered = false;
    previewWindowInitialized = false;
    if(targetFactory){
        std::function<PScene(RenderWindow*)> resetFactory = targetFactory;
        PScene replacementScene = resetFactory(getWindow());
        setActiveScene(replacementScene);
        targetFactory = resetFactory;
        ensureTargetInitialized();
    }
    LogBot.Log(LOG_WARN, "EditorScene::performStop() -> Done");
}

void EditorScene::storeSelectionForPlay(){
    resetContext.hadSelection = !selectedEntityId.empty();
    resetContext.selectedId = selectedEntityId;
    if(resetContext.hadSelection){
        selectEntity("");
    }
    if(editorCamera){
        resetContext.hadCamera = true;
        resetContext.editorCameraTransform = editorCamera->transform();
    }
}

void EditorScene::restoreSelectionAfterReset(){
    if(resetContext.hadSelection && !resetContext.selectedId.empty()){
        selectEntity(resetContext.selectedId);
    }else{
        selectEntity("");
    }
    if(resetContext.hadCamera && editorCamera){
        editorCamera->setTransform(resetContext.editorCameraTransform);
        if(editorCameraTransform){
            editorCameraTransform->local = resetContext.editorCameraTransform;
        }
    }
    resetContext = ResetContext{};
}

void EditorScene::requestClose(){
    if(handleQuitRequest()){
        return;
    }
    Scene::requestClose();
}

bool EditorScene::consumeCloseRequest(){
    if(targetScene && targetScene->consumeCloseRequest()){
        LogBot.Log(LOG_WARN, "EditorScene::consumeCloseRequest() intercepted target close.");
        handleQuitRequest();
        // Swallow target close requests so the editor never quits.
        closeRequested.store(false, std::memory_order_relaxed);
        return false;
    }
    // Editor scene should only close itself if explicitly requested elsewhere.
    return Scene::consumeCloseRequest();
}

void EditorScene::drawEcsPanel(float x, float y, float w, float h, bool lightweight){
    if(lightweight){
        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::Begin("ECS Graph", nullptr, kPanelFlags);
        ImGui::TextDisabled("Panel throttled during play mode (10 Hz).");
        ImGui::End();
        return;
    }
    ecsViewPanel.draw(
        x,
        y,
        w,
        h,
        targetScene,
        selectedEntityId,
        [this](const std::string& id){
            selectEntity(id);
        },
        [this](const std::string& entityId){
            exportEntityAsPrefabToWorkspaceDirectory(entityId);
        },
        [this](const std::filesystem::path& prefabPath, const std::string& parentEntityId, std::string* outError) -> bool {
            const bool ok = instantiatePrefabUnderParentEntity(prefabPath, parentEntityId);
            if(!ok && outError){
                *outError = ioStatusMessage.empty() ? "Instantiate Prefab failed." : ioStatusMessage;
            }
            return ok;
        },
        ECSViewPanel::ChangeCallbacks{
            [this](const std::string& entityId){
                beginPendingDeletedSubtreeCapture(entityId);
            },
            [this](const std::string& entityId){
                commitPendingDeletedSubtreeCapture(entityId);
            },
            [this](const std::string& entityId){
                recordCreatedEntityChange(entityId, "Entity created", "entity.create");
            },
            [this](const std::string& entityId, const std::string& oldName, const std::string& newName){
                recordRenamedEntityChange(entityId, oldName, newName);
            },
            [this](const std::string& entityId, const std::string& oldParentId, const std::string& newParentId){
                recordReparentedEntityChange(entityId, oldParentId, newParentId);
            }
        }
    );
}

void EditorScene::drawViewportPanel(float x, float y, float w, float h){
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(
        "Viewport",
        nullptr,
        kPanelFlags |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
    );
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    viewportHovered = ImGui::IsWindowHovered();
    viewportRect.x = pos.x;
    viewportRect.y = pos.y;
    viewportRect.w = size.x;
    viewportRect.h = size.y;
    viewportRect.valid = (size.x > 1.0f && size.y > 1.0f);

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##ViewportDragDropTarget", size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool showLoadingOverlay = startupBootstrapPending || startupLoadingOverlayActive || !targetInitialized;
    if(showLoadingOverlay && viewportRect.valid){
        const ImVec2 viewportMin(viewportRect.x, viewportRect.y);
        const ImVec2 viewportMax(viewportRect.x + viewportRect.w, viewportRect.y + viewportRect.h);
        drawList->AddRectFilled(viewportMin, viewportMax, IM_COL32(0, 0, 0, 255));

        static const char* kLoadingSuffixes[] = {"", ".", "..", "..."};
        const int suffixCount = (int)(sizeof(kLoadingSuffixes) / sizeof(kLoadingSuffixes[0]));
        const Uint64 dotIndex = (SDL_GetTicks() / 1000ULL) % (Uint64)suffixCount;
        const std::string loadingText = StringUtils::Format(
            "Please wait as the Scene is Loaded%s",
            kLoadingSuffixes[(size_t)dotIndex]
        );
        const ImVec2 textSize = ImGui::CalcTextSize(loadingText.c_str());
        const ImVec2 textPos(
            viewportRect.x + ((viewportRect.w - textSize.x) * 0.5f),
            viewportRect.y + ((viewportRect.h - textSize.y) * 0.5f)
        );
        drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), IM_COL32(0, 0, 0, 180), loadingText.c_str());
        drawList->AddText(textPos, IM_COL32(240, 240, 240, 245), loadingText.c_str());
    }
    TransformWidget::Viewport viewport{viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, viewportRect.valid};
    float topLeftOverlayY = viewportRect.y + 12.0f;

    bool sawViewportPrefabPayload = false;
    bool deliveredViewportPrefabPayload = false;
    std::filesystem::path viewportPrefabPayloadPath;
    if(!showLoadingOverlay && playState == PlayState::Edit && ImGui::BeginDragDropTarget()){
        EditorAssetUI::AssetTransaction droppedAsset;
        bool isDelivery = false;
        if(EditorAssetUI::AcceptAssetDropInCurrentTarget(EditorAssetUI::AssetKind::Any, droppedAsset, true, &isDelivery) &&
           isPathWithExtension(droppedAsset.absolutePath, ".prefab")){
            sawViewportPrefabPayload = true;
            deliveredViewportPrefabPayload = isDelivery;
            viewportPrefabPayloadPath = droppedAsset.absolutePath.lexically_normal();
        }
        ImGui::EndDragDropTarget();
    }

    if(playState != PlayState::Edit){
        cancelViewportPrefabDragPreview();
    }else if(sawViewportPrefabPayload){
        if(!viewportPrefabDragState.active || viewportPrefabDragState.prefabPath != viewportPrefabPayloadPath){
            beginViewportPrefabDragPreview(viewportPrefabPayloadPath);
        }
        if(viewportPrefabDragState.active){
            updateViewportPrefabDragPreviewFromMouse();
            if(deliveredViewportPrefabPayload){
                finalizeViewportPrefabDragPreview();
            }
        }
    }else if(viewportPrefabDragState.active){
        cancelViewportPrefabDragPreview();
    }

    if(playState == PlayState::Edit && targetScene && viewportCamera && viewportRect.valid && targetScene->getECS()){
        auto* ecs = targetScene->getECS();
        auto* components = ecs->getComponentManager();
        ensureEditorIconsLoaded();
        drawWorldGridOverlay(drawList, viewport);
        auto lightColorTint = [](const Math3D::Vec4& c) -> ImU32 {
            int r = (int)Math3D::Clamp(c.x * 255.0f, 0.0f, 255.0f);
            int g = (int)Math3D::Clamp(c.y * 255.0f, 0.0f, 255.0f);
            int b = (int)Math3D::Clamp(c.z * 255.0f, 0.0f, 255.0f);
            int a = (int)Math3D::Clamp(c.w * 255.0f, 0.0f, 255.0f);
            return IM_COL32(r, g, b, a);
        };

        if(showSceneGizmos){
            // Render helper icons for invisible scene objects while editing.
            const auto& entities = ecs->getEntityManager()->getEntities();
            for(const auto& entityPtr : entities){
                auto* entity = entityPtr.get();
                if(!entity){
                    continue;
                }
                auto* transformComp = components->getECSComponent<TransformComponent>(entity);
                if(!transformComp){
                    continue;
                }
                auto* rendererComp = components->getECSComponent<MeshRendererComponent>(entity);
                auto* cameraComp = components->getECSComponent<CameraComponent>(entity);
                auto* lightComp = components->getECSComponent<LightComponent>(entity);
                bool isSelected = (entity->getNodeUniqueID() == selectedEntityId);
                Math3D::Vec3 worldPos = buildWorldMatrix(entity, components).getPosition();

                if(cameraComp && cameraComp->camera && !isSelected){
                    drawBillboardIcon(drawList, worldPos, iconCamera, 56.0f, IM_COL32(255, 255, 255, 255));
                }

                if(lightComp && !isSelected){
                    PTexture icon = iconLightPoint;
                    if(lightComp->light.type == LightType::SPOT){
                        icon = iconLightSpot;
                    }else if(lightComp->light.type == LightType::DIRECTIONAL){
                        icon = iconLightDirectional;
                    }
                    if(icon){
                        drawBillboardIcon(drawList, worldPos, icon, 56.0f, lightColorTint(lightComp->light.color));
                    }
                }

                // Best-effort audio marker: if an entity is an editor-only helper with audio-style naming,
                // show the audio billboard in the editor viewport.
                if(iconAudio && !isSelected && !rendererComp && !cameraComp && !lightComp){
                    std::string lowerName = entity->getName();
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c){
                        return (char)std::tolower(c);
                    });
                    if(lowerName.find("audio") != std::string::npos ||
                       lowerName.find("sound") != std::string::npos ||
                       lowerName.find("speaker") != std::string::npos){
                        drawBillboardIcon(drawList, worldPos, iconAudio, 56.0f, IM_COL32(255, 255, 255, 255));
                    }
                }
            }
        }

        auto* entity = findEntityById(selectedEntityId);
        if(entity){
            if(auto* transformComp = components->getECSComponent<TransformComponent>(entity)){
                Math3D::Mat4 world = buildWorldMatrix(entity, components);
                Math3D::Vec3 worldPos = world.getPosition();
                Math3D::Transform worldTx = Math3D::Transform::fromMat4(world);
                Math3D::Vec3 worldForward = worldTx.forward();

                auto* boundsComp = components->getECSComponent<BoundsComponent>(entity);
                const bool boundsModeActive = boundsComp && BoundsEditState::IsActiveForEntity(selectedEntityId);
                if(boundsModeActive){
                    boundsWidget.draw(drawList, this, viewportCamera, viewport, world, *boundsComp);
                }else{
                    transformWidget.draw(drawList, this, viewportCamera, viewport, worldPos, transformComp->local, viewportHovered);
                    if(auto* cameraComp = components->getECSComponent<CameraComponent>(entity)){
                        if(cameraComp->camera){
                            cameraWidget.draw(
                                drawList,
                                this,
                                viewportCamera,
                                viewport,
                                worldPos,
                                worldForward,
                                cameraComp->camera->getSettings()
                            );
                        }
                    }
                    if(auto* lightComp = components->getECSComponent<LightComponent>(entity)){
                        lightWidget.draw(drawList, this, viewportCamera, viewport, worldPos, worldForward, lightComp->light, lightComp->syncTransform, lightComp->syncDirection);
                    }
                }
            }
        }

        auto drawAdaptiveFocusDebugOverlay = [&](float topY) -> float {
            if(!viewportCamera){
                return topY;
            }

            PScene componentScene;
            NeoECS::ECSEntity* cameraEntity = nullptr;
            if(editorCameraObject && editorCamera && viewportCamera == editorCamera && getECS()){
                componentScene = PScene(this, [](Scene*) {});
                cameraEntity = editorCameraObject->gameobject();
            }else if(targetScene && targetScene->getECS()){
                auto* targetManager = targetScene->getECS()->getComponentManager();
                const auto& targetEntities = targetScene->getECS()->getEntityManager()->getEntities();
                for(const auto& targetEntityPtr : targetEntities){
                    auto* targetEntity = targetEntityPtr.get();
                    if(!targetEntity){
                        continue;
                    }
                    auto* targetCameraComp = targetManager->getECSComponent<CameraComponent>(targetEntity);
                    if(targetCameraComp && targetCameraComp->camera == viewportCamera){
                        componentScene = targetScene;
                        cameraEntity = targetEntity;
                        break;
                    }
                }
            }

            if(!componentScene || !cameraEntity || !componentScene->getECS()){
                return topY;
            }

            auto* componentManager = componentScene->getECS()->getComponentManager();

            bool adaptiveFocusEnabled = false;
            bool adaptiveFocusDebugDraw = false;
            float configuredFocusDistance = 8.0f;
            float configuredFocusRange = 4.0f;
            std::shared_ptr<DepthOfFieldEffect> runtimeDofEffect;

            if(auto* dof = componentManager->getECSComponent<DepthOfFieldComponent>(cameraEntity);
               dof && IsComponentActive(dof)){
                adaptiveFocusEnabled = dof->adaptiveFocus;
                adaptiveFocusDebugDraw = dof->adaptiveFocusDebugDraw;
                configuredFocusDistance = dof->focusDistance;
                configuredFocusRange = dof->focusRange;
                runtimeDofEffect = dof->runtimeEffect;
            }else if(auto* stack = componentManager->getECSComponent<PostProcessingStackComponent>(cameraEntity);
                     stack && IsComponentActive(stack)){
                for(auto& effect : stack->effects){
                    if(effect.kind != PostProcessingEffectKind::DepthOfField || !effect.enabled){
                        continue;
                    }
                    adaptiveFocusEnabled = effect.depthOfField.adaptiveFocus;
                    adaptiveFocusDebugDraw = effect.depthOfField.adaptiveFocusDebugDraw;
                    configuredFocusDistance = effect.depthOfField.focusDistance;
                    configuredFocusRange = effect.depthOfField.focusRange;
                    runtimeDofEffect = effect.depthOfField.runtimeEffect;
                    break;
                }
            }

            if(!adaptiveFocusEnabled || !adaptiveFocusDebugDraw){
                return topY;
            }

            const Scene* adaptiveFocusSourceScene = targetScene ? targetScene.get() : componentScene.get();
            if(!adaptiveFocusSourceScene){
                return topY;
            }

            float hitDistance = 0.0f;
            bool hasHit = adaptiveFocusSourceScene->computeAdaptiveFocusDistanceFromSnapshotForCamera(viewportCamera, hitDistance);

            float resolvedFocusDistance = Math3D::Max(0.01f, configuredFocusDistance);
            float resolvedFocusRange = Math3D::Max(0.001f, configuredFocusRange);
            bool centerDepthValid = false;
            float centerDepthDistance = 0.0f;
            bool rayTargetValid = hasHit;
            float rayTargetDistance = hasHit ? hitDistance : 0.0f;
            bool usedFallback = !hasHit;
            float targetFocusDistance = resolvedFocusDistance;
            float fallbackFocusDistance = Math3D::Max(0.01f, configuredFocusDistance);
            if(runtimeDofEffect){
                resolvedFocusDistance = Math3D::Max(0.01f, runtimeDofEffect->getResolvedFocusDistance());
                resolvedFocusRange = Math3D::Max(0.001f, runtimeDofEffect->getResolvedFocusRange());
                centerDepthValid = runtimeDofEffect->getDebugAdaptiveCenterValid();
                centerDepthDistance = runtimeDofEffect->getDebugAdaptiveCenterDistance();
                rayTargetValid = runtimeDofEffect->getDebugAdaptiveRayValid();
                rayTargetDistance = runtimeDofEffect->getDebugAdaptiveRayDistance();
                usedFallback = runtimeDofEffect->getDebugAdaptiveUsedFallback();
                targetFocusDistance = runtimeDofEffect->getDebugAdaptiveTargetDistance();
                fallbackFocusDistance = runtimeDofEffect->getDebugAdaptiveFallbackDistance();
            }

            CameraSettings cameraSettings = viewportCamera->getSettings();
            Camera::SanitizePerspectivePlanes(cameraSettings.nearPlane, cameraSettings.farPlane);
            const float farLimit = Math3D::Max(1.0f, cameraSettings.farPlane * 0.95f);
            const float markerRange = resolvedFocusRange;
            float lineDistance = hasHit
                ? hitDistance
                : (resolvedFocusDistance + markerRange);
            const float rayStartDistance = Math3D::Max(0.11f, cameraSettings.nearPlane * 1.25f);
            lineDistance = Math3D::Clamp(lineDistance, rayStartDistance + 0.05f, farLimit);

            Math3D::Transform cameraTransform = viewportCamera->transform();
            Math3D::Vec3 rayOrigin = cameraTransform.position;
            Math3D::Vec3 rayDirection = cameraTransform.forward() * -1.0f;
            if(!std::isfinite(rayDirection.x) || !std::isfinite(rayDirection.y) || !std::isfinite(rayDirection.z) ||
               rayDirection.length() < 0.0001f){
                return topY;
            }
            rayDirection = rayDirection.normalize();

            Math3D::Vec3 rayRight = cameraTransform.right();
            if(!std::isfinite(rayRight.x) || !std::isfinite(rayRight.y) || !std::isfinite(rayRight.z) ||
               rayRight.length() < 0.0001f){
                rayRight = Math3D::Vec3(1.0f, 0.0f, 0.0f);
            }else{
                rayRight = rayRight.normalize();
            }

            auto projectToViewport = [&](const Math3D::Vec3& worldPoint, ImVec2& outPoint) -> bool {
                Math3D::Vec3 screenPoint = worldToScreen(viewportCamera, worldPoint, viewport.x, viewport.y, viewport.w, viewport.h);
                if(!screenPointInViewport(screenPoint, viewport)){
                    return false;
                }
                outPoint = ImVec2(screenPoint.x, screenPoint.y);
                return true;
            };

            Math3D::Vec3 lineStart = rayOrigin + (rayDirection * rayStartDistance);
            Math3D::Vec3 lineEnd = rayOrigin + (rayDirection * lineDistance);
            ImVec2 lineStartScreen;
            ImVec2 lineEndScreen;
            if(!projectToViewport(lineStart, lineStartScreen) || !projectToViewport(lineEnd, lineEndScreen)){
                return topY;
            }

            const ImU32 missColor = IM_COL32(52, 152, 219, 255); // #3498db
            const ImU32 hitColor = IM_COL32(46, 204, 113, 255);
            const ImU32 rayColor = hasHit ? hitColor : missColor;
            drawList->AddLine(lineStartScreen, lineEndScreen, rayColor, 2.0f);
            drawList->AddCircleFilled(lineEndScreen, 5.0f, rayColor);
            drawList->AddCircle(lineEndScreen, 6.0f, IM_COL32(0, 0, 0, 150), 0, 1.0f);

            auto drawDistanceMarker = [&](float distance, ImU32 color, float thickness, float halfWidthWorld){
                distance = Math3D::Clamp(distance, rayStartDistance, lineDistance);
                Math3D::Vec3 markerCenter = rayOrigin + (rayDirection * distance);
                Math3D::Vec3 markerA = markerCenter - (rayRight * halfWidthWorld);
                Math3D::Vec3 markerB = markerCenter + (rayRight * halfWidthWorld);
                ImVec2 markerAScreen;
                ImVec2 markerBScreen;
                if(projectToViewport(markerA, markerAScreen) && projectToViewport(markerB, markerBScreen)){
                    drawList->AddLine(markerAScreen, markerBScreen, color, thickness);
                }
            };

            const float markerHalfWidthWorld = Math3D::Clamp(lineDistance * 0.02f, 0.12f, 1.0f);
            drawDistanceMarker(resolvedFocusDistance, IM_COL32(241, 196, 15, 255), 2.2f, markerHalfWidthWorld);
            drawDistanceMarker(resolvedFocusDistance - markerRange, IM_COL32(255, 255, 255, 220), 1.7f, markerHalfWidthWorld * 0.7f);
            drawDistanceMarker(resolvedFocusDistance + markerRange, IM_COL32(255, 255, 255, 220), 1.7f, markerHalfWidthWorld * 0.7f);

            const std::string afDebugText = StringUtils::Format(
                "AF CenterDepth: %s %.2f\nAF RayTapAvg: %s %.2f\nAF Target: %.2f  Resolved: %.2f\nAF Fallback: %s %.2f",
                centerDepthValid ? "yes" : "no",
                centerDepthDistance,
                rayTargetValid ? "yes" : "no",
                rayTargetDistance,
                targetFocusDistance,
                resolvedFocusDistance,
                usedFallback ? "yes" : "no",
                fallbackFocusDistance
            );
            return drawViewportInfoPanel(
                drawList,
                viewport,
                topY,
                afDebugText,
                IM_COL32(0, 0, 0, 145),
                IM_COL32(201, 169, 37, 215)
            );
        };
        topLeftOverlayY = drawAdaptiveFocusDebugOverlay(topLeftOverlayY);

        if(previewTexture && previewTexture->getID() != 0){
            const float minPreviewW = 220.0f;
            const float minPreviewH = 160.0f;
            const float margin = 10.0f;
            float maxPreviewW = std::max(minPreviewW, viewportRect.w - (margin * 2.0f));
            float maxPreviewH = std::max(minPreviewH, viewportRect.h - (margin * 2.0f));
            bool forcePreviewLayout = false;

            if(!previewWindowInitialized){
                float defaultW = std::clamp(viewportRect.w * 0.32f, minPreviewW, std::min(380.0f, maxPreviewW));
                float defaultH = std::clamp((defaultW * (9.0f / 16.0f)) + 44.0f, minPreviewH, maxPreviewH);
                previewWindowSize = Math3D::Vec2(defaultW, defaultH);
                previewWindowLocalPos = Math3D::Vec2(viewportRect.w - defaultW - margin, 28.0f);
                previewWindowInitialized = true;
                forcePreviewLayout = true;
            }

            Math3D::Vec2 unclampedSize = previewWindowSize;
            previewWindowSize.x = std::clamp(previewWindowSize.x, minPreviewW, maxPreviewW);
            previewWindowSize.y = std::clamp(previewWindowSize.y, minPreviewH, maxPreviewH);
            if(!Math3D::AreClose(unclampedSize.x, previewWindowSize.x) ||
               !Math3D::AreClose(unclampedSize.y, previewWindowSize.y)){
                forcePreviewLayout = true;
            }

            if(previewWindowPinned){
                previewWindowLocalPos.x = viewportRect.w - previewWindowSize.x - margin;
                previewWindowLocalPos.y = 28.0f;
                forcePreviewLayout = true;
            }

            Math3D::Vec2 unclampedPos = previewWindowLocalPos;
            float maxLocalX = std::max(0.0f, viewportRect.w - previewWindowSize.x);
            float maxLocalY = std::max(0.0f, viewportRect.h - previewWindowSize.y);
            previewWindowLocalPos.x = std::clamp(previewWindowLocalPos.x, 0.0f, maxLocalX);
            previewWindowLocalPos.y = std::clamp(previewWindowLocalPos.y, 0.0f, maxLocalY);
            if(!Math3D::AreClose(unclampedPos.x, previewWindowLocalPos.x) ||
               !Math3D::AreClose(unclampedPos.y, previewWindowLocalPos.y)){
                forcePreviewLayout = true;
            }

            ImVec2 windowPos(viewportRect.x + previewWindowLocalPos.x, viewportRect.y + previewWindowLocalPos.y);
            ImVec2 windowSize(previewWindowSize.x, previewWindowSize.y);
            const ImGuiCond layoutCond = forcePreviewLayout ? ImGuiCond_Always : ImGuiCond_Appearing;

            ImGui::SetNextWindowPos(windowPos, layoutCond);
            ImGui::SetNextWindowSize(windowSize, layoutCond);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(minPreviewW, minPreviewH),
                ImVec2(maxPreviewW, maxPreviewH)
            );

            ImGuiWindowFlags previewFlags =
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
            if(previewWindowPinned){
                previewFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
            }

            ImVec2 appliedPos = windowPos;
            ImVec2 appliedSize = windowSize;
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.05f, 0.08f, 0.92f));
            if(ImGui::Begin("Preview##ViewportPreviewWindow", nullptr, previewFlags)){
                bool pinned = previewWindowPinned;
                if(ImGui::Checkbox("Pin", &pinned)){
                    previewWindowPinned = pinned;
                    if(previewWindowPinned){
                        previewWindowLocalPos.x = viewportRect.w - previewWindowSize.x - margin;
                        previewWindowLocalPos.y = 28.0f;
                    }
                }
                ImGui::Separator();

                ImVec2 avail = ImGui::GetContentRegionAvail();
                if(avail.x > 1.0f && avail.y > 1.0f){
                    float texW = (float)previewTexture->getWidth();
                    float texH = (float)previewTexture->getHeight();
                    if(texW <= 0.0f || texH <= 0.0f){
                        texW = 16.0f;
                        texH = 9.0f;
                    }
                    float texAspect = texW / texH;
                    float availAspect = avail.x / std::max(avail.y, 1.0f);
                    ImVec2 imageSize = avail;
                    if(availAspect > texAspect){
                        imageSize.x = avail.y * texAspect;
                    }else{
                        imageSize.y = avail.x / texAspect;
                    }

                    ImVec2 centered = ImGui::GetCursorPos();
                    if(avail.x > imageSize.x){
                        centered.x += (avail.x - imageSize.x) * 0.5f;
                    }
                    if(avail.y > imageSize.y){
                        centered.y += (avail.y - imageSize.y) * 0.5f;
                    }
                    ImGui::SetCursorPos(centered);

                    ImTextureID previewTexId = (ImTextureID)(intptr_t)previewTexture->getID();
                    ImGui::Image(previewTexId, imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                }
            }
            appliedPos = ImGui::GetWindowPos();
            appliedSize = ImGui::GetWindowSize();
            ImGui::End();
            ImGui::PopStyleColor();

            previewWindowLocalPos = Math3D::Vec2(appliedPos.x - viewportRect.x, appliedPos.y - viewportRect.y);
            previewWindowSize = Math3D::Vec2(appliedSize.x, appliedSize.y);
        }
    }

    if(showScenePerformanceInfo && viewport.valid && targetScene){
        const ScenePerformanceOverlayCounts counts = collectScenePerformanceOverlayCounts(targetScene.get());
        const Scene::DebugStats& debugStats = targetScene->getDebugStats();
        const float snapshotMs = debugStats.snapshotMs.load(std::memory_order_relaxed);
        const float shadowMs = debugStats.shadowMs.load(std::memory_order_relaxed);
        const float drawMs = debugStats.drawMs.load(std::memory_order_relaxed);
        const float postFxMs = debugStats.postFxMs.load(std::memory_order_relaxed);
        const int drawCount = debugStats.drawCount.load(std::memory_order_relaxed);
        const int postFxEffectCount = debugStats.postFxEffectCount.load(std::memory_order_relaxed);

        float updateMs = 0.0f;
        float renderMs = 0.0f;
        float swapMs = 0.0f;
        float frameMs = 0.0f;
        float fps = 0.0f;
        const char* renderStrategy = "Unknown";
        if(GameEngine::Engine){
            updateMs = GameEngine::Engine->getLastUpdateMs();
            renderMs = GameEngine::Engine->getLastRenderMs();
            swapMs = GameEngine::Engine->getLastSwapMs();
            frameMs = renderMs + swapMs;
            fps = (frameMs > 0.0001f) ? (1000.0f / frameMs) : 0.0f;
            renderStrategy = engineRenderStrategyLabel(GameEngine::Engine->getRenderStrategy());
        }

        const std::string scenePerformanceText = StringUtils::Format(
            "Scene Performance\n"
            "FPS %.1f | Frame %.1f ms | Renderer %s\n"
            "Entities %d | Meshes %d | Lights %d | Cameras %d\n"
            "Draws %d | PostFX %d | Snapshot %.2f ms\n"
            "Shadow %.2f ms | Draw %.2f ms | PostFX %.2f ms\n"
            "Update %.2f ms | Render %.2f ms | Swap %.2f ms",
            fps,
            frameMs,
            renderStrategy,
            counts.entityCount,
            counts.meshCount,
            counts.lightCount,
            counts.cameraCount,
            drawCount,
            postFxEffectCount,
            snapshotMs,
            shadowMs,
            drawMs,
            postFxMs,
            updateMs,
            renderMs,
            swapMs
        );

        topLeftOverlayY = drawViewportInfoPanel(
            drawList,
            viewport,
            topLeftOverlayY,
            scenePerformanceText,
            IM_COL32(8, 12, 18, 182),
            IM_COL32(76, 122, 178, 215)
        );
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorScene::drawPropertiesPanel(float x, float y, float w, float h, bool lightweight){
    if(lightweight){
        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::Begin("Properties", nullptr, kPanelFlags);
        ImGui::TextDisabled("Panel throttled during play mode (10 Hz).");
        ImGui::End();
        return;
    }
    workspacePanel.setAssetRoot(assetRoot);
    PScene panelScene = targetScene;
    std::string panelSelectedEntityId = selectedEntityId;
    std::function<NeoECS::ECSEntity*(const std::string&)> findPanelEntityById = [this](const std::string& id) -> NeoECS::ECSEntity* {
        return findEntityById(id);
    };

    if(showEditorCameraSettings && editorCameraObject && editorCameraObject->gameobject() && getECS()){
        panelScene = PScene(this, [](Scene*) {});
        panelSelectedEntityId = editorCameraObject->gameobject()->getNodeUniqueID();
        findPanelEntityById = [this](const std::string& id) -> NeoECS::ECSEntity* {
            if(id.empty() || !getECS()){
                return nullptr;
            }
            auto* entityManager = getECS()->getEntityManager();
            const auto& entities = entityManager->getEntities();
            for(const auto& entityPtr : entities){
                auto* entity = entityPtr.get();
                if(entity && entity->getNodeUniqueID() == id){
                    return entity;
                }
            }
            return nullptr;
        };
    }

    propertiesPanel.draw(
        x,
        y,
        w,
        h,
        panelScene,
        assetRoot,
        selectedAssetPath,
        panelSelectedEntityId,
        findPanelEntityById
    );
}

void EditorScene::ensureEditorIconsLoaded(){
    if(editorIconsLoaded){
        return;
    }
    iconCamera = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/editor_icons/camera.png"));
    iconLightPoint = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/editor_icons/light_point.png"));
    iconLightSpot = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/editor_icons/light_spot.png"));
    iconLightDirectional = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/editor_icons/light_directional.png"));
    iconAudio = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/editor_icons/audio.png"));
    cameraWidget.setIcon(iconCamera);
    LightWidget::Icons icons;
    icons.point = iconLightPoint;
    icons.spot = iconLightSpot;
    icons.directional = iconLightDirectional;
    lightWidget.setIcons(icons);
    editorIconsLoaded = true;
}

void EditorScene::drawWorldGridOverlay(ImDrawList* drawList, const TransformWidget::Viewport& viewport) const{
    if(!drawList || !showSceneGrid || !viewport.valid || !viewportCamera){
        return;
    }

    const Math3D::Vec3 cameraPos = viewportCamera->transform().position;
    const CameraSettings cameraSettings = viewportCamera->getSettings();
    float farPlane = cameraSettings.farPlane;
    if(!cameraSettings.isOrtho){
        float nearPlane = cameraSettings.nearPlane;
        Camera::SanitizePerspectivePlanes(nearPlane, farPlane);
    }

    constexpr float kGridFadeStartUnits = 500.0f;
    constexpr float kGridFadeEndUnits = 700.0f;
    const float fadeStartSq = kGridFadeStartUnits * kGridFadeStartUnits;
    const float fadeEndSq = kGridFadeEndUnits * kGridFadeEndUnits;
    const float orthoSpan = Math3D::Max(cameraSettings.viewPlane.size.x, cameraSettings.viewPlane.size.y);
    const float baseRadius = cameraSettings.isOrtho
        ? Math3D::Max(32.0f, orthoSpan * 0.65f)
        : Math3D::Max(64.0f, farPlane * 0.75f);
    const float altitudeRadius = std::fabs(cameraPos.y) * 4.0f;
    const float coverageRadius = Math3D::Max(Math3D::Max(baseRadius, altitudeRadius), kGridFadeEndUnits + 8.0f);
    constexpr int kMaxHalfCells = 2048;
    const int halfCells = Math3D::Clamp(static_cast<int>(std::ceil(coverageRadius)), 32, kMaxHalfCells);

    const int centerX = static_cast<int>(std::floor(cameraPos.x));
    const int centerZ = static_cast<int>(std::floor(cameraPos.z));
    const int minX = centerX - halfCells;
    const int maxX = centerX + halfCells;
    const int minZ = centerZ - halfCells;
    const int maxZ = centerZ + halfCells;
    const float gridPlaneY = 0.0f;

    const glm::mat4 viewMatrix = (glm::mat4)viewportCamera->getViewMatrix();
    const glm::mat4 projectionMatrix = (glm::mat4)viewportCamera->getProjectionMatrix();
    const glm::mat4 clipFromWorld = projectionMatrix * viewMatrix;

    const ImVec2 clipMin(viewport.x, viewport.y);
    const ImVec2 clipMax(viewport.x + viewport.w, viewport.y + viewport.h);
    auto clipLineToViewport = [&](ImVec2& start, ImVec2& end) -> bool {
        float t0 = 0.0f;
        float t1 = 1.0f;
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;

        auto clipTest = [&](float p, float q) -> bool {
            if(Math3D::AreClose(p, 0.0f, 1e-6f)){
                return q >= 0.0f;
            }

            const float r = q / p;
            if(p < 0.0f){
                if(r > t1){
                    return false;
                }
                if(r > t0){
                    t0 = r;
                }
            }else{
                if(r < t0){
                    return false;
                }
                if(r < t1){
                    t1 = r;
                }
            }
            return true;
        };

        if(!clipTest(-dx, start.x - clipMin.x) ||
           !clipTest(dx, clipMax.x - start.x) ||
           !clipTest(-dy, start.y - clipMin.y) ||
           !clipTest(dy, clipMax.y - start.y) ||
           t1 < t0){
            return false;
        }

        const ImVec2 clippedStart(start.x + (dx * t0), start.y + (dy * t0));
        const ImVec2 clippedEnd(start.x + (dx * t1), start.y + (dy * t1));
        start = clippedStart;
        end = clippedEnd;
        return true;
    };

    auto drawGridSegment = [&](const Math3D::Vec3& worldStart, const Math3D::Vec3& worldEnd, ImU32 color, float thickness){
        const glm::vec4 clipStart = clipFromWorld * glm::vec4((glm::vec3)worldStart, 1.0f);
        const glm::vec4 clipEnd = clipFromWorld * glm::vec4((glm::vec3)worldEnd, 1.0f);
        if(!std::isfinite(clipStart.x) || !std::isfinite(clipStart.y) || !std::isfinite(clipStart.z) || !std::isfinite(clipStart.w) ||
           !std::isfinite(clipEnd.x) || !std::isfinite(clipEnd.y) || !std::isfinite(clipEnd.z) || !std::isfinite(clipEnd.w)){
            return;
        }

        float tMin = 0.0f;
        float tMax = 1.0f;
        auto clipAgainstPlane = [&](float startDistance, float endDistance) -> bool {
            const float delta = endDistance - startDistance;
            if(Math3D::AreClose(delta, 0.0f, 1e-6f)){
                return startDistance >= 0.0f;
            }

            const float t = -startDistance / delta;
            if(delta > 0.0f){
                tMin = Math3D::Max(tMin, t);
            }else{
                tMax = Math3D::Min(tMax, t);
            }
            return tMin <= tMax;
        };

        if(!clipAgainstPlane(clipStart.x + clipStart.w, clipEnd.x + clipEnd.w) || // left
           !clipAgainstPlane(clipStart.w - clipStart.x, clipEnd.w - clipEnd.x) || // right
           !clipAgainstPlane(clipStart.y + clipStart.w, clipEnd.y + clipEnd.w) || // bottom
           !clipAgainstPlane(clipStart.w - clipStart.y, clipEnd.w - clipEnd.y) || // top
           !clipAgainstPlane(clipStart.z + clipStart.w, clipEnd.z + clipEnd.w) || // near
           !clipAgainstPlane(clipStart.w - clipStart.z, clipEnd.w - clipEnd.z)){  // far
            return;
        }

        const glm::vec4 clippedStart = clipStart + ((clipEnd - clipStart) * tMin);
        const glm::vec4 clippedEnd = clipStart + ((clipEnd - clipStart) * tMax);
        if(Math3D::AreClose(clippedStart.w, 0.0f, 1e-6f) || Math3D::AreClose(clippedEnd.w, 0.0f, 1e-6f)){
            return;
        }

        const glm::vec3 ndcStart = glm::vec3(clippedStart) / clippedStart.w;
        const glm::vec3 ndcEnd = glm::vec3(clippedEnd) / clippedEnd.w;

        ImVec2 screenStart(
            viewport.x + ((ndcStart.x * 0.5f + 0.5f) * viewport.w),
            viewport.y + ((0.5f - ndcStart.y * 0.5f) * viewport.h)
        );
        ImVec2 screenEnd(
            viewport.x + ((ndcEnd.x * 0.5f + 0.5f) * viewport.w),
            viewport.y + ((0.5f - ndcEnd.y * 0.5f) * viewport.h)
        );

        if(!clipLineToViewport(screenStart, screenEnd)){
            return;
        }
        drawList->AddLine(screenStart, screenEnd, color, thickness);
    };

    constexpr int kMajorStep = 10;
    const ImU32 minorColor = IM_COL32(139, 149, 165, 70);
    const ImU32 majorColor = IM_COL32(168, 182, 204, 118);
    const ImU32 axisXColor = IM_COL32(221, 98, 89, 188);
    const ImU32 axisZColor = IM_COL32(73, 146, 219, 188);
    auto colorWithOpacity = [&](ImU32 color, float opacity) -> ImU32 {
        opacity = Math3D::Clamp(opacity, 0.0f, 1.0f);
        const unsigned int baseAlpha = (color >> IM_COL32_A_SHIFT) & 0xFFu;
        const unsigned int scaledAlpha = (unsigned int)Math3D::Clamp(baseAlpha * opacity, 0.0f, 255.0f);
        return (color & ~IM_COL32_A_MASK) | (scaledAlpha << IM_COL32_A_SHIFT);
    };
    auto computeFadeAlpha = [&](float radius) -> float {
        if(radius <= kGridFadeStartUnits){
            return 1.0f;
        }
        if(radius >= kGridFadeEndUnits){
            return 0.0f;
        }
        return 1.0f - ((radius - kGridFadeStartUnits) / (kGridFadeEndUnits - kGridFadeStartUnits));
    };
    auto drawGridSegmentFaded = [&](const Math3D::Vec3& worldStart,
                                    const Math3D::Vec3& worldEnd,
                                    ImU32 color,
                                    float thickness,
                                    float opacity){
        if(opacity <= 0.001f){
            return;
        }
        drawGridSegment(worldStart, worldEnd, colorWithOpacity(color, opacity), thickness);
    };

    drawList->PushClipRect(clipMin, clipMax, true);
    const float cameraX = cameraPos.x;
    const float cameraZ = cameraPos.z;
    for(int x = minX; x <= maxX; ++x){
        const bool isAxis = (x == 0);
        const bool isMajor = (std::abs(x) % kMajorStep) == 0;
        const ImU32 lineColorBase = isAxis ? axisZColor : (isMajor ? majorColor : minorColor);
        const float thickness = isAxis ? 1.7f : (isMajor ? 1.25f : 1.0f);
        const float worldX = static_cast<float>(x);
        const float dx = std::fabs(worldX - cameraX);
        if(dx > kGridFadeEndUnits){
            continue;
        }

        const float fadeZHalf = std::sqrt(Math3D::Max(0.0f, fadeEndSq - (dx * dx)));
        const float fadeMinZ = Math3D::Max(static_cast<float>(minZ), cameraZ - fadeZHalf);
        const float fadeMaxZ = Math3D::Min(static_cast<float>(maxZ), cameraZ + fadeZHalf);
        if(fadeMaxZ <= fadeMinZ){
            continue;
        }

        if(dx < kGridFadeStartUnits){
            const float fullZHalf = std::sqrt(Math3D::Max(0.0f, fadeStartSq - (dx * dx)));
            const float fullMinZ = Math3D::Max(fadeMinZ, cameraZ - fullZHalf);
            const float fullMaxZ = Math3D::Min(fadeMaxZ, cameraZ + fullZHalf);

            if(fullMaxZ > fullMinZ){
                drawGridSegment(
                    Math3D::Vec3(worldX, gridPlaneY, fullMinZ),
                    Math3D::Vec3(worldX, gridPlaneY, fullMaxZ),
                    lineColorBase,
                    thickness
                );
            }

            if(fullMinZ > fadeMinZ){
                const float midZ = (fadeMinZ + fullMinZ) * 0.5f;
                const float radius = std::sqrt((dx * dx) + ((midZ - cameraZ) * (midZ - cameraZ)));
                drawGridSegmentFaded(
                    Math3D::Vec3(worldX, gridPlaneY, fadeMinZ),
                    Math3D::Vec3(worldX, gridPlaneY, fullMinZ),
                    lineColorBase,
                    thickness,
                    computeFadeAlpha(radius)
                );
            }
            if(fadeMaxZ > fullMaxZ){
                const float midZ = (fullMaxZ + fadeMaxZ) * 0.5f;
                const float radius = std::sqrt((dx * dx) + ((midZ - cameraZ) * (midZ - cameraZ)));
                drawGridSegmentFaded(
                    Math3D::Vec3(worldX, gridPlaneY, fullMaxZ),
                    Math3D::Vec3(worldX, gridPlaneY, fadeMaxZ),
                    lineColorBase,
                    thickness,
                    computeFadeAlpha(radius)
                );
            }
        }else{
            const float midZ = (fadeMinZ + fadeMaxZ) * 0.5f;
            const float radius = std::sqrt((dx * dx) + ((midZ - cameraZ) * (midZ - cameraZ)));
            drawGridSegmentFaded(
                Math3D::Vec3(worldX, gridPlaneY, fadeMinZ),
                Math3D::Vec3(worldX, gridPlaneY, fadeMaxZ),
                lineColorBase,
                thickness,
                computeFadeAlpha(radius)
            );
        }
    }

    for(int z = minZ; z <= maxZ; ++z){
        const bool isAxis = (z == 0);
        const bool isMajor = (std::abs(z) % kMajorStep) == 0;
        const ImU32 lineColorBase = isAxis ? axisXColor : (isMajor ? majorColor : minorColor);
        const float thickness = isAxis ? 1.7f : (isMajor ? 1.25f : 1.0f);
        const float worldZ = static_cast<float>(z);
        const float dz = std::fabs(worldZ - cameraZ);
        if(dz > kGridFadeEndUnits){
            continue;
        }

        const float fadeXHalf = std::sqrt(Math3D::Max(0.0f, fadeEndSq - (dz * dz)));
        const float fadeMinX = Math3D::Max(static_cast<float>(minX), cameraX - fadeXHalf);
        const float fadeMaxX = Math3D::Min(static_cast<float>(maxX), cameraX + fadeXHalf);
        if(fadeMaxX <= fadeMinX){
            continue;
        }

        if(dz < kGridFadeStartUnits){
            const float fullXHalf = std::sqrt(Math3D::Max(0.0f, fadeStartSq - (dz * dz)));
            const float fullMinX = Math3D::Max(fadeMinX, cameraX - fullXHalf);
            const float fullMaxX = Math3D::Min(fadeMaxX, cameraX + fullXHalf);

            if(fullMaxX > fullMinX){
                drawGridSegment(
                    Math3D::Vec3(fullMinX, gridPlaneY, worldZ),
                    Math3D::Vec3(fullMaxX, gridPlaneY, worldZ),
                    lineColorBase,
                    thickness
                );
            }

            if(fullMinX > fadeMinX){
                const float midX = (fadeMinX + fullMinX) * 0.5f;
                const float radius = std::sqrt((dz * dz) + ((midX - cameraX) * (midX - cameraX)));
                drawGridSegmentFaded(
                    Math3D::Vec3(fadeMinX, gridPlaneY, worldZ),
                    Math3D::Vec3(fullMinX, gridPlaneY, worldZ),
                    lineColorBase,
                    thickness,
                    computeFadeAlpha(radius)
                );
            }
            if(fadeMaxX > fullMaxX){
                const float midX = (fullMaxX + fadeMaxX) * 0.5f;
                const float radius = std::sqrt((dz * dz) + ((midX - cameraX) * (midX - cameraX)));
                drawGridSegmentFaded(
                    Math3D::Vec3(fullMaxX, gridPlaneY, worldZ),
                    Math3D::Vec3(fadeMaxX, gridPlaneY, worldZ),
                    lineColorBase,
                    thickness,
                    computeFadeAlpha(radius)
                );
            }
        }else{
            const float midX = (fadeMinX + fadeMaxX) * 0.5f;
            const float radius = std::sqrt((dz * dz) + ((midX - cameraX) * (midX - cameraX)));
            drawGridSegmentFaded(
                Math3D::Vec3(fadeMinX, gridPlaneY, worldZ),
                Math3D::Vec3(fadeMaxX, gridPlaneY, worldZ),
                lineColorBase,
                thickness,
                computeFadeAlpha(radius)
            );
        }
    }
    drawList->PopClipRect();
}

void EditorScene::drawBillboardIcon(ImDrawList* drawList,
                                    const Math3D::Vec3& worldPos,
                                    PTexture texture,
                                    float sizePx,
                                    ImU32 tint) const{
    if(!drawList || !texture || !viewportCamera || !viewportRect.valid){
        return;
    }
    TransformWidget::Viewport viewport{viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, viewportRect.valid};
    Math3D::Vec3 screen = worldToScreen(viewportCamera, worldPos, viewport.x, viewport.y, viewport.w, viewport.h);
    if(!screenPointInViewport(screen, viewport)){
        return;
    }
    float half = sizePx * 0.5f;
    ImVec2 pmin(screen.x - half, screen.y - half);
    ImVec2 pmax(screen.x + half, screen.y + half);
    ImTextureID texId = (ImTextureID)(intptr_t)texture->getID();
    drawList->AddImage(texId, pmin, pmax, ImVec2(0, 1), ImVec2(1, 0), tint);
}

void EditorScene::ensurePointLightBounds(NeoECS::ECSEntity* entity, float radius){
    if(!entity || !targetScene || !targetScene->getECS()){
        return;
    }
    auto* manager = targetScene->getECS()->getComponentManager();
    if(auto* bounds = manager->getECSComponent<BoundsComponent>(entity)){
        bounds->type = BoundsType::Sphere;
        bounds->radius = radius;
    }else{
        auto* ctx = targetScene->getECS()->getContext();
        std::unique_ptr<NeoECS::GameObject> wrapper(NeoECS::GameObject::CreateFromECSEntity(ctx, entity));
        if(wrapper && wrapper->addComponent<BoundsComponent>()){
            if(auto* newBounds = manager->getECSComponent<BoundsComponent>(entity)){
                newBounds->type = BoundsType::Sphere;
                newBounds->radius = radius;
            }
        }
    }
}

void EditorScene::drawAssetsPanel(float x, float y, float w, float h, bool lightweight){
    if(lightweight){
        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::Begin("Workspace", nullptr, kPanelFlags);
        ImGui::TextDisabled("Panel throttled during play mode (10 Hz).");
        ImGui::TextDisabled("Hover panel to force full refresh.");
        ImGui::End();
        return;
    }
    workspacePanel.setAssetRoot(assetRoot);
    workspacePanel.draw(
        x,
        y,
        w,
        h,
        selectedAssetPath,
        [this](const std::string& entityId, const std::filesystem::path& exportDirectory, std::string* outError) -> bool {
            const bool ok = exportEntityAsPrefabToDirectory(entityId, exportDirectory);
            if(!ok && outError){
                *outError = ioStatusMessage.empty() ? "Create Prefab failed." : ioStatusMessage;
            }
            return ok;
        }
    );
}

NeoECS::ECSEntity* EditorScene::findEntityById(const std::string& id) const{
    if(id.empty() || !targetScene || !targetScene->getECS()) return nullptr;
    auto* entityManager = targetScene->getECS()->getEntityManager();
    const auto& entities = entityManager->getEntities();
    for(const auto& entityPtr : entities){
        auto* entity = entityPtr.get();
        if(entity && entity->getNodeUniqueID() == id){
            return entity;
        }
    }
    return nullptr;
}

PCamera EditorScene::resolveSelectedTargetCamera() const{
    if(!targetScene || !targetScene->getECS()){
        return nullptr;
    }
    auto* entity = findEntityById(selectedEntityId);
    if(!entity){
        return nullptr;
    }
    auto* components = targetScene->getECS()->getComponentManager();
    auto* cameraComp = components->getECSComponent<CameraComponent>(entity);
    if(!cameraComp || !cameraComp->camera){
        return nullptr;
    }
    return cameraComp->camera;
}

void EditorScene::dispose(){
    cancelViewportPrefabDragPreview();
    flushEditHistoryObservation(true);
    observeTransientEditorSessionState();
    std::string saveCameraError;
    if(!saveEditorCameraToPrefab(&saveCameraError)){
        LogBot.Log(LOG_WARN, "Failed to save editor camera prefab during dispose: %s", saveCameraError.c_str());
    }
    flushEditorSessionAutosave(true, false);
    setEditorCameraSettingsOpen(false);
    if(playViewportMouseRectConstrained){
        if(auto* window = getWindow()){
            SDL_SetWindowMouseRect(window->getWindowPtr(), nullptr);
        }
        playViewportMouseRectConstrained = false;
    }
    if(targetScene){
        targetScene->dispose();
    }
    Scene::dispose();
}
