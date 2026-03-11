/**
 * @file src/Serialization/IO/PrefabIO.cpp
 * @brief Implementation for PrefabIO.
 */

#include "Serialization/IO/PrefabIO.h"

#include "Serialization/IO/ComponentDependencyCollector.h"
#include "Serialization/IO/EntitySnapshotIO.h"
#include "Serialization/Json/JsonUtils.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace {

void setPrefabError(std::string* outError, const std::string& message){
    if(outError){
        *outError = message;
    }
}

bool parseOptionalObjectJson(const JsonSchema::RawJsonValue& raw, JsonUtils::Document& outDoc, yyjson_val*& outRoot, std::string* outError){
    outRoot = nullptr;
    if(!raw.hasValue || raw.json.empty()){
        return true;
    }
    if(!JsonUtils::LoadDocumentFromText(raw.json, outDoc, outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to parse embedded JSON object: " + *outError;
        }
        return false;
    }
    outRoot = outDoc.root();
    if(!outRoot || !yyjson_is_obj(outRoot)){
        setPrefabError(outError, "Embedded JSON payload must be an object.");
        return false;
    }
    return true;
}

bool captureJsonValue(yyjson_val* value, std::string& outJson, std::string* outError, const char* debugName){
    if(!value){
        setPrefabError(outError, std::string("Cannot capture null JSON value for '") + (debugName ? debugName : "value") + "'.");
        return false;
    }

    yyjson_write_err err{};
    size_t len = 0;
    char* json = yyjson_val_write_opts(value, YYJSON_WRITE_NOFLAG, nullptr, &len, &err);
    if(!json){
        std::string message = std::string("Failed to serialize JSON value for '") + (debugName ? debugName : "value") + "'";
        if(err.msg){
            message += ": ";
            message += err.msg;
        }
        setPrefabError(outError, message);
        return false;
    }

    outJson.assign(json, len);
    std::free(json);
    return true;
}

using ComponentRecord = JsonSchema::EntitySnapshotSchemaBase::ComponentRecord;
using EntityRecord = JsonSchema::EntitySnapshotSchemaBase::EntityRecord;

/// @brief Represents Parsed Variant Definition data.
struct ParsedVariantDefinition {
    std::string basePrefabRef;
    bool inheritBaseChildren = true;
    bool inheritBaseComponents = true;
    JsonSchema::RawJsonValue overrides;
};

bool parseVariantDefinition(const JsonSchema::PrefabSchema& schema, ParsedVariantDefinition& outVariant, std::string* outError){
    outVariant = ParsedVariantDefinition{};
    if(!schema.variant.hasValue || schema.variant.json.empty()){
        return true;
    }

    JsonUtils::Document doc;
    if(!JsonUtils::LoadDocumentFromText(schema.variant.json, doc, outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to parse variant metadata JSON: " + *outError;
        }
        return false;
    }

    yyjson_val* root = doc.root();
    if(!root || !yyjson_is_obj(root)){
        setPrefabError(outError, "Variant metadata must be a JSON object.");
        return false;
    }

    if(JsonUtils::ObjHasKey(root, "basePrefabRef") &&
       !JsonUtils::TryGetString(root, "basePrefabRef", outVariant.basePrefabRef)){
        setPrefabError(outError, "variant.basePrefabRef must be a string.");
        return false;
    }
    if(JsonUtils::ObjHasKey(root, "inheritBaseChildren") &&
       !JsonUtils::TryGetBool(root, "inheritBaseChildren", outVariant.inheritBaseChildren)){
        setPrefabError(outError, "variant.inheritBaseChildren must be a bool.");
        return false;
    }
    if(JsonUtils::ObjHasKey(root, "inheritBaseComponents") &&
       !JsonUtils::TryGetBool(root, "inheritBaseComponents", outVariant.inheritBaseComponents)){
        setPrefabError(outError, "variant.inheritBaseComponents must be a bool.");
        return false;
    }

    if(JsonUtils::ObjHasKey(root, "overrides")){
        yyjson_val* overrideValue = JsonUtils::ObjGet(root, "overrides");
        if(!overrideValue || !yyjson_is_obj(overrideValue)){
            setPrefabError(outError, "variant.overrides must be an object when present.");
            return false;
        }
        outVariant.overrides.hasValue = true;
        if(!captureJsonValue(overrideValue, outVariant.overrides.json, outError, "variant.overrides")){
            return false;
        }
    }

    return true;
}

