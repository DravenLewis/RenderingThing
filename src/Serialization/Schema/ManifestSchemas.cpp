/**
 * @file src/Serialization/Schema/ManifestSchemas.cpp
 * @brief Implementation for ManifestSchemas.
 */

#include "Serialization/Schema/ManifestSchemas.h"

#include <set>

namespace {

void SetManifestError(std::string* outError, const std::string& message){
    if(outError){
        *outError = message;
    }
}

void PrefixManifestError(std::string* outError, const std::string& prefix){
    if(outError && !outError->empty()){
        *outError = prefix + *outError;
    }
}

std::string FieldName(const char* key){
    return key ? std::string(key) : std::string("<null>");
}

bool ValidateManifestKind(const std::string& kind, const std::set<std::string>& allowed, std::string* outError, const std::string& label){
    if(allowed.find(kind) != allowed.end()){
        return true;
    }

    std::string list;
    for(std::set<std::string>::const_iterator it = allowed.begin(); it != allowed.end(); ++it){
        if(!list.empty()){
            list += ", ";
        }
        list += "'";
        list += *it;
        list += "'";
    }

    SetManifestError(outError, label + " has unsupported kind '" + kind + "'. Allowed: " + list + ".");
    return false;
}

} // namespace

namespace JsonSchema {

bool ManifestSchemaBase::DeserializePayload(yyjson_val* payload, int version, std::string* outError){
    ResetManifestState();

    // Payload-level version mirror is optional when reading, but if present it must agree
    // with the canonical top-level schema version.
    if(JsonUtils::ObjHasKey(payload, "manifestVersion")){
        int payloadVersion = 0;
        if(!JsonUtils::TryGetInt(payload, "manifestVersion", payloadVersion)){
            SetManifestError(outError, "Field 'manifestVersion' must be an integer when present.");
            return false;
        }
        if(payloadVersion != version){
            SetManifestError(
                outError,
                "Payload 'manifestVersion' (" + std::to_string(payloadVersion) +
                ") does not match document version (" + std::to_string(version) + ")."
            );
            return false;
        }
    }

    return DeserializeManifestPayload(payload, version, outError);
}

bool ManifestSchemaBase::SerializePayload(yyjson_mut_doc* doc, yyjson_mut_val* payload, int version, std::string* outError) const{
    if(!JsonUtils::MutObjAddInt(doc, payload, "manifestVersion", version)){
        SetManifestError(outError, "Failed to write 'manifestVersion' field.");
        return false;
    }
    return SerializeManifestPayload(doc, payload, version, outError);
}

bool ManifestSchemaBase::RequireStringField(yyjson_val* obj, const char* key, std::string& outValue, std::string* outError) const{
    if(JsonUtils::TryGetString(obj, key, outValue)){
        return true;
    }
    SetManifestError(outError, "Missing required string field '" + FieldName(key) + "'.");
    return false;
}

bool ManifestSchemaBase::RequireBoolField(yyjson_val* obj, const char* key, bool& outValue, std::string* outError) const{
    if(JsonUtils::TryGetBool(obj, key, outValue)){
        return true;
    }
    SetManifestError(outError, "Missing required bool field '" + FieldName(key) + "'.");
    return false;
}

bool ManifestSchemaBase::RequireIntField(yyjson_val* obj, const char* key, int& outValue, std::string* outError) const{
    if(JsonUtils::TryGetInt(obj, key, outValue)){
        return true;
    }
    SetManifestError(outError, "Missing required integer field '" + FieldName(key) + "'.");
    return false;
}

bool ManifestSchemaBase::RequireUInt64Field(yyjson_val* obj, const char* key, std::uint64_t& outValue, std::string* outError) const{
    if(JsonUtils::TryGetUInt64(obj, key, outValue)){
        return true;
    }
    SetManifestError(outError, "Missing required unsigned integer field '" + FieldName(key) + "'.");
    return false;
}

bool ManifestSchemaBase::RequireObjectField(yyjson_val* obj, const char* key, yyjson_val*& outValue, std::string* outError) const{
    outValue = JsonUtils::ObjGetObject(obj, key);
    if(outValue){
        return true;
    }
    SetManifestError(outError, "Missing required object field '" + FieldName(key) + "'.");
    return false;
}

bool ManifestSchemaBase::RequireArrayField(yyjson_val* obj, const char* key, yyjson_val*& outValue, std::string* outError) const{
    outValue = JsonUtils::ObjGetArray(obj, key);
    if(outValue){
        return true;
    }
    SetManifestError(outError, "Missing required array field '" + FieldName(key) + "'.");
    return false;
}

bool ManifestSchemaBase::ReadOptionalStringField(yyjson_val* obj, const char* key, std::string& outValue, std::string* outError) const{
    if(!JsonUtils::ObjHasKey(obj, key)){
        return true;
    }
    if(JsonUtils::TryGetString(obj, key, outValue)){
        return true;
    }
    SetManifestError(outError, "Field '" + FieldName(key) + "' must be a string.");
    return false;
}

bool ManifestSchemaBase::ReadOptionalBoolField(yyjson_val* obj, const char* key, bool& outValue, std::string* outError) const{
    if(!JsonUtils::ObjHasKey(obj, key)){
        return true;
    }
    if(JsonUtils::TryGetBool(obj, key, outValue)){
        return true;
    }
    SetManifestError(outError, "Field '" + FieldName(key) + "' must be a bool.");
    return false;
}

bool ManifestSchemaBase::ReadOptionalIntField(yyjson_val* obj, const char* key, int& outValue, std::string* outError) const{
    if(!JsonUtils::ObjHasKey(obj, key)){
        return true;
    }
    if(JsonUtils::TryGetInt(obj, key, outValue)){
        return true;
    }
    SetManifestError(outError, "Field '" + FieldName(key) + "' must be an integer.");
    return false;
}

bool ManifestSchemaBase::ReadOptionalUInt64Field(yyjson_val* obj, const char* key, std::uint64_t& outValue, std::string* outError) const{
    if(!JsonUtils::ObjHasKey(obj, key)){
        return true;
    }
    if(JsonUtils::TryGetUInt64(obj, key, outValue)){
        return true;
    }
    SetManifestError(outError, "Field '" + FieldName(key) + "' must be an unsigned integer.");
    return false;
}

bool ManifestSchemaBase::ReadStringArray(yyjson_val* arrValue, std::vector<std::string>& outValues, std::string* outError, const char* debugName) const{
    if(!arrValue || !yyjson_is_arr(arrValue)){
        SetManifestError(
            outError,
            std::string("Expected array for ") + (debugName ? debugName : "string array") + "."
        );
        return false;
    }

    outValues.clear();
    const size_t count = yyjson_arr_size(arrValue);
    outValues.reserve(count);
    for(size_t i = 0; i < count; ++i){
        yyjson_val* item = yyjson_arr_get(arrValue, i);
        if(!item || !yyjson_is_str(item)){
            SetManifestError(
                outError,
                std::string("Expected string at ") + (debugName ? debugName : "array") +
                "[" + std::to_string(i) + "]."
            );
            return false;
        }
        const char* s = yyjson_get_str(item);
        outValues.push_back(s ? s : "");
    }
    return true;
}

bool ManifestSchemaBase::ReadStringArrayField(yyjson_val* obj, const char* key, std::vector<std::string>& outValues, std::string* outError, bool required) const{
    yyjson_val* arr = JsonUtils::ObjGetArray(obj, key);
    if(!arr){
        if(required){
            SetManifestError(outError, "Missing required string array field '" + FieldName(key) + "'.");
            return false;
        }
        outValues.clear();
        return true;
    }
    return ReadStringArray(arr, outValues, outError, key);
}

bool ManifestSchemaBase::WriteStringArrayField(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const std::vector<std::string>& values, std::string* outError) const{
    if(values.empty()){
        return true;
    }

    yyjson_mut_val* arr = yyjson_mut_obj_add_arr(doc, obj, key);
    if(!arr){
        SetManifestError(outError, "Failed to create array field '" + FieldName(key) + "'.");
        return false;
    }

    for(size_t i = 0; i < values.size(); ++i){
        if(!yyjson_mut_arr_add_strcpy(doc, arr, values[i].c_str())){
            SetManifestError(outError, "Failed to write string array element for field '" + FieldName(key) + "'.");
            return false;
        }
    }

    return true;
}

bool ManifestSchemaBase::WriteStringMapField(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const std::map<std::string, std::string>& values, std::string* outError) const{
    if(values.empty()){
        return true;
    }

    yyjson_mut_val* child = yyjson_mut_obj_add_obj(doc, obj, key);
    if(!child){
        SetManifestError(outError, "Failed to create object field '" + FieldName(key) + "'.");
        return false;
    }

    for(std::map<std::string, std::string>::const_iterator it = values.begin(); it != values.end(); ++it){
        if(!JsonUtils::MutObjAddString(doc, child, it->first.c_str(), it->second)){
            SetManifestError(outError, "Failed to write map entry '" + it->first + "' in field '" + FieldName(key) + "'.");
            return false;
        }
    }

    return true;
}

AssetManifestSchema::AssetManifestSchema(){
    Clear();
}

const char* AssetManifestSchema::SchemaType() const{
    return "asset-manifest";
}

int AssetManifestSchema::CurrentVersion() const{
    return 1;
}

void AssetManifestSchema::Clear(){
    bundleAlias.clear();
    rootEntry.clear();
    entries.clear();
}

bool AssetManifestSchema::OnBeforeSave(int, std::string* outError) const{
    return ValidateState(outError);
}

void AssetManifestSchema::ResetManifestState(){
    Clear();
}

bool AssetManifestSchema::DeserializeManifestPayload(yyjson_val* payload, int, std::string* outError){
    if(!ReadOptionalStringField(payload, "bundleAlias", bundleAlias, outError)){
        return false;
    }
    if(!ReadOptionalStringField(payload, "rootEntry", rootEntry, outError)){
        return false;
    }

    yyjson_val* entriesArray = nullptr;
    if(!RequireArrayField(payload, "entries", entriesArray, outError)){
        return false;
    }

    const size_t count = yyjson_arr_size(entriesArray);
    entries.clear();
    entries.reserve(count);

    for(size_t i = 0; i < count; ++i){
        yyjson_val* item = yyjson_arr_get(entriesArray, i);
        if(!item || !yyjson_is_obj(item)){
            SetManifestError(outError, "Field 'entries' must contain only objects.");
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }

        Entry entry;
        if(!RequireStringField(item, "path", entry.path, outError)){
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }
        if(!ReadOptionalStringField(item, "kind", entry.kind, outError)){
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }
        if(entry.kind.empty()){
            entry.kind = "file";
        }

        if(!ReadOptionalStringField(item, "alias", entry.alias, outError) ||
           !ReadOptionalStringField(item, "sourceRef", entry.sourceRef, outError) ||
           !ReadOptionalUInt64Field(item, "size", entry.size, outError) ||
           !ReadOptionalStringField(item, "hash", entry.hash, outError) ||
           !ReadOptionalStringField(item, "compression", entry.compression, outError) ||
           !ReadOptionalBoolField(item, "readonly", entry.readOnly, outError)){
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }

        // Compatibility with an alternate bool-based shape if you choose to use it later.
        if(JsonUtils::ObjHasKey(item, "isDirectory")){
            bool isDirectory = false;
            if(!JsonUtils::TryGetBool(item, "isDirectory", isDirectory)){
                SetManifestError(outError, "Field 'isDirectory' must be a bool.");
                PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
                return false;
            }
            if(JsonUtils::ObjHasKey(item, "kind")){
                const std::string expectedKind = isDirectory ? "directory" : "file";
                if(entry.kind != expectedKind){
                    SetManifestError(outError, "Field 'kind' conflicts with 'isDirectory'.");
                    PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
                    return false;
                }
            }else{
                entry.kind = isDirectory ? "directory" : "file";
            }
        }

        if(JsonUtils::ObjHasKey(item, "tags")){
            yyjson_val* tagsArray = JsonUtils::ObjGetArray(item, "tags");
            if(!ReadStringArray(tagsArray, entry.tags, outError, "tags")){
                PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
                return false;
            }
        }

        entries.push_back(entry);
    }

    return ValidateState(outError);
}

bool AssetManifestSchema::SerializeManifestPayload(yyjson_mut_doc* doc, yyjson_mut_val* payload, int, std::string* outError) const{
    if(!bundleAlias.empty() && !JsonUtils::MutObjAddString(doc, payload, "bundleAlias", bundleAlias)){
        SetManifestError(outError, "Failed to write 'bundleAlias'.");
        return false;
    }
    if(!rootEntry.empty() && !JsonUtils::MutObjAddString(doc, payload, "rootEntry", rootEntry)){
        SetManifestError(outError, "Failed to write 'rootEntry'.");
        return false;
    }

    yyjson_mut_val* entriesArray = yyjson_mut_obj_add_arr(doc, payload, "entries");
    if(!entriesArray){
        SetManifestError(outError, "Failed to create 'entries' array.");
        return false;
    }

    for(size_t i = 0; i < entries.size(); ++i){
        const Entry& entry = entries[i];
        yyjson_mut_val* item = yyjson_mut_arr_add_obj(doc, entriesArray);
        if(!item){
            SetManifestError(outError, "Failed to add entry object to 'entries'.");
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }

        if(!JsonUtils::MutObjAddString(doc, item, "path", entry.path) ||
           !JsonUtils::MutObjAddString(doc, item, "kind", entry.kind)){
            SetManifestError(outError, "Failed to write required asset entry fields.");
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }

        if(!entry.alias.empty() && !JsonUtils::MutObjAddString(doc, item, "alias", entry.alias)){
            SetManifestError(outError, "Failed to write asset entry 'alias'.");
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }
        if(!entry.sourceRef.empty() && !JsonUtils::MutObjAddString(doc, item, "sourceRef", entry.sourceRef)){
            SetManifestError(outError, "Failed to write asset entry 'sourceRef'.");
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }
        if(entry.size > 0 && !JsonUtils::MutObjAddUInt64(doc, item, "size", entry.size)){
            SetManifestError(outError, "Failed to write asset entry 'size'.");
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }
        if(!entry.hash.empty() && !JsonUtils::MutObjAddString(doc, item, "hash", entry.hash)){
            SetManifestError(outError, "Failed to write asset entry 'hash'.");
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }
        if(!entry.compression.empty() && !JsonUtils::MutObjAddString(doc, item, "compression", entry.compression)){
            SetManifestError(outError, "Failed to write asset entry 'compression'.");
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }
        if(entry.readOnly && !JsonUtils::MutObjAddBool(doc, item, "readonly", entry.readOnly)){
            SetManifestError(outError, "Failed to write asset entry 'readonly'.");
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }
        if(!WriteStringArrayField(doc, item, "tags", entry.tags, outError)){
            PrefixManifestError(outError, "entries[" + std::to_string(i) + "]: ");
            return false;
        }
    }

    return true;
}

bool AssetManifestSchema::ValidateState(std::string* outError) const{
    static const std::set<std::string> kKinds = {"directory", "file"};

    std::set<std::string> seenPaths;
    std::set<std::string> seenAliases;
    bool foundRoot = rootEntry.empty();

    for(size_t i = 0; i < entries.size(); ++i){
        const Entry& entry = entries[i];
        const std::string label = "entries[" + std::to_string(i) + "]";

        if(entry.path.empty()){
            SetManifestError(outError, label + ".path must not be empty.");
            return false;
        }
        if(!ValidateManifestKind(entry.kind, kKinds, outError, label)){
            return false;
        }
        if(!seenPaths.insert(entry.path).second){
            SetManifestError(outError, "Duplicate asset manifest entry path: '" + entry.path + "'.");
            return false;
        }
        if(!entry.alias.empty() && !seenAliases.insert(entry.alias).second){
            SetManifestError(outError, "Duplicate asset manifest entry alias: '" + entry.alias + "'.");
            return false;
        }
        if(!rootEntry.empty() && entry.path == rootEntry){
            foundRoot = true;
        }
    }

    if(!foundRoot){
        SetManifestError(outError, "Asset manifest rootEntry '" + rootEntry + "' was not found in entries.");
        return false;
    }

    return true;
}

GameManifestSchema::GameManifestSchema(){
    Clear();
}

const char* GameManifestSchema::SchemaType() const{
    return "game-manifest";
}

int GameManifestSchema::CurrentVersion() const{
    return 1;
}

void GameManifestSchema::Clear(){
    game = GameInfo{};
    window = WindowConfig{};
    render = RenderConfig{};
    assetSources.clear();
    scenes.clear();
    startupScene.clear();
}

bool GameManifestSchema::OnBeforeSave(int, std::string* outError) const{
    return ValidateState(outError);
}

void GameManifestSchema::ResetManifestState(){
    Clear();
}

bool GameManifestSchema::DeserializeManifestPayload(yyjson_val* payload, int, std::string* outError){
    yyjson_val* gameObj = nullptr;
    if(RequireObjectField(payload, "game", gameObj, outError)){
        if(!RequireStringField(gameObj, "name", game.name, outError)){
            PrefixManifestError(outError, "game: ");
            return false;
        }
        if(!ReadOptionalStringField(gameObj, "version", game.version, outError)){
            PrefixManifestError(outError, "game: ");
            return false;
        }
    }else{
        return false;
    }

    if(JsonUtils::ObjHasKey(payload, "window")){
        yyjson_val* windowObj = JsonUtils::ObjGetObject(payload, "window");
        if(!windowObj){
            SetManifestError(outError, "Field 'window' must be an object.");
            return false;
        }

        if(!ReadOptionalStringField(windowObj, "title", window.title, outError) ||
           !ReadOptionalIntField(windowObj, "width", window.width, outError) ||
           !ReadOptionalIntField(windowObj, "height", window.height, outError) ||
           !ReadOptionalBoolField(windowObj, "resizable", window.resizable, outError)){
            PrefixManifestError(outError, "window: ");
            return false;
        }
    }

    if(JsonUtils::ObjHasKey(payload, "render")){
        yyjson_val* renderObj = JsonUtils::ObjGetObject(payload, "render");
        if(!renderObj){
            SetManifestError(outError, "Field 'render' must be an object.");
            return false;
        }
        if(!ReadOptionalStringField(renderObj, "defaultPipeline", render.defaultPipeline, outError)){
            PrefixManifestError(outError, "render: ");
            return false;
        }
    }

    if(JsonUtils::ObjHasKey(payload, "assetSources")){
        yyjson_val* assetSourcesArray = JsonUtils::ObjGetArray(payload, "assetSources");
        if(!assetSourcesArray){
            SetManifestError(outError, "Field 'assetSources' must be an array.");
            return false;
        }

        assetSources.clear();
        const size_t count = yyjson_arr_size(assetSourcesArray);
        assetSources.reserve(count);
        for(size_t i = 0; i < count; ++i){
            yyjson_val* item = yyjson_arr_get(assetSourcesArray, i);
            if(!item || !yyjson_is_obj(item)){
                SetManifestError(outError, "Field 'assetSources' must contain only objects.");
                PrefixManifestError(outError, "assetSources[" + std::to_string(i) + "]: ");
                return false;
            }

            AssetSource source;
            if(!RequireStringField(item, "ref", source.ref, outError)){
                PrefixManifestError(outError, "assetSources[" + std::to_string(i) + "]: ");
                return false;
            }
            if(!ReadOptionalStringField(item, "kind", source.kind, outError) ||
               !ReadOptionalStringField(item, "alias", source.alias, outError) ||
               !ReadOptionalBoolField(item, "enabled", source.enabled, outError)){
                PrefixManifestError(outError, "assetSources[" + std::to_string(i) + "]: ");
                return false;
            }
            if(source.kind.empty()){
                source.kind = "folder";
            }

            assetSources.push_back(source);
        }
    }

    yyjson_val* scenesArray = nullptr;
    if(!RequireArrayField(payload, "scenes", scenesArray, outError)){
        return false;
    }

    scenes.clear();
    const size_t sceneCount = yyjson_arr_size(scenesArray);
    scenes.reserve(sceneCount);
    for(size_t i = 0; i < sceneCount; ++i){
        yyjson_val* item = yyjson_arr_get(scenesArray, i);
        if(!item || !yyjson_is_obj(item)){
            SetManifestError(outError, "Field 'scenes' must contain only objects.");
            PrefixManifestError(outError, "scenes[" + std::to_string(i) + "]: ");
            return false;
        }

        SceneEntry scene;
        if(!RequireStringField(item, "id", scene.id, outError) ||
           !RequireStringField(item, "ref", scene.ref, outError) ||
           !ReadOptionalStringField(item, "name", scene.name, outError)){
            PrefixManifestError(outError, "scenes[" + std::to_string(i) + "]: ");
            return false;
        }
        scenes.push_back(scene);
    }

    if(!RequireStringField(payload, "startupScene", startupScene, outError)){
        return false;
    }

    return ValidateState(outError);
}

bool GameManifestSchema::SerializeManifestPayload(yyjson_mut_doc* doc, yyjson_mut_val* payload, int, std::string* outError) const{
    yyjson_mut_val* gameObj = yyjson_mut_obj_add_obj(doc, payload, "game");
    if(!gameObj){
        SetManifestError(outError, "Failed to create 'game' object.");
        return false;
    }
    if(!JsonUtils::MutObjAddString(doc, gameObj, "name", game.name)){
        SetManifestError(outError, "Failed to write 'game.name'.");
        return false;
    }
    if(!game.version.empty() && !JsonUtils::MutObjAddString(doc, gameObj, "version", game.version)){
        SetManifestError(outError, "Failed to write 'game.version'.");
        return false;
    }

    yyjson_mut_val* windowObj = yyjson_mut_obj_add_obj(doc, payload, "window");
    if(!windowObj){
        SetManifestError(outError, "Failed to create 'window' object.");
        return false;
    }
    if(!window.title.empty() && !JsonUtils::MutObjAddString(doc, windowObj, "title", window.title)){
        SetManifestError(outError, "Failed to write 'window.title'.");
        return false;
    }
    if(!JsonUtils::MutObjAddInt(doc, windowObj, "width", window.width) ||
       !JsonUtils::MutObjAddInt(doc, windowObj, "height", window.height) ||
       !JsonUtils::MutObjAddBool(doc, windowObj, "resizable", window.resizable)){
        SetManifestError(outError, "Failed to write 'window' settings.");
        return false;
    }

    yyjson_mut_val* renderObj = yyjson_mut_obj_add_obj(doc, payload, "render");
    if(!renderObj){
        SetManifestError(outError, "Failed to create 'render' object.");
        return false;
    }
    if(!JsonUtils::MutObjAddString(doc, renderObj, "defaultPipeline", render.defaultPipeline)){
        SetManifestError(outError, "Failed to write 'render.defaultPipeline'.");
        return false;
    }

    yyjson_mut_val* assetSourcesArray = yyjson_mut_obj_add_arr(doc, payload, "assetSources");
    if(!assetSourcesArray){
        SetManifestError(outError, "Failed to create 'assetSources' array.");
        return false;
    }
    for(size_t i = 0; i < assetSources.size(); ++i){
        const AssetSource& source = assetSources[i];
        yyjson_mut_val* item = yyjson_mut_arr_add_obj(doc, assetSourcesArray);
        if(!item){
            SetManifestError(outError, "Failed to add asset source object.");
            PrefixManifestError(outError, "assetSources[" + std::to_string(i) + "]: ");
            return false;
        }
        if(!JsonUtils::MutObjAddString(doc, item, "kind", source.kind) ||
           !JsonUtils::MutObjAddString(doc, item, "ref", source.ref)){
            SetManifestError(outError, "Failed to write required asset source fields.");
            PrefixManifestError(outError, "assetSources[" + std::to_string(i) + "]: ");
            return false;
        }
        if(!source.alias.empty() && !JsonUtils::MutObjAddString(doc, item, "alias", source.alias)){
            SetManifestError(outError, "Failed to write asset source 'alias'.");
            PrefixManifestError(outError, "assetSources[" + std::to_string(i) + "]: ");
            return false;
        }
        if(source.enabled != true && !JsonUtils::MutObjAddBool(doc, item, "enabled", source.enabled)){
            SetManifestError(outError, "Failed to write asset source 'enabled'.");
            PrefixManifestError(outError, "assetSources[" + std::to_string(i) + "]: ");
            return false;
        }
    }

    yyjson_mut_val* scenesArray = yyjson_mut_obj_add_arr(doc, payload, "scenes");
    if(!scenesArray){
        SetManifestError(outError, "Failed to create 'scenes' array.");
        return false;
    }
    for(size_t i = 0; i < scenes.size(); ++i){
        const SceneEntry& scene = scenes[i];
        yyjson_mut_val* item = yyjson_mut_arr_add_obj(doc, scenesArray);
        if(!item){
            SetManifestError(outError, "Failed to add scene object.");
            PrefixManifestError(outError, "scenes[" + std::to_string(i) + "]: ");
            return false;
        }
        if(!JsonUtils::MutObjAddString(doc, item, "id", scene.id) ||
           !JsonUtils::MutObjAddString(doc, item, "ref", scene.ref)){
            SetManifestError(outError, "Failed to write required scene fields.");
            PrefixManifestError(outError, "scenes[" + std::to_string(i) + "]: ");
            return false;
        }
        if(!scene.name.empty() && !JsonUtils::MutObjAddString(doc, item, "name", scene.name)){
            SetManifestError(outError, "Failed to write scene 'name'.");
            PrefixManifestError(outError, "scenes[" + std::to_string(i) + "]: ");
            return false;
        }
    }

    if(!JsonUtils::MutObjAddString(doc, payload, "startupScene", startupScene)){
        SetManifestError(outError, "Failed to write 'startupScene'.");
        return false;
    }

    return true;
}

bool GameManifestSchema::ValidateState(std::string* outError) const{
    static const std::set<std::string> kAssetSourceKinds = {"bundle", "folder"};

    if(game.name.empty()){
        SetManifestError(outError, "game.name must not be empty.");
        return false;
    }
    if(window.width <= 0 || window.height <= 0){
        SetManifestError(outError, "window.width and window.height must be > 0.");
        return false;
    }
    if(render.defaultPipeline.empty()){
        SetManifestError(outError, "render.defaultPipeline must not be empty.");
        return false;
    }

    std::set<std::string> sceneIds;
    for(size_t i = 0; i < scenes.size(); ++i){
        const SceneEntry& scene = scenes[i];
        const std::string label = "scenes[" + std::to_string(i) + "]";
        if(scene.id.empty()){
            SetManifestError(outError, label + ".id must not be empty.");
            return false;
        }
        if(scene.ref.empty()){
            SetManifestError(outError, label + ".ref must not be empty.");
            return false;
        }
        if(!sceneIds.insert(scene.id).second){
            SetManifestError(outError, "Duplicate game manifest scene id: '" + scene.id + "'.");
            return false;
        }
    }

    for(size_t i = 0; i < assetSources.size(); ++i){
        const AssetSource& source = assetSources[i];
        const std::string label = "assetSources[" + std::to_string(i) + "]";
        if(source.ref.empty()){
            SetManifestError(outError, label + ".ref must not be empty.");
            return false;
        }
        if(!ValidateManifestKind(source.kind, kAssetSourceKinds, outError, label)){
            return false;
        }
    }

    if(startupScene.empty()){
        SetManifestError(outError, "startupScene must not be empty.");
        return false;
    }

    bool foundStartup = false;
    for(size_t i = 0; i < scenes.size(); ++i){
        if(scenes[i].id == startupScene || scenes[i].ref == startupScene){
            foundStartup = true;
            break;
        }
    }
    if(!foundStartup){
        SetManifestError(outError, "startupScene '" + startupScene + "' does not match any scene id/ref.");
        return false;
    }

    return true;
}

} // namespace JsonSchema
