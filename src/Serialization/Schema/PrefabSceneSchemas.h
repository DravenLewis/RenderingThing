/**
 * @file src/Serialization/Schema/PrefabSceneSchemas.h
 * @brief Declarations for PrefabSceneSchemas.
 */

#ifndef SERIALIZATION_SCHEMA_PREFAB_SCENE_SCHEMAS_H
#define SERIALIZATION_SCHEMA_PREFAB_SCENE_SCHEMAS_H

#include <cstdint>
#include <string>
#include <vector>

#include "Serialization/Schema/ISchema.h"

namespace JsonSchema {

/// @brief Holds data for RawJsonValue.
struct RawJsonValue {
    bool hasValue = false;
    std::string json;

    /**
     * @brief Clears the current state.
     */
    void Clear(){
        hasValue = false;
        json.clear();
    }
};

/// @brief Holds data for DocumentMetadata.
struct DocumentMetadata {
    std::string name;
    std::string description;
    std::string sourceAssetRef;
    std::string createdUtc;
    std::string modifiedUtc;
    std::vector<std::string> tags;

    /**
     * @brief Clears the current state.
     */
    void Clear(){
        name.clear();
        description.clear();
        sourceAssetRef.clear();
        createdUtc.clear();
        modifiedUtc.clear();
        tags.clear();
    }
};

/// @brief Represents the EntitySnapshotSchemaBase type.
class EntitySnapshotSchemaBase : public ISchema {
    public:
        /// @brief Holds data for ComponentRecord.
        struct ComponentRecord {
            std::string type;
            int version = 1;
            std::string payloadJson = "{}"; // Raw JSON payload serialized as compact text.
        };

        /// @brief Holds data for EntityRecord.
        struct EntityRecord {
            std::uint64_t id = 0;
            std::string name;
            bool enabled = true;
            bool hasParentId = false;
            std::uint64_t parentId = 0;
            std::vector<std::string> tags;
            std::vector<ComponentRecord> components;
        };

        std::vector<std::uint64_t> rootEntityIds;
        std::vector<EntityRecord> entities;

        /**
         * @brief Clears snapshot.
         */
        void ClearSnapshot();

    protected:
        /**
         * @brief Deserializes payload.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool DeserializePayload(JsonUtils::JsonVal* payload, int version, std::string* outError) override final;
        /**
         * @brief Serializes payload.
         * @param doc Value for doc.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool SerializePayload(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, int version, std::string* outError) const override final;

        /**
         * @brief Resets document fields state.
         */
        virtual void ResetDocumentFieldsState() = 0;
        /**
         * @brief Deserializes document fields.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        virtual bool DeserializeDocumentFields(JsonUtils::JsonVal* payload, int version, std::string* outError) = 0;
        /**
         * @brief Serializes document fields.
         * @param doc Value for doc.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        virtual bool SerializeDocumentFields(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, int version, std::string* outError) const = 0;
        /**
         * @brief Checks whether validate document state.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        virtual bool ValidateDocumentState(std::string* outError) const;

        /**
         * @brief Checks whether require string field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool RequireStringField(JsonUtils::JsonVal* obj, const char* key, std::string& outValue, std::string* outError) const;
        /**
         * @brief Checks whether require int field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool RequireIntField(JsonUtils::JsonVal* obj, const char* key, int& outValue, std::string* outError) const;
        /**
         * @brief Checks whether require u int64 field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool RequireUInt64Field(JsonUtils::JsonVal* obj, const char* key, std::uint64_t& outValue, std::string* outError) const;
        /**
         * @brief Checks whether require object field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool RequireObjectField(JsonUtils::JsonVal* obj, const char* key, JsonUtils::JsonVal*& outValue, std::string* outError) const;
        /**
         * @brief Checks whether require array field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool RequireArrayField(JsonUtils::JsonVal* obj, const char* key, JsonUtils::JsonVal*& outValue, std::string* outError) const;

        /**
         * @brief Reads optional string field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadOptionalStringField(JsonUtils::JsonVal* obj, const char* key, std::string& outValue, std::string* outError) const;
        /**
         * @brief Reads optional bool field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadOptionalBoolField(JsonUtils::JsonVal* obj, const char* key, bool& outValue, std::string* outError) const;
        /**
         * @brief Reads optional int field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadOptionalIntField(JsonUtils::JsonVal* obj, const char* key, int& outValue, std::string* outError) const;
        /**
         * @brief Reads optional u int64 field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadOptionalUInt64Field(JsonUtils::JsonVal* obj, const char* key, std::uint64_t& outValue, std::string* outError) const;

        /**
         * @brief Reads string array field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValues Output value for values.
         * @param outError Output value for error.
         * @param required Flag controlling required.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadStringArrayField(JsonUtils::JsonVal* obj, const char* key, std::vector<std::string>& outValues, std::string* outError, bool required = false) const;
        /**
         * @brief Writes string array field.
         * @param doc Value for doc.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param values Value for values.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool WriteStringArrayField(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* obj, const char* key, const std::vector<std::string>& values, std::string* outError) const;

        /**
         * @brief Reads optional raw json field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @param requireObject Value for require object.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadOptionalRawJsonField(JsonUtils::JsonVal* obj, const char* key, RawJsonValue& outValue, std::string* outError, bool requireObject = false) const;
        /**
         * @brief Writes optional raw json field.
         * @param doc Value for doc.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param value Value for value.
         * @param outError Output value for error.
         * @param requireObject Value for require object.
         * @return True when the operation succeeds; otherwise false.
         */
        bool WriteOptionalRawJsonField(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* obj, const char* key, const RawJsonValue& value, std::string* outError, bool requireObject = false) const;