void mergeDependencies(
    const std::vector<std::string>& baseDependencies,
    const std::vector<std::string>& overlayDependencies,
    std::vector<std::string>& outDependencies
){
    outDependencies = baseDependencies;
    outDependencies.insert(outDependencies.end(), overlayDependencies.begin(), overlayDependencies.end());
    std::sort(outDependencies.begin(), outDependencies.end());
    outDependencies.erase(
        std::unique(outDependencies.begin(), outDependencies.end()),
        outDependencies.end());
}

void mergeComponentRecords(
    const std::vector<ComponentRecord>& baseComponents,
    const std::vector<ComponentRecord>& overlayComponents,
    std::vector<ComponentRecord>& outComponents
){
    outComponents = baseComponents;

    std::unordered_map<std::string, size_t> indexByType;
    indexByType.reserve(outComponents.size());
    for(size_t i = 0; i < outComponents.size(); ++i){
        indexByType[outComponents[i].type] = i;
    }

    for(const ComponentRecord& overlay : overlayComponents){
        auto existing = indexByType.find(overlay.type);
        if(existing == indexByType.end()){
            indexByType[overlay.type] = outComponents.size();
            outComponents.push_back(overlay);
        }else{
            outComponents[existing->second] = overlay;
        }
    }
}

EntityRecord mergeEntityRecord(
    const EntityRecord& baseEntity,
    const EntityRecord& overlayEntity,
    bool inheritBaseComponents
){
    EntityRecord merged = overlayEntity;
    if(inheritBaseComponents){
        mergeComponentRecords(baseEntity.components, overlayEntity.components, merged.components);
    }
    return merged;
}

void dedupeRootEntityIds(std::vector<std::uint64_t>& rootEntityIds){
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(rootEntityIds.size());

    std::vector<std::uint64_t> deduped;
    deduped.reserve(rootEntityIds.size());
    for(std::uint64_t id : rootEntityIds){
        if(seen.insert(id).second){
            deduped.push_back(id);
        }
    }
    rootEntityIds = std::move(deduped);
}

void mergePrefabSchemas(
    const JsonSchema::PrefabSchema& baseSchema,
    const JsonSchema::PrefabSchema& overlaySchema,
    const ParsedVariantDefinition& variant,
    JsonSchema::PrefabSchema& outMergedSchema
){
    outMergedSchema = overlaySchema;

    if(variant.inheritBaseChildren){
        outMergedSchema.rootEntityIds = baseSchema.rootEntityIds;
        outMergedSchema.entities = baseSchema.entities;

        std::unordered_map<std::uint64_t, size_t> entityIndexById;
        entityIndexById.reserve(outMergedSchema.entities.size());
        for(size_t i = 0; i < outMergedSchema.entities.size(); ++i){
            entityIndexById[outMergedSchema.entities[i].id] = i;
        }

        for(const EntityRecord& overlayEntity : overlaySchema.entities){
            auto existing = entityIndexById.find(overlayEntity.id);
            if(existing == entityIndexById.end()){
                entityIndexById[overlayEntity.id] = outMergedSchema.entities.size();
                outMergedSchema.entities.push_back(overlayEntity);
            }else{
                outMergedSchema.entities[existing->second] = mergeEntityRecord(
                    outMergedSchema.entities[existing->second],
                    overlayEntity,
                    variant.inheritBaseComponents);
            }
        }

        outMergedSchema.rootEntityIds.insert(
            outMergedSchema.rootEntityIds.end(),
            overlaySchema.rootEntityIds.begin(),
            overlaySchema.rootEntityIds.end());
        dedupeRootEntityIds(outMergedSchema.rootEntityIds);
    }

    mergeDependencies(baseSchema.dependencies, overlaySchema.dependencies, outMergedSchema.dependencies);

    if(outMergedSchema.metadata.name.empty()){
        outMergedSchema.metadata.name = baseSchema.metadata.name;
    }
    if(outMergedSchema.metadata.description.empty()){
        outMergedSchema.metadata.description = baseSchema.metadata.description;
    }
    if(outMergedSchema.metadata.sourceAssetRef.empty()){
        outMergedSchema.metadata.sourceAssetRef = baseSchema.metadata.sourceAssetRef;
    }
    if(outMergedSchema.metadata.createdUtc.empty()){
        outMergedSchema.metadata.createdUtc = baseSchema.metadata.createdUtc;
    }
    if(outMergedSchema.metadata.modifiedUtc.empty()){
        outMergedSchema.metadata.modifiedUtc = baseSchema.metadata.modifiedUtc;
    }
    if(outMergedSchema.metadata.tags.empty()){
        outMergedSchema.metadata.tags = baseSchema.metadata.tags;
    }

    if((!outMergedSchema.prefabSettings.hasValue || outMergedSchema.prefabSettings.json.empty()) &&
       baseSchema.prefabSettings.hasValue && !baseSchema.prefabSettings.json.empty()){
        outMergedSchema.prefabSettings = baseSchema.prefabSettings;
    }
}

