/**
 * @file src/Serialization/Schema/PrefabSceneSchemas.cpp
 * @brief Implementation for PrefabSceneSchemas.
 */

#include "Serialization/Schema/PrefabSceneSchemas.h"

#include <cstdlib>
#include <set>
#include <unordered_map>

namespace {

void SetDocSchemaError(std::string* outError, const std::string& message){
    if(outError){
        *outError = message;
    }
}

void PrefixDocSchemaError(std::string* outError, const std::string& prefix){
    if(outError && !outError->empty()){
        *outError = prefix + *outError;
    }
}

std::string FieldName(const char* key){
    return key ? std::string(key) : std::string("<null>");
}

bool ReadStringArrayLocal(JsonUtils::JsonVal* arr, std::vector<std::string>& outValues, std::string* outError, const char* debugName){
    if(!arr || !yyjson_is_arr(arr)){
        SetDocSchemaError(outError, std::string("Expected string array for '") + (debugName ? debugName : "array") + "'.");
        return false;
    }

    outValues.clear();
    const size_t count = yyjson_arr_size(arr);
    outValues.reserve(count);
    for(size_t i = 0; i < count; ++i){
        JsonUtils::JsonVal* item = yyjson_arr_get(arr, i);
        if(!item || !yyjson_is_str(item)){
            SetDocSchemaError(
                outError,
                std::string("Expected string at '") + (debugName ? debugName : "array") +
                "[" + std::to_string(i) + "]'."
            );
            return false;
        }
        const char* s = yyjson_get_str(item);
        outValues.push_back(s ? s : "");
    }
    return true;
}

bool WriteStringArrayLocal(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* obj, const char* key, const std::vector<std::string>& values, std::string* outError){
    if(values.empty()){
        return true;
    }

    JsonUtils::JsonMutVal* arr = yyjson_mut_obj_add_arr(doc, obj, key);
    if(!arr){
        SetDocSchemaError(outError, "Failed to create array field '" + FieldName(key) + "'.");
        return false;
    }

    for(size_t i = 0; i < values.size(); ++i){
        if(!yyjson_mut_arr_add_strcpy(doc, arr, values[i].c_str())){
            SetDocSchemaError(outError, "Failed to write array element for field '" + FieldName(key) + "'.");
            return false;
        }
    }
    return true;
}

bool ReadMetadataBlock(
    JsonUtils::JsonVal* payload,
    JsonSchema::DocumentMetadata& metadata,
    std::string* outError
){
    if(!JsonUtils::ObjHasKey(payload, "metadata")){
        return true;
    }

    JsonUtils::JsonVal* metaObj = JsonUtils::ObjGetObject(payload, "metadata");
    if(!metaObj){
        SetDocSchemaError(outError, "Field 'metadata' must be an object.");
        return false;
    }

    if(JsonUtils::ObjHasKey(metaObj, "name") && !JsonUtils::TryGetString(metaObj, "name", metadata.name)){
        SetDocSchemaError(outError, "Field 'metadata.name' must be a string.");
        PrefixDocSchemaError(outError, "metadata: ");
        return false;
    }
    if(JsonUtils::ObjHasKey(metaObj, "description") && !JsonUtils::TryGetString(metaObj, "description", metadata.description)){
        SetDocSchemaError(outError, "Field 'metadata.description' must be a string.");
        PrefixDocSchemaError(outError, "metadata: ");
        return false;
    }
    if(JsonUtils::ObjHasKey(metaObj, "sourceAssetRef") && !JsonUtils::TryGetString(metaObj, "sourceAssetRef", metadata.sourceAssetRef)){
        SetDocSchemaError(outError, "Field 'metadata.sourceAssetRef' must be a string.");
        PrefixDocSchemaError(outError, "metadata: ");
        return false;
    }
    if(JsonUtils::ObjHasKey(metaObj, "createdUtc") && !JsonUtils::TryGetString(metaObj, "createdUtc", metadata.createdUtc)){
        SetDocSchemaError(outError, "Field 'metadata.createdUtc' must be a string.");
        PrefixDocSchemaError(outError, "metadata: ");
        return false;
    }
    if(JsonUtils::ObjHasKey(metaObj, "modifiedUtc") && !JsonUtils::TryGetString(metaObj, "modifiedUtc", metadata.modifiedUtc)){
        SetDocSchemaError(outError, "Field 'metadata.modifiedUtc' must be a string.");
        PrefixDocSchemaError(outError, "metadata: ");
        return false;
    }
    if(JsonUtils::ObjHasKey(metaObj, "tags")){
        JsonUtils::JsonVal* tagsArr = JsonUtils::ObjGetArray(metaObj, "tags");
        if(!ReadStringArrayLocal(tagsArr, metadata.tags, outError, "metadata.tags")){
            PrefixDocSchemaError(outError, "metadata: ");
            return false;
        }
    }else{
        metadata.tags.clear();
    }

    return true;
}

bool WriteMetadataBlock(
    yyjson_mut_doc* doc,
    JsonUtils::JsonMutVal* payload,
    const JsonSchema::DocumentMetadata& metadata,
    std::string* outError
){
    JsonUtils::JsonMutVal* metaObj = yyjson_mut_obj_add_obj(doc, payload, "metadata");
    if(!metaObj){
        SetDocSchemaError(outError, "Failed to create 'metadata' object.");
        return false;
    }

    if(!metadata.name.empty() && !JsonUtils::MutObjAddString(doc, metaObj, "name", metadata.name)){
        SetDocSchemaError(outError, "Failed to write 'metadata.name'.");
        return false;
    }
    if(!metadata.description.empty() && !JsonUtils::MutObjAddString(doc, metaObj, "description", metadata.description)){
        SetDocSchemaError(outError, "Failed to write 'metadata.description'.");
        return false;
    }
    if(!metadata.sourceAssetRef.empty() && !JsonUtils::MutObjAddString(doc, metaObj, "sourceAssetRef", metadata.sourceAssetRef)){
        SetDocSchemaError(outError, "Failed to write 'metadata.sourceAssetRef'.");
        return false;
    }
    if(!metadata.createdUtc.empty() && !JsonUtils::MutObjAddString(doc, metaObj, "createdUtc", metadata.createdUtc)){
        SetDocSchemaError(outError, "Failed to write 'metadata.createdUtc'.");
        return false;
    }
    if(!metadata.modifiedUtc.empty() && !JsonUtils::MutObjAddString(doc, metaObj, "modifiedUtc", metadata.modifiedUtc)){
        SetDocSchemaError(outError, "Failed to write 'metadata.modifiedUtc'.");
        return false;
    }
    if(!WriteStringArrayLocal(doc, metaObj, "tags", metadata.tags, outError)){
        PrefixDocSchemaError(outError, "metadata: ");
        return false;
    }

    return true;
}

} // namespace

