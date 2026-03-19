/**
 * @file src/Serialization/Json/JsonUtils.h
 * @brief Declarations for JsonUtils.
 */

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <cstdint>
#include <filesystem>
#include <string>

#include "Foundation/Math/Math3D.h"
#include "yyjson.h"

namespace JsonUtils {

using JsonVal = yyjson_val;
using JsonMutVal = yyjson_mut_val;

/// @brief Represents the Document type.
class Document {
    private:
        yyjson_doc* docPtr = nullptr;

    public:
        /**
         * @brief Constructs a new Document instance.
         */
        Document() = default;
        /**
         * @brief Constructs a new Document instance.
         * @param doc Value for doc.
          * @return Result of this operation.
         */
        explicit Document(yyjson_doc* doc);
        /**
         * @brief Destroys this Document instance.
         */
        ~Document();

        /**
         * @brief Constructs a new Document instance.
         */
        Document(const Document&) = delete;
        /**
         * @brief Assigns from another instance.
         */
        Document& operator=(const Document&) = delete;

        /**
         * @brief Constructs a new Document instance.
         * @param other Value for other.
         */
        Document(Document&& other) noexcept;
        /**
         * @brief Assigns from another instance.
         * @param other Value for other.
         */
        Document& operator=(Document&& other) noexcept;

        /**
         * @brief Resets this object state.
         * @param doc Value for doc.
         */
        void reset(yyjson_doc* doc = nullptr);
        /**
         * @brief Checks whether valid.
         * @return True when the operation succeeds; otherwise false.
         */
        bool valid() const { return docPtr != nullptr; }
        yyjson_doc* get() const { return docPtr; }
        JsonVal* root() const;
};

/// @brief Represents the MutableDocument type.
class MutableDocument {
    private:
        yyjson_mut_doc* docPtr = nullptr;

    public:
        /**
         * @brief Constructs a new MutableDocument instance.
         */
        MutableDocument() = default;
        /**
         * @brief Constructs a new MutableDocument instance.
         * @param doc Value for doc.
          * @return Result of this operation.
         */
        explicit MutableDocument(yyjson_mut_doc* doc);
        /**
         * @brief Destroys this MutableDocument instance.
         */
        ~MutableDocument();

        /**
         * @brief Constructs a new MutableDocument instance.
         */
        MutableDocument(const MutableDocument&) = delete;
        /**
         * @brief Assigns from another instance.
         */
        MutableDocument& operator=(const MutableDocument&) = delete;

        /**
         * @brief Constructs a new MutableDocument instance.
         * @param other Value for other.
         */
        MutableDocument(MutableDocument&& other) noexcept;
        /**
         * @brief Assigns from another instance.
         * @param other Value for other.
         */
        MutableDocument& operator=(MutableDocument&& other) noexcept;

        /**
         * @brief Resets this object state.
         * @param doc Value for doc.
         */
        void reset(yyjson_mut_doc* doc = nullptr);
        /**
         * @brief Creates a new object.
         * @return True when the operation succeeds; otherwise false.
         */
        bool create();
        /**
         * @brief Copies from.
         * @param doc Value for doc.
         * @return True when the operation succeeds; otherwise false.
         */
        bool copyFrom(const Document& doc);
        /**
         * @brief Copies from.
         * @param doc Value for doc.
         * @return True when the operation succeeds; otherwise false.
         */
        bool copyFrom(const MutableDocument& doc);

