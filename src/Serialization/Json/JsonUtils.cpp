/**
 * @file src/Serialization/Json/JsonUtils.cpp
 * @brief Implementation for JsonUtils.
 */

#include "Serialization/Json/JsonUtils.h"

#include "Assets/Core/Asset.h"
#include "Assets/Core/AssetDescriptorUtils.h"

#include <cstdlib>
#include <filesystem>
#include <limits>

namespace {
    using JsonVal = JsonUtils::JsonVal;
    using JsonMutVal = JsonUtils::JsonMutVal;

    std::string makeReadErrorString(const yyjson_read_err& err){
        std::string message = "JSON read failed";
        if(err.msg){
            message += ": ";
            message += err.msg;
        }
        if(err.pos > 0){
            message += " (pos ";
            message += std::to_string(err.pos);
            message += ")";
        }
        return message;
    }

    std::string makeWriteErrorString(const yyjson_write_err& err){
        std::string message = "JSON write failed";
        if(err.msg){
            message += ": ";
            message += err.msg;
        }
        return message;
    }

    yyjson_write_flag getWriteFlags(bool pretty){
        yyjson_write_flag flags = YYJSON_WRITE_NEWLINE_AT_END;
        if(pretty){
            flags = static_cast<yyjson_write_flag>(flags | YYJSON_WRITE_PRETTY_TWO_SPACES);
        }
        return flags;
    }

    bool tryGetNumberValue(JsonVal* value, double& outValue){
        if(!value || !yyjson_is_num(value)){
            return false;
        }
        outValue = yyjson_get_num(value);
        return true;
    }

    bool tryGetFloatValue(JsonVal* value, float& outValue){
        double number = 0.0;
        if(!tryGetNumberValue(value, number)){
            return false;
        }
        outValue = static_cast<float>(number);
        return true;
    }

    bool tryReadVecNArray(JsonVal* value, float* outValues, size_t count){
        if(!value || !yyjson_is_arr(value) || yyjson_arr_size(value) < count){
            return false;
        }

        for(size_t i = 0; i < count; ++i){
            JsonVal* entry = yyjson_arr_get(value, i);
            if(!tryGetFloatValue(entry, outValues[i])){
                return false;
            }
        }
        return true;
    }
}