bool applyPayloadMergePatch(
    const JsonSchema::PrefabSchema& sourceSchema,
    const JsonSchema::RawJsonValue& patch,
    JsonSchema::PrefabSchema& outPatchedSchema,
    std::string* outError
){
    outPatchedSchema = sourceSchema;
    if(!patch.hasValue || patch.json.empty()){
        return true;
    }

    JsonUtils::Document patchDoc;
    if(!JsonUtils::LoadDocumentFromText(patch.json, patchDoc, outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to parse variant override patch JSON: " + *outError;
        }
        return false;
    }
    yyjson_val* patchRoot = patchDoc.root();
    if(!patchRoot || !yyjson_is_obj(patchRoot)){
        setPrefabError(outError, "Variant override patch must be a JSON object.");
        return false;
    }

    std::string sourceJson;
    if(!sourceSchema.WriteToString(sourceJson, outError, false)){
        if(outError && !outError->empty()){
            *outError = "Failed to serialize schema before applying variant override patch: " + *outError;
        }
        return false;
    }

    JsonUtils::Document sourceDoc;
    if(!JsonUtils::LoadDocumentFromText(sourceJson, sourceDoc, outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to parse serialized schema before applying variant override patch: " + *outError;
        }
        return false;
    }

    std::string sourceType;
    int sourceVersion = 0;
    yyjson_val* sourcePayload = nullptr;
    if(!JsonUtils::ReadStandardDocumentHeader(sourceDoc, sourceType, sourceVersion, &sourcePayload, outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to read schema header before applying variant override patch: " + *outError;
        }
        return false;
    }
    if(!sourcePayload || !yyjson_is_obj(sourcePayload)){
        setPrefabError(outError, "Schema payload must be a JSON object before applying variant override patch.");
        return false;
    }

    JsonUtils::MutableDocument patchedDoc;
    JsonUtils::StandardDocumentRefs refs;
    if(!JsonUtils::CreateStandardDocument(patchedDoc, sourceType, sourceVersion, refs, outError)){
        return false;
    }

    yyjson_mut_val* patchedPayload = yyjson_merge_patch(patchedDoc.get(), sourcePayload, patchRoot);
    if(!patchedPayload){
        setPrefabError(outError, "Failed to apply variant override patch to schema payload.");
        return false;
    }
    if(!yyjson_mut_is_obj(patchedPayload)){
        setPrefabError(outError, "Variant override patch produced a non-object payload.");
        return false;
    }

    (void)yyjson_mut_obj_remove_key(refs.root, "payload");
    if(!yyjson_mut_obj_add_val(patchedDoc.get(), refs.root, "payload", patchedPayload)){
        setPrefabError(outError, "Failed to write patched payload into schema document.");
        return false;
    }

    std::string patchedJson;
    if(!JsonUtils::WriteDocumentToString(patchedDoc, patchedJson, outError, false)){
        if(outError && !outError->empty()){
            *outError = "Failed to encode schema after applying variant override patch: " + *outError;
        }
        return false;
    }

    outPatchedSchema.Clear();
    if(!outPatchedSchema.LoadFromText(patchedJson, outError)){
        if(outError && !outError->empty()){
            *outError = "Variant override patch produced invalid schema state: " + *outError;
        }
        return false;
    }

    return true;
}

