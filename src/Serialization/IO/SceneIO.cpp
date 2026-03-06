#include "Serialization/IO/SceneIO.h"

#include <algorithm>

#include "ECS/Core/ECSComponents.h"
#include "Serialization/IO/ComponentDependencyCollector.h"
#include "Serialization/IO/EntitySnapshotIO.h"
#include "Serialization/Json/JsonUtils.h"

namespace {

void setSceneIoError(std::string* outError, const std::string& message){
    if(outError){
        *outError = message;
    }
}

bool compareEntityUniqueIdAsc(const NeoECS::ECSEntity* a, const NeoECS::ECSEntity* b){
    if(a == b){
        return false;
    }
    if(!a){
        return false;
    }
    if(!b){
        return true;
    }
    if(a->getNodeUniqueID() != b->getNodeUniqueID()){
        return a->getNodeUniqueID() < b->getNodeUniqueID();
    }
    return a->getName() < b->getName();
}

bool buildSceneSettingsRawJson(
    PScene scene,
    const Serialization::SnapshotIO::SnapshotBuildResult& snapshot,
    JsonSchema::RawJsonValue& outSettings,
    std::string* outError
){
    outSettings.hasValue = true;
    outSettings.json = "{}";

    if(!scene || !scene->getECS()){
        return true;
    }

    JsonUtils::MutableDocument doc;
    yyjson_mut_val* root = doc.setRootObject();
    if(!root){
        setSceneIoError(outError, "Failed to allocate sceneSettings object.");
        return false;
    }

    if(!JsonUtils::MutObjAddBool(doc.get(), root, "outlineEnabled", scene->isOutlineEnabled())){
        setSceneIoError(outError, "Failed to write sceneSettings.outlineEnabled.");
        return false;
    }

    auto* entityManager = scene->getECS()->getEntityManager();
    if(entityManager){
        NeoECS::ECSEntity* selectedEntity = nullptr;
        const std::string selectedId = scene->getSelectedEntityId();
        if(!selectedId.empty()){
            selectedEntity = entityManager->getSpecificEntity(selectedId);
        }
        auto selectedSnapshotIt = snapshot.entityToSnapshotId.find(selectedEntity);
        if(selectedEntity && selectedSnapshotIt != snapshot.entityToSnapshotId.end()){
            if(!JsonUtils::MutObjAddUInt64(doc.get(), root, "selectedEntitySnapshotId", selectedSnapshotIt->second)){
                setSceneIoError(outError, "Failed to write sceneSettings.selectedEntitySnapshotId.");
                return false;
            }
        }
    }

    PCamera preferredCamera = scene->getPreferredCamera();
    if(preferredCamera){
        auto* manager = scene->getECS()->getComponentManager();
        if(manager){
            for(const auto& kv : snapshot.entityToSnapshotId){
                NeoECS::ECSEntity* entity = kv.first;
                if(!entity){
                    continue;
                }
                auto* cameraComp = manager->getECSComponent<CameraComponent>(entity);
                if(cameraComp && IsComponentActive(cameraComp) && cameraComp->camera == preferredCamera){
                    if(!JsonUtils::MutObjAddUInt64(doc.get(), root, "preferredCameraSnapshotId", kv.second)){
                        setSceneIoError(outError, "Failed to write sceneSettings.preferredCameraSnapshotId.");
                        return false;
                    }
                    break;
                }
            }
        }
    }

    if(!JsonUtils::WriteDocumentToString(doc, outSettings.json, outError, false)){
        if(outError && !outError->empty()){
            *outError = "Failed to encode sceneSettings JSON: " + *outError;
        }
        return false;
    }

    outSettings.hasValue = true;
    return true;
}

bool applySceneSettingsRawJson(
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
    yyjson_val* root = doc.root();
    if(!root || !yyjson_is_obj(root)){
        setSceneIoError(outError, "sceneSettings must be a JSON object.");
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

} // namespace

namespace SceneIO {

bool BuildSchemaFromScene(
    PScene scene,
    JsonSchema::SceneSchema& outSchema,
    const SceneSaveOptions& options,
    std::string* outError
){
    if(!scene || !scene->getECS()){
        setSceneIoError(outError, "Cannot build scene schema: scene/ECS is null.");
        return false;
    }

    outSchema.Clear();
    outSchema.metadata = options.metadata;
    if(outSchema.metadata.name.empty()){
        outSchema.metadata.name = "Scene";
    }

    std::vector<NeoECS::ECSEntity*> roots;
    if(NeoECS::ECSEntity* sceneRoot = scene->getSceneRootEntity()){
        roots.reserve(sceneRoot->children().size());
        for(const auto& kv : sceneRoot->children()){
            if(kv.second){
                roots.push_back(kv.second);
            }
        }
    }
    std::sort(roots.begin(), roots.end(), compareEntityUniqueIdAsc);

    Serialization::SnapshotIO::SnapshotBuildResult snapshot;
    if(!roots.empty()){
        Serialization::SnapshotIO::SnapshotBuildOptions snapshotOptions;
        snapshotOptions.registry = options.registry;
        if(!Serialization::SnapshotIO::BuildSnapshotFromEntityRoots(scene, roots, snapshot, snapshotOptions, outError)){
            if(outError && !outError->empty()){
                *outError = "Failed to build scene snapshot: " + *outError;
            }
            return false;
        }
        outSchema.rootEntityIds = snapshot.rootEntityIds;
        outSchema.entities = std::move(snapshot.entities);
    }else{
        outSchema.rootEntityIds.clear();
        outSchema.entities.clear();
    }

    if(options.autoCollectDependencies){
        std::vector<NeoECS::ECSEntity*> snapshotEntities;
        snapshotEntities.reserve(snapshot.entityToSnapshotId.size());
        for(const auto& kv : snapshot.entityToSnapshotId){
            snapshotEntities.push_back(kv.first);
        }
        Serialization::CollectAssetDependenciesFromEntities(
            scene->getECS()->getComponentManager(),
            snapshotEntities,
            outSchema.dependencies);
    }else{
        outSchema.dependencies = options.dependencies;
        std::sort(outSchema.dependencies.begin(), outSchema.dependencies.end());
        outSchema.dependencies.erase(
            std::unique(outSchema.dependencies.begin(), outSchema.dependencies.end()),
            outSchema.dependencies.end());
    }

    if(options.includeSceneSettings){
        if(options.sceneSettingsOverride.hasValue){
            outSchema.sceneSettings = options.sceneSettingsOverride;
            if(!outSchema.sceneSettings.hasValue || outSchema.sceneSettings.json.empty()){
                outSchema.sceneSettings.hasValue = true;
                outSchema.sceneSettings.json = "{}";
            }
        }else{
            if(!buildSceneSettingsRawJson(scene, snapshot, outSchema.sceneSettings, outError)){
                return false;
            }
        }
    }else{
        outSchema.sceneSettings.hasValue = true;
        outSchema.sceneSettings.json = "{}";
    }

    if(options.includeEditorState){
        outSchema.editorState = options.editorState;
        if(outSchema.editorState.hasValue && outSchema.editorState.json.empty()){
            outSchema.editorState.json = "{}";
        }
    }else{
        outSchema.editorState.Clear();
    }

    return true;
}

bool SaveSceneToAbsolutePath(
    PScene scene,
    const std::filesystem::path& path,
    const SceneSaveOptions& options,
    std::string* outError
){
    JsonSchema::SceneSchema schema;
    if(!BuildSchemaFromScene(scene, schema, options, outError)){
        return false;
    }
    return schema.SaveToAbsolutePath(path, outError, true);
}

bool SaveSceneToAssetRef(
    PScene scene,
    const std::string& assetRef,
    const SceneSaveOptions& options,
    std::string* outError
){
    JsonSchema::SceneSchema schema;
    if(!BuildSchemaFromScene(scene, schema, options, outError)){
        return false;
    }
    return schema.SaveToAssetRef(assetRef, outError, true);
}

bool LoadSchemaFromAbsolutePath(
    const std::filesystem::path& path,
    JsonSchema::SceneSchema& outSchema,
    std::string* outError
){
    outSchema.Clear();
    return outSchema.LoadFromAbsolutePath(path, outError);
}

bool LoadSchemaFromAssetRef(
    const std::string& assetRef,
    JsonSchema::SceneSchema& outSchema,
    std::string* outError
){
    outSchema.Clear();
    return outSchema.LoadFromAssetRef(assetRef, outError);
}

bool ApplySchemaToScene(
    PScene scene,
    const JsonSchema::SceneSchema& schema,
    const SceneLoadOptions& options,
    SceneLoadResult* outResult,
    std::string* outError
){
    if(!scene || !scene->getECS()){
        setSceneIoError(outError, "Cannot apply scene schema: scene/ECS is null.");
        return false;
    }

    if(options.clearExistingScene){
        if(!Serialization::SnapshotIO::DestroySceneRootChildren(scene, outError)){
            return false;
        }
    }

    Serialization::SnapshotIO::SnapshotInstantiateResult instantiateResult;
    Serialization::SnapshotIO::SnapshotInstantiateOptions instantiateOptions;
    instantiateOptions.registry = options.registry;
    instantiateOptions.destinationParent = nullptr;
    if(!Serialization::SnapshotIO::InstantiateSnapshotIntoScene(
            scene,
            schema.entities,
            schema.rootEntityIds,
            instantiateResult,
            instantiateOptions,
            outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to instantiate scene snapshot: " + *outError;
        }
        return false;
    }

    if(options.applySceneSettings){
        if(!applySceneSettingsRawJson(scene, schema.sceneSettings, instantiateResult.snapshotIdToEntity, outError)){
            return false;
        }
    }

    if(outResult){
        outResult->rootObjects = std::move(instantiateResult.rootObjects);
        outResult->snapshotIdToEntity = std::move(instantiateResult.snapshotIdToEntity);
        outResult->editorState = schema.editorState;
    }
    return true;
}

bool LoadSceneFromAbsolutePath(
    PScene scene,
    const std::filesystem::path& path,
    const SceneLoadOptions& options,
    SceneLoadResult* outResult,
    std::string* outError
){
    JsonSchema::SceneSchema schema;
    if(!LoadSchemaFromAbsolutePath(path, schema, outError)){
        return false;
    }
    return ApplySchemaToScene(scene, schema, options, outResult, outError);
}

bool LoadSceneFromAssetRef(
    PScene scene,
    const std::string& assetRef,
    const SceneLoadOptions& options,
    SceneLoadResult* outResult,
    std::string* outError
){
    JsonSchema::SceneSchema schema;
    if(!LoadSchemaFromAssetRef(assetRef, schema, outError)){
        return false;
    }
    return ApplySchemaToScene(scene, schema, options, outResult, outError);
}

} // namespace SceneIO