namespace JsonUtils {

Document::Document(yyjson_doc* doc) : docPtr(doc) {}

Document::~Document(){
    reset();
}

Document::Document(Document&& other) noexcept : docPtr(other.docPtr){
    other.docPtr = nullptr;
}

Document& Document::operator=(Document&& other) noexcept{
    if(this != &other){
        reset();
        docPtr = other.docPtr;
        other.docPtr = nullptr;
    }
    return *this;
}

void Document::reset(yyjson_doc* doc){
    if(docPtr){
        yyjson_doc_free(docPtr);
    }
    docPtr = doc;
}

JsonVal* Document::root() const{
    return docPtr ? yyjson_doc_get_root(docPtr) : nullptr;
}

MutableDocument::MutableDocument(yyjson_mut_doc* doc) : docPtr(doc) {}

MutableDocument::~MutableDocument(){
    reset();
}

MutableDocument::MutableDocument(MutableDocument&& other) noexcept : docPtr(other.docPtr){
    other.docPtr = nullptr;
}

MutableDocument& MutableDocument::operator=(MutableDocument&& other) noexcept{
    if(this != &other){
        reset();
        docPtr = other.docPtr;
        other.docPtr = nullptr;
    }
    return *this;
}

void MutableDocument::reset(yyjson_mut_doc* doc){
    if(docPtr){
        yyjson_mut_doc_free(docPtr);
    }
    docPtr = doc;
}

bool MutableDocument::create(){
    reset(yyjson_mut_doc_new(nullptr));
    return docPtr != nullptr;
}

bool MutableDocument::copyFrom(const Document& doc){
    if(!doc.valid()){
        reset();
        return false;
    }
    reset(yyjson_doc_mut_copy(doc.get(), nullptr));
    return docPtr != nullptr;
}

bool MutableDocument::copyFrom(const MutableDocument& doc){
    if(!doc.valid()){
        reset();
        return false;
    }
    reset(yyjson_mut_doc_mut_copy(const_cast<yyjson_mut_doc*>(doc.get()), nullptr));
    return docPtr != nullptr;
}

JsonMutVal* MutableDocument::root() const{
    return docPtr ? yyjson_mut_doc_get_root(docPtr) : nullptr;
}

void MutableDocument::setRoot(JsonMutVal* value){
    if(docPtr){
        yyjson_mut_doc_set_root(docPtr, value);
    }
}

JsonMutVal* MutableDocument::setRootObject(){
    if(!docPtr && !create()){
        return nullptr;
    }
    JsonMutVal* obj = yyjson_mut_obj(docPtr);
    if(!obj){
        return nullptr;
    }
    yyjson_mut_doc_set_root(docPtr, obj);
    return obj;
}

JsonMutVal* MutableDocument::setRootArray(){
    if(!docPtr && !create()){
        return nullptr;
    }
    JsonMutVal* arr = yyjson_mut_arr(docPtr);
    if(!arr){
        return nullptr;
    }
    yyjson_mut_doc_set_root(docPtr, arr);
    return arr;
}

bool LoadDocumentFromText(const std::string& jsonText, Document& outDoc, std::string* outError){
    yyjson_read_err err{};
    yyjson_doc* doc = yyjson_read_opts(
        (char*)(void*)jsonText.data(),
        jsonText.size(),
        YYJSON_READ_NOFLAG,
        nullptr,
        &err
    );

    if(!doc){
        if(outError){
            *outError = makeReadErrorString(err);
        }
        outDoc.reset();
        return false;
    }

    outDoc.reset(doc);
    return true;
}

bool LoadDocumentFromAbsolutePath(const std::filesystem::path& path, Document& outDoc, std::string* outError){
    yyjson_read_err err{};
    std::string nativePath = path.string();
    yyjson_doc* doc = yyjson_read_file(nativePath.c_str(), YYJSON_READ_NOFLAG, nullptr, &err);
    if(!doc){
        if(outError){
            std::string message = makeReadErrorString(err);
            message += " [";
            message += path.generic_string();
            message += "]";
            *outError = message;
        }
        outDoc.reset();
        return false;
    }

    outDoc.reset(doc);
    return true;
}

bool LoadDocumentFromAssetRef(const std::string& assetRef, Document& outDoc, std::string* outError){
    std::string text;
    if(!AssetDescriptorUtils::ReadTextAsset(assetRef, text, outError)){
        outDoc.reset();
        return false;
    }
    return LoadDocumentFromText(text, outDoc, outError);
}

bool WriteDocumentToString(const MutableDocument& doc, std::string& outJson, std::string* outError, bool pretty){
    if(!doc.valid() || !doc.root()){
        if(outError){
            *outError = "Cannot write JSON: document is null or has no root.";
        }
        return false;
    }

    yyjson_write_err err{};
    size_t len = 0;
    char* buffer = yyjson_mut_write_opts(doc.get(), getWriteFlags(pretty), nullptr, &len, &err);
    if(!buffer){
        if(outError){
            *outError = makeWriteErrorString(err);
        }
        return false;
    }

    outJson.assign(buffer, len);
    std::free(buffer);
    return true;
}

bool SaveDocumentToAbsolutePath(const std::filesystem::path& path, const MutableDocument& doc, std::string* outError, bool pretty){
    if(!doc.valid() || !doc.root()){
        if(outError){
            *outError = "Cannot save JSON: document is null or has no root.";
        }
        return false;
    }

    std::filesystem::path parent = path.parent_path();
    std::error_code ec;
    if(!parent.empty() && !std::filesystem::exists(parent, ec)){
        if(!std::filesystem::create_directories(parent, ec)){
            if(outError){
                *outError = "Failed to create directory: " + parent.generic_string();
            }
            return false;
        }
    }

    yyjson_write_err err{};
    std::string nativePath = path.string();
    if(!yyjson_mut_write_file(nativePath.c_str(), doc.get(), getWriteFlags(pretty), nullptr, &err)){
        if(outError){
            std::string message = makeWriteErrorString(err);
            message += " [";
            message += path.generic_string();
            message += "]";
            *outError = message;
        }
        return false;
    }

    AssetManager::Instance.unmanageAsset(path.generic_string());
    return true;
}

bool SaveDocumentToAssetRef(const std::string& assetRef, const MutableDocument& doc, std::string* outError, bool pretty){
    std::filesystem::path path;
    if(!AssetDescriptorUtils::AssetRefToAbsolutePath(assetRef, path)){
        if(outError){
            *outError = "Invalid asset ref for JSON save: " + assetRef;
        }
        return false;
    }
    return SaveDocumentToAbsolutePath(path, doc, outError, pretty);
}

bool CreateStandardDocument(MutableDocument& outDoc, const std::string& type, int version, StandardDocumentRefs& outRefs, std::string* outError){
    outRefs = StandardDocumentRefs{};
    if(!outDoc.create()){
        if(outError){
            *outError = "Failed to allocate mutable JSON document.";
        }
        return false;
    }

    JsonMutVal* root = outDoc.setRootObject();
    if(!root){
        if(outError){
            *outError = "Failed to create JSON root object.";
        }
        return false;
    }

    if(!MutObjAddString(outDoc.get(), root, "type", type) ||
       !MutObjAddInt(outDoc.get(), root, "version", version)){
        if(outError){
            *outError = "Failed to add standard JSON header fields.";
        }
        return false;
    }

    JsonMutVal* payload = yyjson_mut_obj_add_obj(outDoc.get(), root, "payload");
    if(!payload){
        if(outError){
            *outError = "Failed to create standard JSON payload object.";
        }
        return false;
    }

    outRefs.root = root;
    outRefs.payload = payload;
    return true;
}

bool ReadStandardDocumentHeader(const Document& doc, std::string& outType, int& outVersion, JsonVal** outPayload, std::string* outError){
    JsonVal* root = doc.root();
    if(!root || !yyjson_is_obj(root)){
        if(outError){
            *outError = "JSON root must be an object.";
        }
        return false;
    }

    if(!TryGetString(root, "type", outType)){
        if(outError){
            *outError = "JSON document missing string field 'type'.";
        }
        return false;
    }

    if(!TryGetInt(root, "version", outVersion)){
        if(outError){
            *outError = "JSON document missing integer field 'version'.";
        }
        return false;
    }

    JsonVal* payload = ObjGet(root, "payload");
    if(!payload){
        if(outError){
            *outError = "JSON document missing field 'payload'.";
        }
        return false;
    }

    if(outPayload){
        *outPayload = payload;
    }
    return true;
}

JsonVal* ObjGet(JsonVal* obj, const char* key){
    if(!obj || !yyjson_is_obj(obj) || !key){
        return nullptr;
    }
    return yyjson_obj_get(obj, key);
}

JsonVal* ObjGetObject(JsonVal* obj, const char* key){
    JsonVal* value = ObjGet(obj, key);
    return (value && yyjson_is_obj(value)) ? value : nullptr;
}

JsonVal* ObjGetArray(JsonVal* obj, const char* key){
    JsonVal* value = ObjGet(obj, key);
    return (value && yyjson_is_arr(value)) ? value : nullptr;
}

bool ObjHasKey(JsonVal* obj, const char* key){
    return ObjGet(obj, key) != nullptr;
}

bool TryGetString(JsonVal* obj, const char* key, std::string& outValue){
    JsonVal* value = ObjGet(obj, key);
    if(!value || !yyjson_is_str(value)){
        return false;
    }
    const char* str = yyjson_get_str(value);
    outValue = str ? str : "";
    return true;
}

bool TryGetBool(JsonVal* obj, const char* key, bool& outValue){
    JsonVal* value = ObjGet(obj, key);
    if(!value || !yyjson_is_bool(value)){
        return false;
    }
    outValue = yyjson_get_bool(value);
    return true;
}

bool TryGetInt(JsonVal* obj, const char* key, int& outValue){
    int64_t temp = 0;
    if(!TryGetInt64(obj, key, temp)){
        return false;
    }
    if(temp < std::numeric_limits<int>::min() || temp > std::numeric_limits<int>::max()){
        return false;
    }
    outValue = static_cast<int>(temp);
    return true;
}

bool TryGetInt64(JsonVal* obj, const char* key, int64_t& outValue){
    JsonVal* value = ObjGet(obj, key);
    if(!value){
        return false;
    }

    if(yyjson_is_sint(value) || yyjson_is_int(value)){
        outValue = yyjson_get_sint(value);
        return true;
    }

    if(yyjson_is_uint(value)){
        uint64_t raw = yyjson_get_uint(value);
        if(raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())){
            return false;
        }
        outValue = static_cast<int64_t>(raw);
        return true;
    }