bool resolveSchemaForInstantiationRecursive(
    const JsonSchema::PrefabSchema& sourceSchema,
    const PrefabIO::PrefabInstantiateOptions& options,
    std::unordered_set<std::string>& activeBaseRefs,
    std::size_t depth,
    JsonSchema::PrefabSchema& outResolvedSchema,
    std::vector<std::string>* outResolvedBaseChain,
    std::string* outError
){
    outResolvedSchema = sourceSchema;
    if(outResolvedBaseChain){
        outResolvedBaseChain->clear();
    }

    if(!options.resolveVariants){
        return true;
    }

    ParsedVariantDefinition variant;
    if(!parseVariantDefinition(sourceSchema, variant, outError)){
        return false;
    }

    if(!variant.basePrefabRef.empty()){
        if(depth >= options.maxVariantDepth){
            setPrefabError(
                outError,
                "Prefab variant chain exceeded max depth (" + std::to_string(options.maxVariantDepth) +
                ") while resolving base prefab '" + variant.basePrefabRef + "'.");
            return false;
        }
        if(activeBaseRefs.find(variant.basePrefabRef) != activeBaseRefs.end()){
            setPrefabError(outError, "Detected cyclic prefab variant reference at '" + variant.basePrefabRef + "'.");
            return false;
        }

        JsonSchema::PrefabSchema baseSchema;
        if(!PrefabIO::LoadSchemaFromAssetRef(variant.basePrefabRef, baseSchema, outError)){
            if(outError && !outError->empty()){
                *outError = "Failed to load base prefab '" + variant.basePrefabRef + "': " + *outError;
            }
            return false;
        }

        activeBaseRefs.insert(variant.basePrefabRef);

        JsonSchema::PrefabSchema resolvedBaseSchema;
        std::vector<std::string> resolvedBaseChain;
        if(!resolveSchemaForInstantiationRecursive(
                baseSchema,
                options,
                activeBaseRefs,
                depth + 1,
                resolvedBaseSchema,
                &resolvedBaseChain,
                outError)){
            activeBaseRefs.erase(variant.basePrefabRef);
            return false;
        }

        activeBaseRefs.erase(variant.basePrefabRef);

        JsonSchema::PrefabSchema mergedSchema;
        mergePrefabSchemas(resolvedBaseSchema, sourceSchema, variant, mergedSchema);
        outResolvedSchema = std::move(mergedSchema);

        resolvedBaseChain.push_back(variant.basePrefabRef);
        if(outResolvedBaseChain){
            *outResolvedBaseChain = std::move(resolvedBaseChain);
        }
    }

    if(variant.overrides.hasValue){
        JsonSchema::PrefabSchema patchedSchema;
        if(!applyPayloadMergePatch(outResolvedSchema, variant.overrides, patchedSchema, outError)){
            return false;
        }
        outResolvedSchema = std::move(patchedSchema);
    }

    return true;
}

bool buildPrefabSettingsRawJson(const PrefabIO::PrefabSettingsOptions& settings, JsonSchema::RawJsonValue& outSettings, std::string* outError){
    JsonUtils::MutableDocument doc;
    yyjson_mut_val* root = doc.setRootObject();
    if(!root){
        setPrefabError(outError, "Failed to allocate prefabSettings JSON object.");
        return false;
    }

    if(!JsonUtils::MutObjAddBool(doc.get(), root, "runtimeInstantiable", settings.runtimeInstantiable) ||
       !JsonUtils::MutObjAddBool(doc.get(), root, "allowChildAdditions", settings.allowChildAdditions) ||
       !JsonUtils::MutObjAddBool(doc.get(), root, "allowChildRemovals", settings.allowChildRemovals) ||
       !JsonUtils::MutObjAddBool(doc.get(), root, "allowComponentOverrides", settings.allowComponentOverrides)){
        setPrefabError(outError, "Failed to write prefabSettings fields.");
        return false;
    }

    yyjson_mut_val* exposedArr = yyjson_mut_obj_add_arr(doc.get(), root, "exposedProperties");
    if(!exposedArr){
        setPrefabError(outError, "Failed to create prefabSettings.exposedProperties array.");
        return false;
    }
    for(const std::string& propertyPath : settings.exposedProperties){
        if(!yyjson_mut_arr_add_strcpy(doc.get(), exposedArr, propertyPath.c_str())){
            setPrefabError(outError, "Failed to append exposed property path.");
            return false;
        }
    }

    std::string json;
    if(!JsonUtils::WriteDocumentToString(doc, json, outError, false)){
        if(outError && !outError->empty()){
            *outError = "Failed to build prefabSettings JSON: " + *outError;
        }
        return false;
    }

    outSettings.hasValue = true;
    outSettings.json = std::move(json);
    return true;
}