        /**
         * @brief Checks whether valid.
         * @return True when the operation succeeds; otherwise false.
         */
        bool valid() const { return docPtr != nullptr; }
        yyjson_mut_doc* get() const { return docPtr; }
        JsonMutVal* root() const;
        void setRoot(JsonMutVal* value);
        JsonMutVal* setRootObject();
        JsonMutVal* setRootArray();
};

/// @brief Holds data for StandardDocumentRefs.
struct StandardDocumentRefs {
    JsonMutVal* root = nullptr;
    JsonMutVal* payload = nullptr;
};

/**
 * @brief Loads document from text.
 * @param jsonText Value for json text.
 * @param outDoc Output value for doc.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool LoadDocumentFromText(const std::string& jsonText, Document& outDoc, std::string* outError = nullptr);
/**
 * @brief Loads document from absolute path.
 * @param path Filesystem path for path.
 * @param outDoc Output value for doc.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool LoadDocumentFromAbsolutePath(const std::filesystem::path& path, Document& outDoc, std::string* outError = nullptr);
/**
 * @brief Loads document from asset ref.
 * @param assetRef Reference to asset.
 * @param outDoc Output value for doc.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool LoadDocumentFromAssetRef(const std::string& assetRef, Document& outDoc, std::string* outError = nullptr);
/**
 * @brief Saves document to absolute path.
 * @param path Filesystem path for path.
 * @param doc Value for doc.
 * @param outError Output value for error.
 * @param pretty Value for pretty.
 * @return True when the operation succeeds; otherwise false.
 */
bool SaveDocumentToAbsolutePath(const std::filesystem::path& path, const MutableDocument& doc, std::string* outError = nullptr, bool pretty = true);
/**
 * @brief Saves document to asset ref.
 * @param assetRef Reference to asset.
 * @param doc Value for doc.
 * @param outError Output value for error.
 * @param pretty Value for pretty.
 * @return True when the operation succeeds; otherwise false.
 */
bool SaveDocumentToAssetRef(const std::string& assetRef, const MutableDocument& doc, std::string* outError = nullptr, bool pretty = true);
/**
 * @brief Writes document to string.
 * @param doc Value for doc.
 * @param outJson Buffer that receives json data.
 * @param outError Output value for error.
 * @param pretty Value for pretty.
 * @return True when the operation succeeds; otherwise false.
 */
bool WriteDocumentToString(const MutableDocument& doc, std::string& outJson, std::string* outError = nullptr, bool pretty = true);

/**
 * @brief Creates standard document.
 * @param outDoc Output value for doc.
 * @param type Mode or type selector.
 * @param version Value for version.
 * @param outRefs Output value for refs.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool CreateStandardDocument(MutableDocument& outDoc, const std::string& type, int version, StandardDocumentRefs& outRefs, std::string* outError = nullptr);
/**
 * @brief Reads standard document header.
 * @param doc Value for doc.
 * @param outType Mode or type selector.
 * @param outVersion Output value for version.
 * @param outPayload Output value for payload.
 * @param outError Output value for error.
 * @return True when the operation succeeds; otherwise false.
 */
bool ReadStandardDocumentHeader(const Document& doc, std::string& outType, int& outVersion, JsonVal** outPayload = nullptr, std::string* outError = nullptr);

/**
 * @brief Gets a value from a JSON object.
 * @param obj Value for obj.
 * @param key Value for key.
 * @return Pointer to the resulting object.
 */
JsonVal* ObjGet(JsonVal* obj, const char* key);
/**
 * @brief Gets an object child from a JSON object.
 * @param obj Value for obj.
 * @param key Value for key.
 * @return Pointer to the resulting object.
 */
JsonVal* ObjGetObject(JsonVal* obj, const char* key);
/**
 * @brief Gets an array child from a JSON object.
 * @param obj Value for obj.
 * @param key Value for key.
 * @return Pointer to the resulting object.
 */
JsonVal* ObjGetArray(JsonVal* obj, const char* key);
/**
 * @brief Checks whether obj has key.
 * @param obj Value for obj.
 * @param key Value for key.
 * @return True when the operation succeeds; otherwise false.
 */
bool ObjHasKey(JsonVal* obj, const char* key);

/**
 * @brief Checks whether try get string.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryGetString(JsonVal* obj, const char* key, std::string& outValue);
/**
 * @brief Checks whether try get bool.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryGetBool(JsonVal* obj, const char* key, bool& outValue);
/**
 * @brief Checks whether try get int.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryGetInt(JsonVal* obj, const char* key, int& outValue);
/**
 * @brief Checks whether try get int64.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryGetInt64(JsonVal* obj, const char* key, int64_t& outValue);
/**
 * @brief Checks whether try get u int64.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryGetUInt64(JsonVal* obj, const char* key, uint64_t& outValue);
/**
 * @brief Checks whether try get float.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryGetFloat(JsonVal* obj, const char* key, float& outValue);
/**
 * @brief Checks whether try get double.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryGetDouble(JsonVal* obj, const char* key, double& outValue);

/**
 * @brief Returns the string or default.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param fallback Value for fallback.
 * @return Resulting string value.
 */
std::string GetStringOrDefault(JsonVal* obj, const char* key, const std::string& fallback = std::string());
/**
 * @brief Returns the bool or default.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param fallback Value for fallback.
 * @return True when the operation succeeds; otherwise false.
 */
bool GetBoolOrDefault(JsonVal* obj, const char* key, bool fallback = false);
/**
 * @brief Returns the int or default.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param fallback Value for fallback.
 * @return Computed numeric result.
 */
int GetIntOrDefault(JsonVal* obj, const char* key, int fallback = 0);
/**
 * @brief Returns the int64 or default.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param fallback Value for fallback.
 * @return Result of this operation.
 */
int64_t GetInt64OrDefault(JsonVal* obj, const char* key, int64_t fallback = 0);
/**
 * @brief Returns the u int64 or default.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param fallback Value for fallback.
 * @return Result of this operation.
 */
uint64_t GetUInt64OrDefault(JsonVal* obj, const char* key, uint64_t fallback = 0);
/**
 * @brief Returns the float or default.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param fallback Value for fallback.
 * @return Computed numeric result.
 */
float GetFloatOrDefault(JsonVal* obj, const char* key, float fallback = 0.0f);
/**
 * @brief Returns the double or default.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param fallback Value for fallback.
 * @return Computed numeric result.
 */
double GetDoubleOrDefault(JsonVal* obj, const char* key, double fallback = 0.0);

/**
 * @brief Checks whether try read vec2.
 * @param value Value for value.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryReadVec2(JsonVal* value, Math3D::Vec2& outValue);
/**
 * @brief Checks whether try read vec3.
 * @param value Value for value.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryReadVec3(JsonVal* value, Math3D::Vec3& outValue);
/**
 * @brief Checks whether try read vec4.
 * @param value Value for value.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryReadVec4(JsonVal* value, Math3D::Vec4& outValue);

/**
 * @brief Checks whether try get vec2.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryGetVec2(JsonVal* obj, const char* key, Math3D::Vec2& outValue);
/**
 * @brief Checks whether try get vec3.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryGetVec3(JsonVal* obj, const char* key, Math3D::Vec3& outValue);
/**
 * @brief Checks whether try get vec4.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param outValue Output value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool TryGetVec4(JsonVal* obj, const char* key, Math3D::Vec4& outValue);

/**
 * @brief Returns the vec2 or default.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param fallback Value for fallback.
 * @return Result of this operation.
 */
Math3D::Vec2 GetVec2OrDefault(JsonVal* obj, const char* key, const Math3D::Vec2& fallback = Math3D::Vec2());
/**
 * @brief Returns the vec3 or default.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param fallback Value for fallback.
 * @return Result of this operation.
 */
Math3D::Vec3 GetVec3OrDefault(JsonVal* obj, const char* key, const Math3D::Vec3& fallback = Math3D::Vec3());
/**
 * @brief Returns the vec4 or default.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param fallback Value for fallback.
 * @return Result of this operation.
 */
Math3D::Vec4 GetVec4OrDefault(JsonVal* obj, const char* key, const Math3D::Vec4& fallback = Math3D::Vec4());

// Convenience writers for common schema fields and vector data.
bool MutObjAddString(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, const std::string& value);
/**
 * @brief Checks whether mut obj add bool.
 * @param doc Value for doc.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param value Value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool MutObjAddBool(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, bool value);
/**
 * @brief Checks whether mut obj add int.
 * @param doc Value for doc.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param value Value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool MutObjAddInt(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, int value);
/**
 * @brief Checks whether mut obj add int64.
 * @param doc Value for doc.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param value Value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool MutObjAddInt64(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, int64_t value);
/**
 * @brief Checks whether mut obj add u int64.
 * @param doc Value for doc.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param value Value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool MutObjAddUInt64(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, uint64_t value);
/**
 * @brief Checks whether mut obj add float.
 * @param doc Value for doc.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param value Value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool MutObjAddFloat(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, float value);
/**
 * @brief Checks whether mut obj add double.
 * @param doc Value for doc.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param value Value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool MutObjAddDouble(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, double value);
/**
 * @brief Checks whether mut obj add vec2.
 * @param doc Value for doc.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param value Value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool MutObjAddVec2(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, const Math3D::Vec2& value);
/**
 * @brief Checks whether mut obj add vec3.
 * @param doc Value for doc.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param value Value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool MutObjAddVec3(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, const Math3D::Vec3& value);
/**
 * @brief Checks whether mut obj add vec4.
 * @param doc Value for doc.
 * @param obj Value for obj.
 * @param key Value for key.
 * @param value Value for value.
 * @return True when the operation succeeds; otherwise false.
 */
bool MutObjAddVec4(yyjson_mut_doc* doc, JsonMutVal* obj, const char* key, const Math3D::Vec4& value);

} // namespace JsonUtils

#endif // JSON_UTILS_H