    return false;
}

bool TryGetUInt64(JsonVal* obj, const char* key, uint64_t& outValue){
    JsonVal* value = ObjGet(obj, key);
    if(!value){
        return false;
    }

    if(yyjson_is_uint(value)){
        outValue = yyjson_get_uint(value);
        return true;
    }

    if(yyjson_is_sint(value) || yyjson_is_int(value)){
        int64_t raw = yyjson_get_sint(value);
        if(raw < 0){
            return false;
        }
        outValue = static_cast<uint64_t>(raw);
        return true;
    }

    return false;
}

bool TryGetFloat(JsonVal* obj, const char* key, float& outValue){
    return tryGetFloatValue(ObjGet(obj, key), outValue);
}

bool TryGetDouble(JsonVal* obj, const char* key, double& outValue){
    return tryGetNumberValue(ObjGet(obj, key), outValue);
}

std::string GetStringOrDefault(JsonVal* obj, const char* key, const std::string& fallback){
    std::string value;
    return TryGetString(obj, key, value) ? value : fallback;
}

bool GetBoolOrDefault(JsonVal* obj, const char* key, bool fallback){
    bool value = false;
    return TryGetBool(obj, key, value) ? value : fallback;
}

int GetIntOrDefault(JsonVal* obj, const char* key, int fallback){
    int value = 0;
    return TryGetInt(obj, key, value) ? value : fallback;
}