bool buildVariantRawJson(const PrefabIO::PrefabVariantOptions& variant, JsonSchema::RawJsonValue& outVariant, std::string* outError){
    const bool hasVariantData =
        !variant.basePrefabRef.empty() ||
        variant.overrides.hasValue;

    if(!hasVariantData){
        outVariant.Clear();
        return true;
    }

    JsonUtils::MutableDocument doc;
    yyjson_mut_val* root = doc.setRootObject();
    if(!root){
        setPrefabError(outError, "Failed to allocate variant JSON object.");
        return false;
    }

    if(!JsonUtils::MutObjAddString(doc.get(), root, "basePrefabRef", variant.basePrefabRef) ||
       !JsonUtils::MutObjAddBool(doc.get(), root, "inheritBaseChildren", variant.inheritBaseChildren) ||
       !JsonUtils::MutObjAddBool(doc.get(), root, "inheritBaseComponents", variant.inheritBaseComponents)){
        setPrefabError(outError, "Failed to write variant metadata fields.");
        return false;
    }

    JsonUtils::Document overrideDoc;
    yyjson_val* overrideRoot = nullptr;
    if(!parseOptionalObjectJson(variant.overrides, overrideDoc, overrideRoot, outError)){
        return false;
    }
    if(overrideRoot){
        yyjson_mut_val* copied = yyjson_val_mut_copy(doc.get(), overrideRoot);
        if(!copied){
            setPrefabError(outError, "Failed to copy variant overrides payload.");
            return false;
        }
        if(!yyjson_mut_obj_add_val(doc.get(), root, "overrides", copied)){
            setPrefabError(outError, "Failed to attach variant overrides payload.");
            return false;
        }
    }

    std::string json;
    if(!JsonUtils::WriteDocumentToString(doc, json, outError, false)){
        if(outError && !outError->empty()){
            *outError = "Failed to build variant JSON: " + *outError;
        }
        return false;
    }

    outVariant.hasValue = true;
    outVariant.json = std::move(json);
    return true;
}

std::string readVariantBasePrefabRef(const JsonSchema::PrefabSchema& schema){
    if(!schema.variant.hasValue || schema.variant.json.empty()){
        return std::string();
    }

    JsonUtils::Document doc;
    std::string error;
    if(!JsonUtils::LoadDocumentFromText(schema.variant.json, doc, &error)){
        return std::string();
    }
    yyjson_val* root = doc.root();
    if(!root || !yyjson_is_obj(root)){
        return std::string();
    }
    std::string baseRef;
    JsonUtils::TryGetString(root, "basePrefabRef", baseRef);
    return baseRef;
}

} // namespace

