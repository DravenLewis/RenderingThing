#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <cstdint>
#include <filesystem>
#include <string>

#include "Foundation/Math/Math3D.h"
#include "yyjson.h"

namespace JsonUtils {

class Document {
    private:
        yyjson_doc* docPtr = nullptr;

    public:
        Document() = default;
        explicit Document(yyjson_doc* doc);
        ~Document();

        Document(const Document&) = delete;
        Document& operator=(const Document&) = delete;

        Document(Document&& other) noexcept;
        Document& operator=(Document&& other) noexcept;

        void reset(yyjson_doc* doc = nullptr);
        bool valid() const { return docPtr != nullptr; }
        yyjson_doc* get() const { return docPtr; }
        yyjson_val* root() const;
};

class MutableDocument {
    private:
        yyjson_mut_doc* docPtr = nullptr;

    public:
        MutableDocument() = default;
        explicit MutableDocument(yyjson_mut_doc* doc);
        ~MutableDocument();

        MutableDocument(const MutableDocument&) = delete;
        MutableDocument& operator=(const MutableDocument&) = delete;

        MutableDocument(MutableDocument&& other) noexcept;
        MutableDocument& operator=(MutableDocument&& other) noexcept;

        void reset(yyjson_mut_doc* doc = nullptr);
        bool create();
        bool copyFrom(const Document& doc);
        bool copyFrom(const MutableDocument& doc);

        bool valid() const { return docPtr != nullptr; }
        yyjson_mut_doc* get() const { return docPtr; }
        yyjson_mut_val* root() const;
        void setRoot(yyjson_mut_val* value);
        yyjson_mut_val* setRootObject();
        yyjson_mut_val* setRootArray();
};

struct StandardDocumentRefs {
    yyjson_mut_val* root = nullptr;
    yyjson_mut_val* payload = nullptr;
};

bool LoadDocumentFromText(const std::string& jsonText, Document& outDoc, std::string* outError = nullptr);
bool LoadDocumentFromAbsolutePath(const std::filesystem::path& path, Document& outDoc, std::string* outError = nullptr);
bool LoadDocumentFromAssetRef(const std::string& assetRef, Document& outDoc, std::string* outError = nullptr);
bool SaveDocumentToAbsolutePath(const std::filesystem::path& path, const MutableDocument& doc, std::string* outError = nullptr, bool pretty = true);
bool SaveDocumentToAssetRef(const std::string& assetRef, const MutableDocument& doc, std::string* outError = nullptr, bool pretty = true);
bool WriteDocumentToString(const MutableDocument& doc, std::string& outJson, std::string* outError = nullptr, bool pretty = true);

bool CreateStandardDocument(MutableDocument& outDoc, const std::string& type, int version, StandardDocumentRefs& outRefs, std::string* outError = nullptr);
bool ReadStandardDocumentHeader(const Document& doc, std::string& outType, int& outVersion, yyjson_val** outPayload = nullptr, std::string* outError = nullptr);

yyjson_val* ObjGet(yyjson_val* obj, const char* key);
yyjson_val* ObjGetObject(yyjson_val* obj, const char* key);
yyjson_val* ObjGetArray(yyjson_val* obj, const char* key);
bool ObjHasKey(yyjson_val* obj, const char* key);

bool TryGetString(yyjson_val* obj, const char* key, std::string& outValue);
bool TryGetBool(yyjson_val* obj, const char* key, bool& outValue);
bool TryGetInt(yyjson_val* obj, const char* key, int& outValue);
bool TryGetInt64(yyjson_val* obj, const char* key, int64_t& outValue);
bool TryGetUInt64(yyjson_val* obj, const char* key, uint64_t& outValue);
bool TryGetFloat(yyjson_val* obj, const char* key, float& outValue);
bool TryGetDouble(yyjson_val* obj, const char* key, double& outValue);

std::string GetStringOrDefault(yyjson_val* obj, const char* key, const std::string& fallback = std::string());
bool GetBoolOrDefault(yyjson_val* obj, const char* key, bool fallback = false);
int GetIntOrDefault(yyjson_val* obj, const char* key, int fallback = 0);
int64_t GetInt64OrDefault(yyjson_val* obj, const char* key, int64_t fallback = 0);
uint64_t GetUInt64OrDefault(yyjson_val* obj, const char* key, uint64_t fallback = 0);
float GetFloatOrDefault(yyjson_val* obj, const char* key, float fallback = 0.0f);
double GetDoubleOrDefault(yyjson_val* obj, const char* key, double fallback = 0.0);

bool TryReadVec2(yyjson_val* value, Math3D::Vec2& outValue);
bool TryReadVec3(yyjson_val* value, Math3D::Vec3& outValue);
bool TryReadVec4(yyjson_val* value, Math3D::Vec4& outValue);

bool TryGetVec2(yyjson_val* obj, const char* key, Math3D::Vec2& outValue);
bool TryGetVec3(yyjson_val* obj, const char* key, Math3D::Vec3& outValue);
bool TryGetVec4(yyjson_val* obj, const char* key, Math3D::Vec4& outValue);

Math3D::Vec2 GetVec2OrDefault(yyjson_val* obj, const char* key, const Math3D::Vec2& fallback = Math3D::Vec2());
Math3D::Vec3 GetVec3OrDefault(yyjson_val* obj, const char* key, const Math3D::Vec3& fallback = Math3D::Vec3());
Math3D::Vec4 GetVec4OrDefault(yyjson_val* obj, const char* key, const Math3D::Vec4& fallback = Math3D::Vec4());

// Convenience writers for common schema fields and vector data.
bool MutObjAddString(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const std::string& value);
bool MutObjAddBool(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, bool value);
bool MutObjAddInt(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, int value);
bool MutObjAddInt64(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, int64_t value);
bool MutObjAddUInt64(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, uint64_t value);
bool MutObjAddFloat(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, float value);
bool MutObjAddDouble(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, double value);
bool MutObjAddVec2(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const Math3D::Vec2& value);
bool MutObjAddVec3(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const Math3D::Vec3& value);
bool MutObjAddVec4(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const Math3D::Vec4& value);

} // namespace JsonUtils

#endif // JSON_UTILS_H