int64_t GetInt64OrDefault(JsonVal* obj, const char* key, int64_t fallback){
    int64_t value = 0;
    return TryGetInt64(obj, key, value) ? value : fallback;
}

uint64_t GetUInt64OrDefault(JsonVal* obj, const char* key, uint64_t fallback){
    uint64_t value = 0;
    return TryGetUInt64(obj, key, value) ? value : fallback;
}

float GetFloatOrDefault(JsonVal* obj, const char* key, float fallback){
    float value = 0.0f;
    return TryGetFloat(obj, key, value) ? value : fallback;
}

double GetDoubleOrDefault(JsonVal* obj, const char* key, double fallback){
    double value = 0.0;
    return TryGetDouble(obj, key, value) ? value : fallback;
}

bool TryReadVec2(JsonVal* value, Math3D::Vec2& outValue){
    float data[2] = {};
    if(tryReadVecNArray(value, data, 2)){
        outValue = Math3D::Vec2(data[0], data[1]);
        return true;
    }

    if(value && yyjson_is_obj(value)){
        float x = 0.0f;
        float y = 0.0f;
        if(TryGetFloat(value, "x", x) && TryGetFloat(value, "y", y)){
            outValue = Math3D::Vec2(x, y);
            return true;
        }
    }
    return false;
}

bool TryReadVec3(JsonVal* value, Math3D::Vec3& outValue){
    float data[3] = {};
    if(tryReadVecNArray(value, data, 3)){
        outValue = Math3D::Vec3(data[0], data[1], data[2]);
        return true;
    }

    if(value && yyjson_is_obj(value)){
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if(TryGetFloat(value, "x", x) && TryGetFloat(value, "y", y) && TryGetFloat(value, "z", z)){
            outValue = Math3D::Vec3(x, y, z);
            return true;
        }
    }
    return false;
}

bool TryReadVec4(JsonVal* value, Math3D::Vec4& outValue){
    float data[4] = {};
    if(tryReadVecNArray(value, data, 4)){
        outValue = Math3D::Vec4(data[0], data[1], data[2], data[3]);
        return true;
    }

    if(value && yyjson_is_obj(value)){
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
        if(TryGetFloat(value, "x", x) &&
           TryGetFloat(value, "y", y) &&
           TryGetFloat(value, "z", z) &&
           TryGetFloat(value, "w", w)){
            outValue = Math3D::Vec4(x, y, z, w);
            return true;
        }
    }
    return false;
}