namespace JsonSchema {

void EntitySnapshotSchemaBase::ClearSnapshot(){
    rootEntityIds.clear();
    entities.clear();
}

bool EntitySnapshotSchemaBase::DeserializePayload(JsonUtils::JsonVal* payload, int version, std::string* outError){
    ClearSnapshot();
    ResetDocumentFieldsState();

    if(!DeserializeDocumentFields(payload, version, outError)){
        return false;
    }
    if(!DeserializeSnapshotFields(payload, outError)){
        return false;
    }

    DeriveRootEntityIdsIfMissing();

    if(!ValidateSnapshotState(outError)){
        return false;
    }
    return ValidateDocumentState(outError);
}

bool EntitySnapshotSchemaBase::SerializePayload(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, int version, std::string* outError) const{
    (void)version;

    if(!ValidateSnapshotState(outError)){
        return false;
    }
    if(!ValidateDocumentState(outError)){
        return false;
    }
    if(!SerializeDocumentFields(doc, payload, version, outError)){
        return false;
    }
    return SerializeSnapshotFields(doc, payload, outError);
}

bool EntitySnapshotSchemaBase::ValidateDocumentState(std::string*) const{
    return true;
}

bool EntitySnapshotSchemaBase::RequireStringField(JsonUtils::JsonVal* obj, const char* key, std::string& outValue, std::string* outError) const{
    if(JsonUtils::TryGetString(obj, key, outValue)){
        return true;
    }
    SetDocSchemaError(outError, "Missing required string field '" + FieldName(key) + "'.");
    return false;
}

bool EntitySnapshotSchemaBase::RequireIntField(JsonUtils::JsonVal* obj, const char* key, int& outValue, std::string* outError) const{
    if(JsonUtils::TryGetInt(obj, key, outValue)){
        return true;
    }
    SetDocSchemaError(outError, "Missing required integer field '" + FieldName(key) + "'.");
    return false;
}

bool EntitySnapshotSchemaBase::RequireUInt64Field(JsonUtils::JsonVal* obj, const char* key, std::uint64_t& outValue, std::string* outError) const{
    if(JsonUtils::TryGetUInt64(obj, key, outValue)){
        return true;
    }
    SetDocSchemaError(outError, "Missing required unsigned integer field '" + FieldName(key) + "'.");
    return false;
}

bool EntitySnapshotSchemaBase::RequireObjectField(JsonUtils::JsonVal* obj, const char* key, JsonUtils::JsonVal*& outValue, std::string* outError) const{
    outValue = JsonUtils::ObjGetObject(obj, key);
    if(outValue){
        return true;
    }
    SetDocSchemaError(outError, "Missing required object field '" + FieldName(key) + "'.");
    return false;
}

bool EntitySnapshotSchemaBase::RequireArrayField(JsonUtils::JsonVal* obj, const char* key, JsonUtils::JsonVal*& outValue, std::string* outError) const{
    outValue = JsonUtils::ObjGetArray(obj, key);
    if(outValue){
        return true;
    }
    SetDocSchemaError(outError, "Missing required array field '" + FieldName(key) + "'.");
    return false;
}

bool EntitySnapshotSchemaBase::ReadOptionalStringField(JsonUtils::JsonVal* obj, const char* key, std::string& outValue, std::string* outError) const{
    if(!JsonUtils::ObjHasKey(obj, key)){
        return true;
    }
    if(JsonUtils::TryGetString(obj, key, outValue)){
        return true;
    }
    SetDocSchemaError(outError, "Field '" + FieldName(key) + "' must be a string.");
    return false;
}

bool EntitySnapshotSchemaBase::ReadOptionalBoolField(JsonUtils::JsonVal* obj, const char* key, bool& outValue, std::string* outError) const{
    if(!JsonUtils::ObjHasKey(obj, key)){
        return true;
    }
    if(JsonUtils::TryGetBool(obj, key, outValue)){
        return true;
    }
    SetDocSchemaError(outError, "Field '" + FieldName(key) + "' must be a bool.");
    return false;
}

bool EntitySnapshotSchemaBase::ReadOptionalIntField(JsonUtils::JsonVal* obj, const char* key, int& outValue, std::string* outError) const{
    if(!JsonUtils::ObjHasKey(obj, key)){
        return true;
    }
    if(JsonUtils::TryGetInt(obj, key, outValue)){
        return true;
    }
    SetDocSchemaError(outError, "Field '" + FieldName(key) + "' must be an integer.");
    return false;
}

bool EntitySnapshotSchemaBase::ReadOptionalUInt64Field(JsonUtils::JsonVal* obj, const char* key, std::uint64_t& outValue, std::string* outError) const{
    if(!JsonUtils::ObjHasKey(obj, key)){
        return true;
    }
    if(JsonUtils::TryGetUInt64(obj, key, outValue)){
        return true;
    }
    SetDocSchemaError(outError, "Field '" + FieldName(key) + "' must be an unsigned integer.");
    return false;
}

bool EntitySnapshotSchemaBase::ReadStringArray(JsonUtils::JsonVal* arr, std::vector<std::string>& outValues, std::string* outError, const char* debugName) const{
    if(!arr || !yyjson_is_arr(arr)){
        SetDocSchemaError(outError, std::string("Expected string array for '") + (debugName ? debugName : "array") + "'.");
        return false;
    }

    outValues.clear();
    const size_t count = yyjson_arr_size(arr);
    outValues.reserve(count);
    for(size_t i = 0; i < count; ++i){
        JsonUtils::JsonVal* item = yyjson_arr_get(arr, i);
        if(!item || !yyjson_is_str(item)){
            SetDocSchemaError(
                outError,
                std::string("Expected string at '") + (debugName ? debugName : "array") +
                "[" + std::to_string(i) + "]'."
            );
            return false;
        }
        const char* s = yyjson_get_str(item);
        outValues.push_back(s ? s : "");
    }
    return true;
}

bool EntitySnapshotSchemaBase::ReadUInt64Array(JsonUtils::JsonVal* arr, std::vector<std::uint64_t>& outValues, std::string* outError, const char* debugName) const{
    if(!arr || !yyjson_is_arr(arr)){
        SetDocSchemaError(outError, std::string("Expected uint64 array for '") + (debugName ? debugName : "array") + "'.");
        return false;
    }

    outValues.clear();
    const size_t count = yyjson_arr_size(arr);
    outValues.reserve(count);
    for(size_t i = 0; i < count; ++i){
        JsonUtils::JsonVal* item = yyjson_arr_get(arr, i);
        std::uint64_t value = 0;
        if(!item || !(yyjson_is_uint(item) || yyjson_is_int(item) || yyjson_is_sint(item))){
            SetDocSchemaError(
                outError,
                std::string("Expected unsigned integer at '") + (debugName ? debugName : "array") +
                "[" + std::to_string(i) + "]'."
            );
            return false;
        }
        if(yyjson_is_uint(item)){
            value = yyjson_get_uint(item);
        }else{
            int64_t raw = yyjson_get_sint(item);
            if(raw < 0){
                SetDocSchemaError(
                    outError,
                    std::string("Expected non-negative integer at '") + (debugName ? debugName : "array") +
                    "[" + std::to_string(i) + "]'."
                );
                return false;
            }
            value = static_cast<std::uint64_t>(raw);
        }
        outValues.push_back(value);
    }
    return true;
}

bool EntitySnapshotSchemaBase::ReadStringArrayField(JsonUtils::JsonVal* obj, const char* key, std::vector<std::string>& outValues, std::string* outError, bool required) const{
    JsonUtils::JsonVal* arr = JsonUtils::ObjGetArray(obj, key);
    if(!arr){
        if(required){
            SetDocSchemaError(outError, "Missing required string array field '" + FieldName(key) + "'.");
            return false;
        }
        outValues.clear();
        return true;
    }
    return ReadStringArray(arr, outValues, outError, key);
}

bool EntitySnapshotSchemaBase::WriteStringArrayField(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* obj, const char* key, const std::vector<std::string>& values, std::string* outError) const{
    if(values.empty()){
        return true;
    }

    JsonUtils::JsonMutVal* arr = yyjson_mut_obj_add_arr(doc, obj, key);
    if(!arr){
        SetDocSchemaError(outError, "Failed to create array field '" + FieldName(key) + "'.");
        return false;
    }

    for(size_t i = 0; i < values.size(); ++i){
        if(!yyjson_mut_arr_add_strcpy(doc, arr, values[i].c_str())){
            SetDocSchemaError(outError, "Failed to write array element for field '" + FieldName(key) + "'.");
            return false;
        }
    }
    return true;
}

bool EntitySnapshotSchemaBase::ReadOptionalRawJsonField(JsonUtils::JsonVal* obj, const char* key, RawJsonValue& outValue, std::string* outError, bool requireObject) const{
    if(!JsonUtils::ObjHasKey(obj, key)){
        outValue.Clear();
        return true;
    }

    JsonUtils::JsonVal* value = JsonUtils::ObjGet(obj, key);
    if(!value){
        outValue.Clear();
        return true;
    }
    if(requireObject && !yyjson_is_obj(value)){
        SetDocSchemaError(outError, "Field '" + FieldName(key) + "' must be an object.");
        return false;
    }

    std::string json;
    if(!CaptureJsonValue(value, json, outError, key)){
        return false;
    }

    outValue.hasValue = true;
    outValue.json = json;
    return true;
}

bool EntitySnapshotSchemaBase::WriteOptionalRawJsonField(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* obj, const char* key, const RawJsonValue& value, std::string* outError, bool requireObject) const{
    if(!value.hasValue){
        return true;
    }

    JsonUtils::JsonMutVal* copied = nullptr;
    if(!CopyRawJsonToMutableValue(doc, value.json, copied, outError, key, requireObject)){
        return false;
    }

    if(!yyjson_mut_obj_add_val(doc, obj, key, copied)){
        SetDocSchemaError(outError, "Failed to write raw JSON field '" + FieldName(key) + "'.");
        return false;
    }
    return true;
}

bool EntitySnapshotSchemaBase::DeserializeSnapshotFields(JsonUtils::JsonVal* payload, std::string* outError){
    JsonUtils::JsonVal* snapshotObj = nullptr;
    if(!RequireObjectField(payload, "snapshot", snapshotObj, outError)){
        return false;
    }

    if(JsonUtils::ObjHasKey(snapshotObj, "rootEntityIds")){
        JsonUtils::JsonVal* rootIdsArr = JsonUtils::ObjGetArray(snapshotObj, "rootEntityIds");
        if(!rootIdsArr){
            SetDocSchemaError(outError, "Field 'snapshot.rootEntityIds' must be an array.");
            return false;
        }
        if(!ReadUInt64Array(rootIdsArr, rootEntityIds, outError, "snapshot.rootEntityIds")){
            return false;
        }
    }

    JsonUtils::JsonVal* entitiesArr = nullptr;
    if(!RequireArrayField(snapshotObj, "entities", entitiesArr, outError)){
        PrefixDocSchemaError(outError, "snapshot: ");
        return false;
    }

    entities.clear();
    const size_t count = yyjson_arr_size(entitiesArr);
    entities.reserve(count);
    for(size_t i = 0; i < count; ++i){
        JsonUtils::JsonVal* item = yyjson_arr_get(entitiesArr, i);
        if(!item || !yyjson_is_obj(item)){
            SetDocSchemaError(outError, "Field 'snapshot.entities' must contain only objects.");
            PrefixDocSchemaError(outError, "snapshot.entities[" + std::to_string(i) + "]: ");
            return false;
        }

        EntityRecord entity;
        if(!ReadEntityRecord(item, entity, outError)){
            PrefixDocSchemaError(outError, "snapshot.entities[" + std::to_string(i) + "]: ");
            return false;
        }
        entities.push_back(entity);
    }

    return true;
}

bool EntitySnapshotSchemaBase::SerializeSnapshotFields(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* outError) const{
    JsonUtils::JsonMutVal* snapshotObj = yyjson_mut_obj_add_obj(doc, payload, "snapshot");
    if(!snapshotObj){
        SetDocSchemaError(outError, "Failed to create 'snapshot' object.");
        return false;
    }

    std::vector<std::uint64_t> roots = rootEntityIds;
    if(roots.empty()){
        CollectDerivedRootEntityIds(roots);
    }
    if(!WriteUInt64ArrayField(doc, snapshotObj, "rootEntityIds", roots, outError)){
        PrefixDocSchemaError(outError, "snapshot: ");
        return false;
    }

    JsonUtils::JsonMutVal* entitiesArr = yyjson_mut_obj_add_arr(doc, snapshotObj, "entities");
    if(!entitiesArr){
        SetDocSchemaError(outError, "Failed to create 'snapshot.entities' array.");
        return false;
    }

    for(size_t i = 0; i < entities.size(); ++i){
        if(!WriteEntityRecord(doc, entitiesArr, entities[i], outError)){
            PrefixDocSchemaError(outError, "snapshot.entities[" + std::to_string(i) + "]: ");
            return false;
        }
    }
    return true;
}

void EntitySnapshotSchemaBase::DeriveRootEntityIdsIfMissing(){
    if(!rootEntityIds.empty()){
        return;
    }
    CollectDerivedRootEntityIds(rootEntityIds);
}

void EntitySnapshotSchemaBase::CollectDerivedRootEntityIds(std::vector<std::uint64_t>& outRoots) const{
    outRoots.clear();
    outRoots.reserve(entities.size());
    for(size_t i = 0; i < entities.size(); ++i){
        if(!entities[i].hasParentId){
            outRoots.push_back(entities[i].id);
        }
    }
}

bool EntitySnapshotSchemaBase::ValidateSnapshotState(std::string* outError) const{
    std::unordered_map<std::uint64_t, size_t> idToIndex;
    idToIndex.reserve(entities.size());

    for(size_t i = 0; i < entities.size(); ++i){
        const EntityRecord& entity = entities[i];
        const std::string label = "entities[" + std::to_string(i) + "]";

        if(entity.id == 0){
            SetDocSchemaError(outError, label + ".id must be > 0.");
            return false;
        }
        if(idToIndex.find(entity.id) != idToIndex.end()){
            SetDocSchemaError(outError, "Duplicate entity id in snapshot: " + std::to_string(entity.id) + ".");
            return false;
        }
        idToIndex[entity.id] = i;

        for(size_t c = 0; c < entity.components.size(); ++c){
            const ComponentRecord& component = entity.components[c];
            const std::string compLabel = label + ".components[" + std::to_string(c) + "]";
            if(component.type.empty()){
                SetDocSchemaError(outError, compLabel + ".type must not be empty.");
                return false;
            }
            if(component.version <= 0){
                SetDocSchemaError(outError, compLabel + ".version must be > 0.");
                return false;
            }
            if(component.payloadJson.empty()){
                SetDocSchemaError(outError, compLabel + ".payloadJson must not be empty.");
                return false;
            }
        }
    }

    std::set<std::uint64_t> parentlessIds;
    for(size_t i = 0; i < entities.size(); ++i){
        const EntityRecord& entity = entities[i];
        if(entity.hasParentId){
            if(entity.parentId == 0){
                SetDocSchemaError(outError, "Entity " + std::to_string(entity.id) + " has parentId=0 but hasParentId=true.");
                return false;
            }
            if(entity.parentId == entity.id){
                SetDocSchemaError(outError, "Entity " + std::to_string(entity.id) + " cannot parent itself.");
                return false;
            }
            if(idToIndex.find(entity.parentId) == idToIndex.end()){
                SetDocSchemaError(
                    outError,
                    "Entity " + std::to_string(entity.id) + " references missing parentId " +
                    std::to_string(entity.parentId) + "."
                );
                return false;
            }
        }else{
            parentlessIds.insert(entity.id);
        }
    }

    if(rootEntityIds.empty()){
        return true;
    }

    if(rootEntityIds.size() > entities.size()){
        SetDocSchemaError(outError, "snapshot.rootEntityIds contains more ids than snapshot.entities.");
        return false;
    }

    std::set<std::uint64_t> seenRoots;
    for(size_t i = 0; i < rootEntityIds.size(); ++i){
        const std::uint64_t rootId = rootEntityIds[i];
        if(!seenRoots.insert(rootId).second){
            SetDocSchemaError(outError, "Duplicate root entity id: " + std::to_string(rootId) + ".");
            return false;
        }
        if(idToIndex.find(rootId) == idToIndex.end()){
            SetDocSchemaError(outError, "Root entity id not found in snapshot.entities: " + std::to_string(rootId) + ".");
            return false;
        }
        if(parentlessIds.find(rootId) == parentlessIds.end()){
            SetDocSchemaError(outError, "Root entity id " + std::to_string(rootId) + " is not a root (it has a parent).");
            return false;
        }
    }

    if(seenRoots.size() != parentlessIds.size()){
        SetDocSchemaError(outError, "snapshot.rootEntityIds must include all root entities (parentless entities).");
        return false;
    }

    return true;
}

bool EntitySnapshotSchemaBase::ReadEntityRecord(JsonUtils::JsonVal* obj, EntityRecord& outEntity, std::string* outError) const{
    if(!RequireUInt64Field(obj, "id", outEntity.id, outError)){
        return false;
    }
    if(!ReadOptionalStringField(obj, "name", outEntity.name, outError)){
        return false;
    }
    if(!ReadOptionalBoolField(obj, "enabled", outEntity.enabled, outError)){
        return false;
    }
    if(JsonUtils::ObjHasKey(obj, "parentId")){
        outEntity.hasParentId = true;
        if(!RequireUInt64Field(obj, "parentId", outEntity.parentId, outError)){
            return false;
        }
    }
    if(!ReadStringArrayField(obj, "tags", outEntity.tags, outError, false)){
        return false;
    }

    JsonUtils::JsonVal* componentsArr = JsonUtils::ObjGetArray(obj, "components");
    if(!componentsArr){
        outEntity.components.clear();
        return true;
    }

    const size_t count = yyjson_arr_size(componentsArr);
    outEntity.components.clear();
    outEntity.components.reserve(count);
    for(size_t i = 0; i < count; ++i){
        JsonUtils::JsonVal* item = yyjson_arr_get(componentsArr, i);
        if(!item || !yyjson_is_obj(item)){
            SetDocSchemaError(outError, "Field 'components' must contain only objects.");
            PrefixDocSchemaError(outError, "components[" + std::to_string(i) + "]: ");
            return false;
        }

        ComponentRecord component;
        if(!ReadComponentRecord(item, component, outError)){
            PrefixDocSchemaError(outError, "components[" + std::to_string(i) + "]: ");
            return false;
        }
        outEntity.components.push_back(component);
    }

    return true;
}

bool EntitySnapshotSchemaBase::WriteEntityRecord(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* arr, const EntityRecord& entity, std::string* outError) const{
    JsonUtils::JsonMutVal* obj = yyjson_mut_arr_add_obj(doc, arr);
    if(!obj){
        SetDocSchemaError(outError, "Failed to create entity object.");
        return false;
    }

    if(!JsonUtils::MutObjAddUInt64(doc, obj, "id", entity.id)){
        SetDocSchemaError(outError, "Failed to write entity 'id'.");
        return false;
    }
    if(!entity.name.empty() && !JsonUtils::MutObjAddString(doc, obj, "name", entity.name)){
        SetDocSchemaError(outError, "Failed to write entity 'name'.");
        return false;
    }
    if(entity.hasParentId && !JsonUtils::MutObjAddUInt64(doc, obj, "parentId", entity.parentId)){
        SetDocSchemaError(outError, "Failed to write entity 'parentId'.");
        return false;
    }
    if(entity.enabled != true && !JsonUtils::MutObjAddBool(doc, obj, "enabled", entity.enabled)){
        SetDocSchemaError(outError, "Failed to write entity 'enabled'.");
        return false;
    }
    if(!WriteStringArrayField(doc, obj, "tags", entity.tags, outError)){
        return false;
    }

    JsonUtils::JsonMutVal* componentsArr = yyjson_mut_obj_add_arr(doc, obj, "components");
    if(!componentsArr){
        SetDocSchemaError(outError, "Failed to create entity 'components' array.");
        return false;
    }
    for(size_t i = 0; i < entity.components.size(); ++i){
        if(!WriteComponentRecord(doc, componentsArr, entity.components[i], outError)){
            PrefixDocSchemaError(outError, "components[" + std::to_string(i) + "]: ");
            return false;
        }
    }

    return true;
}

bool EntitySnapshotSchemaBase::ReadComponentRecord(JsonUtils::JsonVal* obj, ComponentRecord& outComponent, std::string* outError) const{
    if(!RequireStringField(obj, "type", outComponent.type, outError)){
        return false;
    }
    if(!ReadOptionalIntField(obj, "version", outComponent.version, outError)){
        return false;
    }
    if(outComponent.version <= 0){
        outComponent.version = 1;
    }

    JsonUtils::JsonVal* payload = JsonUtils::ObjGet(obj, "payload");
    if(!payload){
        SetDocSchemaError(outError, "Missing required field 'payload'.");
        return false;
    }
    return CaptureJsonValue(payload, outComponent.payloadJson, outError, "payload");
}

bool EntitySnapshotSchemaBase::WriteComponentRecord(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* arr, const ComponentRecord& component, std::string* outError) const{
    JsonUtils::JsonMutVal* obj = yyjson_mut_arr_add_obj(doc, arr);
    if(!obj){
        SetDocSchemaError(outError, "Failed to create component object.");
        return false;
    }

    if(!JsonUtils::MutObjAddString(doc, obj, "type", component.type) ||
       !JsonUtils::MutObjAddInt(doc, obj, "version", component.version)){
        SetDocSchemaError(outError, "Failed to write component header fields.");
        return false;
    }

    JsonUtils::JsonMutVal* payload = nullptr;
    if(!CopyRawJsonToMutableValue(doc, component.payloadJson, payload, outError, "payload", false)){
        return false;
    }
    if(!yyjson_mut_obj_add_val(doc, obj, "payload", payload)){
        SetDocSchemaError(outError, "Failed to attach component payload JSON.");
        return false;
    }

    return true;
}

bool EntitySnapshotSchemaBase::WriteUInt64ArrayField(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* obj, const char* key, const std::vector<std::uint64_t>& values, std::string* outError) const{
    JsonUtils::JsonMutVal* arr = yyjson_mut_obj_add_arr(doc, obj, key);
    if(!arr){
        SetDocSchemaError(outError, "Failed to create array field '" + FieldName(key) + "'.");
        return false;
    }

    for(size_t i = 0; i < values.size(); ++i){
        if(!yyjson_mut_arr_add_uint(doc, arr, values[i])){
            SetDocSchemaError(outError, "Failed to write uint array element for field '" + FieldName(key) + "'.");
            return false;
        }
    }
    return true;
}

bool EntitySnapshotSchemaBase::CaptureJsonValue(JsonUtils::JsonVal* value, std::string& outJson, std::string* outError, const char* debugName) const{
    if(!value){
        SetDocSchemaError(outError, std::string("Cannot capture null JSON value for '") + (debugName ? debugName : "value") + "'.");
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
        SetDocSchemaError(outError, message);
        return false;
    }

    outJson.assign(json, len);
    std::free(json);
    return true;
}

bool EntitySnapshotSchemaBase::CopyRawJsonToMutableValue(
    yyjson_mut_doc* doc,
    const std::string& jsonText,
    JsonUtils::JsonMutVal*& outValue,
    std::string* outError,
    const char* debugName,
    bool requireObject
) const{
    outValue = nullptr;

    JsonUtils::Document temp;
    if(!JsonUtils::LoadDocumentFromText(jsonText, temp, outError)){
        PrefixDocSchemaError(outError, std::string("Invalid raw JSON for '") + (debugName ? debugName : "value") + "': ");
        return false;
    }

    JsonUtils::JsonVal* root = temp.root();
    if(!root){
        SetDocSchemaError(outError, std::string("Raw JSON for '") + (debugName ? debugName : "value") + "' has no root.");
        return false;
    }
    if(requireObject && !yyjson_is_obj(root)){
        SetDocSchemaError(outError, std::string("Raw JSON for '") + (debugName ? debugName : "value") + "' must be an object.");
        return false;
    }

    outValue = yyjson_val_mut_copy(doc, root);
    if(!outValue){
        SetDocSchemaError(outError, std::string("Failed to copy raw JSON for '") + (debugName ? debugName : "value") + "'.");
        return false;
    }

    return true;
}

PrefabSchema::PrefabSchema(){
    Clear();
}

const char* PrefabSchema::SchemaType() const{
    return "prefab";
}

int PrefabSchema::CurrentVersion() const{
    return 1;
}

void PrefabSchema::Clear(){
    metadata.Clear();
    dependencies.clear();
    prefabSettings.hasValue = true;
    prefabSettings.json = "{}";
    variant.Clear();
    ClearSnapshot();
}

void PrefabSchema::ResetDocumentFieldsState(){
    metadata.Clear();
    dependencies.clear();
    prefabSettings.hasValue = true;
    prefabSettings.json = "{}";
    variant.Clear();
}

bool PrefabSchema::DeserializeDocumentFields(JsonUtils::JsonVal* payload, int, std::string* outError){
    if(!ReadMetadataBlock(payload, metadata, outError)){
        return false;
    }
    if(!ReadStringArrayField(payload, "dependencies", dependencies, outError, false)){
        return false;
    }
    if(!ReadOptionalRawJsonField(payload, "prefabSettings", prefabSettings, outError, true)){
        return false;
    }
    if(!prefabSettings.hasValue){
        prefabSettings.hasValue = true;
        prefabSettings.json = "{}";
    }
    if(!ReadOptionalRawJsonField(payload, "variant", variant, outError, true)){
        return false;
    }
    return true;
}

bool PrefabSchema::SerializeDocumentFields(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, int, std::string* outError) const{
    if(!WriteMetadataBlock(doc, payload, metadata, outError)){
        return false;
    }
    if(!WriteStringArrayField(doc, payload, "dependencies", dependencies, outError)){
        return false;
    }
    if(!WriteOptionalRawJsonField(doc, payload, "prefabSettings", prefabSettings, outError, true)){
        return false;
    }
    if(!WriteOptionalRawJsonField(doc, payload, "variant", variant, outError, true)){
        return false;
    }
    return true;
}

bool PrefabSchema::ValidateDocumentState(std::string* outError) const{
    if(!prefabSettings.hasValue){
        SetDocSchemaError(outError, "prefabSettings must be present (use '{}' for empty settings).");
        return false;
    }
    if(prefabSettings.json.empty()){
        SetDocSchemaError(outError, "prefabSettings JSON must not be empty.");
        return false;
    }
    return true;
}

SceneSchema::SceneSchema(){
    Clear();
}

const char* SceneSchema::SchemaType() const{
    return "scene";
}

int SceneSchema::CurrentVersion() const{
    return 1;
}

void SceneSchema::Clear(){
    metadata.Clear();
    dependencies.clear();
    sceneSettings.hasValue = true;
    sceneSettings.json = "{}";
    editorState.Clear();
    ClearSnapshot();
}

void SceneSchema::ResetDocumentFieldsState(){
    metadata.Clear();
    dependencies.clear();
    sceneSettings.hasValue = true;
    sceneSettings.json = "{}";
    editorState.Clear();
}

bool SceneSchema::DeserializeDocumentFields(JsonUtils::JsonVal* payload, int, std::string* outError){
    if(!ReadMetadataBlock(payload, metadata, outError)){
        return false;
    }
    if(!ReadStringArrayField(payload, "dependencies", dependencies, outError, false)){
        return false;
    }
    if(!ReadOptionalRawJsonField(payload, "sceneSettings", sceneSettings, outError, true)){
        return false;
    }
    if(!sceneSettings.hasValue){
        sceneSettings.hasValue = true;
        sceneSettings.json = "{}";
    }
    if(!ReadOptionalRawJsonField(payload, "editorState", editorState, outError, true)){
        return false;
    }
    return true;
}

bool SceneSchema::SerializeDocumentFields(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, int, std::string* outError) const{
    if(!WriteMetadataBlock(doc, payload, metadata, outError)){
        return false;
    }
    if(!WriteStringArrayField(doc, payload, "dependencies", dependencies, outError)){
        return false;
    }
    if(!WriteOptionalRawJsonField(doc, payload, "sceneSettings", sceneSettings, outError, true)){
        return false;
    }
    if(!WriteOptionalRawJsonField(doc, payload, "editorState", editorState, outError, true)){
        return false;
    }
    return true;
}

bool SceneSchema::ValidateDocumentState(std::string* outError) const{
    if(!sceneSettings.hasValue){
        SetDocSchemaError(outError, "sceneSettings must be present (use '{}' for empty settings).");
        return false;
    }
    if(sceneSettings.json.empty()){
        SetDocSchemaError(outError, "sceneSettings JSON must not be empty.");
        return false;
    }
    return true;
}

} // namespace JsonSchema