    private:
        /**
         * @brief Deserializes snapshot fields.
         * @param payload Value for payload.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool DeserializeSnapshotFields(JsonUtils::JsonVal* payload, std::string* outError);
        /**
         * @brief Serializes snapshot fields.
         * @param doc Value for doc.
         * @param payload Value for payload.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool SerializeSnapshotFields(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, std::string* outError) const;
        /**
         * @brief Checks whether validate snapshot state.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ValidateSnapshotState(std::string* outError) const;
        /**
         * @brief Derives root entity ids if missing.
         */
        void DeriveRootEntityIdsIfMissing();
        /**
         * @brief Collects derived root entity ids.
         * @param outRoots Output value for roots.
         */
        void CollectDerivedRootEntityIds(std::vector<std::uint64_t>& outRoots) const;

        /**
         * @brief Reads entity record.
         * @param obj Value for obj.
         * @param outEntity Output value for entity.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadEntityRecord(JsonUtils::JsonVal* obj, EntityRecord& outEntity, std::string* outError) const;
        /**
         * @brief Writes entity record.
         * @param doc Value for doc.
         * @param arr Value for arr.
         * @param entity Value for entity.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool WriteEntityRecord(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* arr, const EntityRecord& entity, std::string* outError) const;
        /**
         * @brief Reads component record.
         * @param obj Value for obj.
         * @param outComponent Output value for component.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadComponentRecord(JsonUtils::JsonVal* obj, ComponentRecord& outComponent, std::string* outError) const;
        /**
         * @brief Writes component record.
         * @param doc Value for doc.
         * @param arr Value for arr.
         * @param component Value for component.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool WriteComponentRecord(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* arr, const ComponentRecord& component, std::string* outError) const;

        /**
         * @brief Reads string array.
         * @param arr Value for arr.
         * @param outValues Output value for values.
         * @param outError Output value for error.
         * @param debugName Name used for debug name.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadStringArray(JsonUtils::JsonVal* arr, std::vector<std::string>& outValues, std::string* outError, const char* debugName) const;
        /**
         * @brief Reads u int64 array.
         * @param arr Value for arr.
         * @param outValues Output value for values.
         * @param outError Output value for error.
         * @param debugName Name used for debug name.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadUInt64Array(JsonUtils::JsonVal* arr, std::vector<std::uint64_t>& outValues, std::string* outError, const char* debugName) const;
        /**
         * @brief Writes u int64 array field.
         * @param doc Value for doc.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param values Value for values.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool WriteUInt64ArrayField(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* obj, const char* key, const std::vector<std::uint64_t>& values, std::string* outError) const;

        /**
         * @brief Checks whether capture json value.
         * @param value Value for value.
         * @param outJson Buffer that receives json data.
         * @param outError Output value for error.
         * @param debugName Name used for debug name.
         * @return True when the operation succeeds; otherwise false.
         */
        bool CaptureJsonValue(JsonUtils::JsonVal* value, std::string& outJson, std::string* outError, const char* debugName) const;
        /**
         * @brief Copies raw json to mutable value.
         * @param doc Value for doc.
         * @param jsonText Value for json text.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @param debugName Name used for debug name.
         * @param requireObject Value for require object.
         * @return True when the operation succeeds; otherwise false.
         */
        bool CopyRawJsonToMutableValue(yyjson_mut_doc* doc, const std::string& jsonText, JsonUtils::JsonMutVal*& outValue, std::string* outError, const char* debugName, bool requireObject) const;
};

/// @brief Represents the PrefabSchema type.
class PrefabSchema : public EntitySnapshotSchemaBase {
    public:
        DocumentMetadata metadata;
        std::vector<std::string> dependencies;
        /**
         * @brief Returns the JSON representation.
         * @return Result of this operation.
         */
        RawJsonValue prefabSettings; // Expected object JSON (optional; defaults to {}).
        RawJsonValue variant;        // Expected object JSON (optional).

        PrefabSchema();

        const char* SchemaType() const override;
        int CurrentVersion() const override;

        void Clear();

    protected:
        void ResetDocumentFieldsState() override;
        bool DeserializeDocumentFields(JsonUtils::JsonVal* payload, int version, std::string* outError) override;
        bool SerializeDocumentFields(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, int version, std::string* outError) const override;
        bool ValidateDocumentState(std::string* outError) const override;
};

/// @brief Represents the SceneSchema type.
class SceneSchema : public EntitySnapshotSchemaBase {
    public:
        DocumentMetadata metadata;
        std::vector<std::string> dependencies;
        RawJsonValue sceneSettings; // Expected object JSON.
        /**
         * @brief Returns the JSON representation.
         * @return Result of this operation.
         */
        RawJsonValue editorState;   // Expected object JSON (optional).

        /**
         * @brief Constructs a new SceneSchema instance.
         */
        SceneSchema();

        /**
         * @brief Returns the schema type identifier.
         * @return Pointer to the resulting object.
         */
        const char* SchemaType() const override;
        /**
         * @brief Returns the current schema version.
         * @return Computed numeric result.
         */
        int CurrentVersion() const override;

        /**
         * @brief Clears the current state.
         */
        void Clear();

    protected:
        /**
         * @brief Resets document fields state.
         */
        void ResetDocumentFieldsState() override;
        /**
         * @brief Deserializes document fields.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool DeserializeDocumentFields(JsonUtils::JsonVal* payload, int version, std::string* outError) override;
        /**
         * @brief Serializes document fields.
         * @param doc Value for doc.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool SerializeDocumentFields(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, int version, std::string* outError) const override;
        /**
         * @brief Checks whether validate document state.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ValidateDocumentState(std::string* outError) const override;
};

} // namespace JsonSchema

#endif // SERIALIZATION_SCHEMA_PREFAB_SCENE_SCHEMAS_H