bool TryGetVec2(JsonVal* obj, const char* key, Math3D::Vec2& outValue){
    return TryReadVec2(ObjGet(obj, key), outValue);
}

bool TryGetVec3(JsonVal* obj, const char* key, Math3D::Vec3& outValue){
    return TryReadVec3(ObjGet(obj, key), outValue);
}

bool TryGetVec4(JsonVal* obj, const char* key, Math3D::Vec4& outValue){
    return TryReadVec4(ObjGet(obj, key), outValue);
}

Math3D::Vec2 GetVec2OrDefault(JsonVal* obj, const char* key, const Math3D::Vec2& fallback){
    Math3D::Vec2 value;
    return TryGetVec2(obj, key, value) ? value : fallback;
}

Math3D::Vec3 GetVec3OrDefault(JsonVal* obj, const char* key, const Math3D::Vec3& fallback){
    Math3D::Vec3 value;
    return TryGetVec3(obj, key, value) ? value : fallback;
}

Math3D::Vec4 GetVec4OrDefault(JsonVal* obj, const char* key, const Math3D::Vec4& fallback){
    Math3D::Vec4 value;
    return TryGetVec4(obj, key, value) ? value : fallback;
}

bool MutObjAddString(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, const std::string& value){
    if(!doc || !obj || !key){
        return false;
    }
    return yyjson_mut_obj_add_strcpy(doc, obj, key, value.c_str());
}

bool MutObjAddBool(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, bool value){
    if(!doc || !obj || !key){
        return false;
    }
    return yyjson_mut_obj_add_bool(doc, obj, key, value);
}

bool MutObjAddInt(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, int value){
    if(!doc || !obj || !key){
        return false;
    }
    return yyjson_mut_obj_add_int(doc, obj, key, value);
}

bool MutObjAddInt64(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, int64_t value){
    if(!doc || !obj || !key){
        return false;
    }
    return yyjson_mut_obj_add_sint(doc, obj, key, value);
}

bool MutObjAddUInt64(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, uint64_t value){
    if(!doc || !obj || !key){
        return false;
    }
    return yyjson_mut_obj_add_uint(doc, obj, key, value);
}

bool MutObjAddFloat(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, float value){
    if(!doc || !obj || !key){
        return false;
    }
    return yyjson_mut_obj_add_real(doc, obj, key, static_cast<double>(value));
}

bool MutObjAddDouble(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, double value){
    if(!doc || !obj || !key){
        return false;
    }
    return yyjson_mut_obj_add_real(doc, obj, key, value);
}

bool MutObjAddVec2(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, const Math3D::Vec2& value){
    if(!doc || !obj || !key){
        return false;
    }
    JsonMutVal* arr = yyjson_mut_obj_add_arr(doc, obj, key);
    if(!arr){
        return false;
    }
    return yyjson_mut_arr_add_real(doc, arr, value.x) &&
           yyjson_mut_arr_add_real(doc, arr, value.y);
}

bool MutObjAddVec3(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, const Math3D::Vec3& value){
    if(!doc || !obj || !key){
        return false;
    }
    JsonMutVal* arr = yyjson_mut_obj_add_arr(doc, obj, key);
    if(!arr){
        return false;
    }
    return yyjson_mut_arr_add_real(doc, arr, value.x) &&
           yyjson_mut_arr_add_real(doc, arr, value.y) &&
           yyjson_mut_arr_add_real(doc, arr, value.z);
}

bool MutObjAddVec4(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, const Math3D::Vec4& value){
    if(!doc || !obj || !key){
        return false;
    }
    JsonMutVal* arr = yyjson_mut_obj_add_arr(doc, obj, key);
    if(!arr){
        return false;
    }
    return yyjson_mut_arr_add_real(doc, arr, value.x) &&
           yyjson_mut_arr_add_real(doc, arr, value.y) &&
           yyjson_mut_arr_add_real(doc, arr, value.z) &&
           yyjson_mut_arr_add_real(doc, arr, value.w);
}

} // namespace JsonUtils