namespace PrefabIO {

bool BuildSchemaFromEntitySubtree(
    PScene scene,
    NeoECS::ECSEntity* rootEntity,
    JsonSchema::PrefabSchema& outSchema,
    const PrefabSaveOptions& options,
    std::string* outError
){
    if(!scene || !scene->getECS()){
        setPrefabError(outError, "Cannot build prefab schema: scene/ECS is null.");
        return false;
    }
    if(!rootEntity){
        setPrefabError(outError, "Cannot build prefab schema from a null root entity.");
        return false;
    }

    Serialization::SnapshotIO::SnapshotBuildResult snapshot;
    Serialization::SnapshotIO::SnapshotBuildOptions snapshotOptions;
    snapshotOptions.registry = options.registry;
    if(!Serialization::SnapshotIO::BuildSnapshotFromEntityRoots(
            scene,
            std::vector<NeoECS::ECSEntity*>{rootEntity},
            snapshot,
            snapshotOptions,
            outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to build prefab snapshot: " + *outError;
        }
        return false;
    }

    outSchema.Clear();
    outSchema.rootEntityIds = snapshot.rootEntityIds;
    outSchema.entities = std::move(snapshot.entities);
    outSchema.metadata = options.metadata;
    if(outSchema.metadata.name.empty()){
        outSchema.metadata.name = rootEntity->getName();
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

    if(!buildPrefabSettingsRawJson(options.settings, outSchema.prefabSettings, outError)){
        return false;
    }
    if(!buildVariantRawJson(options.variant, outSchema.variant, outError)){
        return false;
    }

    return true;
}

bool SaveEntitySubtreeToAbsolutePath(
    PScene scene,
    NeoECS::ECSEntity* rootEntity,
    const std::filesystem::path& path,
    const PrefabSaveOptions& options,
    std::string* outError
){
    JsonSchema::PrefabSchema schema;
    if(!BuildSchemaFromEntitySubtree(scene, rootEntity, schema, options, outError)){
        return false;
    }
    return schema.SaveToAbsolutePath(path, outError, true);
}

bool SaveEntitySubtreeToAssetRef(
    PScene scene,
    NeoECS::ECSEntity* rootEntity,
    const std::string& assetRef,
    const PrefabSaveOptions& options,
    std::string* outError
){
    JsonSchema::PrefabSchema schema;
    if(!BuildSchemaFromEntitySubtree(scene, rootEntity, schema, options, outError)){
        return false;
    }
    return schema.SaveToAssetRef(assetRef, outError, true);
}

bool LoadSchemaFromAbsolutePath(
    const std::filesystem::path& path,
    JsonSchema::PrefabSchema& outSchema,
    std::string* outError
){
    outSchema.Clear();
    return outSchema.LoadFromAbsolutePath(path, outError);
}

bool LoadSchemaFromAssetRef(
    const std::string& assetRef,
    JsonSchema::PrefabSchema& outSchema,
    std::string* outError
){
    outSchema.Clear();
    return outSchema.LoadFromAssetRef(assetRef, outError);
}

bool InstantiateSchemaIntoScene(
    PScene scene,
    const JsonSchema::PrefabSchema& schema,
    const PrefabInstantiateOptions& options,
    PrefabInstantiateResult* outResult,
    std::string* outError
){
    JsonSchema::PrefabSchema resolvedSchema;
    std::vector<std::string> resolvedBaseChain;
    std::unordered_set<std::string> activeVariantRefs;
    if(!resolveSchemaForInstantiationRecursive(
            schema,
            options,
            activeVariantRefs,
            0,
            resolvedSchema,
            &resolvedBaseChain,
            outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to resolve prefab variant chain: " + *outError;
        }
        return false;
    }

    Serialization::SnapshotIO::SnapshotInstantiateResult instantiateResult;
    Serialization::SnapshotIO::SnapshotInstantiateOptions instantiateOptions;
    instantiateOptions.destinationParent = options.parent;
    instantiateOptions.registry = options.registry;

    if(!Serialization::SnapshotIO::InstantiateSnapshotIntoScene(
            scene,
            resolvedSchema.entities,
            resolvedSchema.rootEntityIds,
            instantiateResult,
            instantiateOptions,
            outError)){
        if(outError && !outError->empty()){
            *outError = "Failed to instantiate prefab snapshot: " + *outError;
        }
        return false;
    }

    if(outResult){
        outResult->rootObjects = std::move(instantiateResult.rootObjects);
        outResult->snapshotIdToEntity = std::move(instantiateResult.snapshotIdToEntity);
        outResult->variantBasePrefabRef = readVariantBasePrefabRef(schema);
        outResult->resolvedVariantBaseRefs = std::move(resolvedBaseChain);
    }
    return true;
}

bool InstantiateFromAbsolutePath(
    PScene scene,
    const std::filesystem::path& path,
    const PrefabInstantiateOptions& options,
    PrefabInstantiateResult* outResult,
    std::string* outError
){
    JsonSchema::PrefabSchema schema;
    if(!LoadSchemaFromAbsolutePath(path, schema, outError)){
        return false;
    }
    return InstantiateSchemaIntoScene(scene, schema, options, outResult, outError);
}

bool InstantiateFromAssetRef(
    PScene scene,
    const std::string& assetRef,
    const PrefabInstantiateOptions& options,
    PrefabInstantiateResult* outResult,
    std::string* outError
){
    JsonSchema::PrefabSchema schema;
    if(!LoadSchemaFromAssetRef(assetRef, schema, outError)){
        return false;
    }
    return InstantiateSchemaIntoScene(scene, schema, options, outResult, outError);
}

} // namespace PrefabIO
