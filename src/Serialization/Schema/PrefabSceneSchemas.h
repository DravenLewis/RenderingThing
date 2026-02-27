#ifndef SERIALIZATION_SCHEMA_PREFAB_SCENE_SCHEMAS_H
#define SERIALIZATION_SCHEMA_PREFAB_SCENE_SCHEMAS_H

#include <cstdint>
#include <string>
#include <vector>

#include "Serialization/Schema/ISchema.h"

namespace JsonSchema {

struct RawJsonValue {
    bool hasValue = false;
    std::string json;

    void Clear(){
        hasValue = false;
        json.clear();
    }
};

struct DocumentMetadata {
    std::string name;
    std::string description;
    std::string sourceAssetRef;
    std::string createdUtc;
    std::string modifiedUtc;
    std::vector<std::string> tags;

    void Clear(){
        name.clear();
        description.clear();
        sourceAssetRef.clear();
        createdUtc.clear();
        modifiedUtc.clear();
        tags.clear();
    }
};

class EntitySnapshotSchemaBase : public ISchema {
    public:
        struct ComponentRecord {
            std::string type;
            int version = 1;
            std::string payloadJson = "{}"; // Raw JSON payload serialized as compact text.
        };

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

        void ClearSnapshot();

    protected:
        bool DeserializePayload(yyjson_val* payload, int version, std::string* outError) override final;
        bool SerializePayload(yyjson_mut_doc* doc, yyjson_mut_val* payload, int version, std::string* outError) const override final;

        virtual void ResetDocumentFieldsState() = 0;
        virtual bool DeserializeDocumentFields(yyjson_val* payload, int version, std::string* outError) = 0;
        virtual bool SerializeDocumentFields(yyjson_mut_doc* doc, yyjson_mut_val* payload, int version, std::string* outError) const = 0;
        virtual bool ValidateDocumentState(std::string* outError) const;

        bool RequireStringField(yyjson_val* obj, const char* key, std::string& outValue, std::string* outError) const;
        bool RequireIntField(yyjson_val* obj, const char* key, int& outValue, std::string* outError) const;
        bool RequireUInt64Field(yyjson_val* obj, const char* key, std::uint64_t& outValue, std::string* outError) const;
        bool RequireObjectField(yyjson_val* obj, const char* key, yyjson_val*& outValue, std::string* outError) const;
        bool RequireArrayField(yyjson_val* obj, const char* key, yyjson_val*& outValue, std::string* outError) const;

        bool ReadOptionalStringField(yyjson_val* obj, const char* key, std::string& outValue, std::string* outError) const;
        bool ReadOptionalBoolField(yyjson_val* obj, const char* key, bool& outValue, std::string* outError) const;
        bool ReadOptionalIntField(yyjson_val* obj, const char* key, int& outValue, std::string* outError) const;
        bool ReadOptionalUInt64Field(yyjson_val* obj, const char* key, std::uint64_t& outValue, std::string* outError) const;

        bool ReadStringArrayField(yyjson_val* obj, const char* key, std::vector<std::string>& outValues, std::string* outError, bool required = false) const;
        bool WriteStringArrayField(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const std::vector<std::string>& values, std::string* outError) const;

        bool ReadOptionalRawJsonField(yyjson_val* obj, const char* key, RawJsonValue& outValue, std::string* outError, bool requireObject = false) const;
        bool WriteOptionalRawJsonField(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const RawJsonValue& value, std::string* outError, bool requireObject = false) const;

    private:
        bool DeserializeSnapshotFields(yyjson_val* payload, std::string* outError);
        bool SerializeSnapshotFields(yyjson_mut_doc* doc, yyjson_mut_val* payload, std::string* outError) const;
        bool ValidateSnapshotState(std::string* outError) const;
        void DeriveRootEntityIdsIfMissing();
        void CollectDerivedRootEntityIds(std::vector<std::uint64_t>& outRoots) const;

        bool ReadEntityRecord(yyjson_val* obj, EntityRecord& outEntity, std::string* outError) const;
        bool WriteEntityRecord(yyjson_mut_doc* doc, yyjson_mut_val* arr, const EntityRecord& entity, std::string* outError) const;
        bool ReadComponentRecord(yyjson_val* obj, ComponentRecord& outComponent, std::string* outError) const;
        bool WriteComponentRecord(yyjson_mut_doc* doc, yyjson_mut_val* arr, const ComponentRecord& component, std::string* outError) const;

        bool ReadStringArray(yyjson_val* arr, std::vector<std::string>& outValues, std::string* outError, const char* debugName) const;
        bool ReadUInt64Array(yyjson_val* arr, std::vector<std::uint64_t>& outValues, std::string* outError, const char* debugName) const;
        bool WriteUInt64ArrayField(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const std::vector<std::uint64_t>& values, std::string* outError) const;

        bool CaptureJsonValue(yyjson_val* value, std::string& outJson, std::string* outError, const char* debugName) const;
        bool CopyRawJsonToMutableValue(yyjson_mut_doc* doc, const std::string& jsonText, yyjson_mut_val*& outValue, std::string* outError, const char* debugName, bool requireObject) const;
};

class PrefabSchema : public EntitySnapshotSchemaBase {
    public:
        DocumentMetadata metadata;
        std::vector<std::string> dependencies;
        RawJsonValue prefabSettings; // Expected object JSON (optional; defaults to {}).
        RawJsonValue variant;        // Expected object JSON (optional).

        PrefabSchema();

        const char* SchemaType() const override;
        int CurrentVersion() const override;

        void Clear();

    protected:
        void ResetDocumentFieldsState() override;
        bool DeserializeDocumentFields(yyjson_val* payload, int version, std::string* outError) override;
        bool SerializeDocumentFields(yyjson_mut_doc* doc, yyjson_mut_val* payload, int version, std::string* outError) const override;
        bool ValidateDocumentState(std::string* outError) const override;
};

class SceneSchema : public EntitySnapshotSchemaBase {
    public:
        DocumentMetadata metadata;
        std::vector<std::string> dependencies;
        RawJsonValue sceneSettings; // Expected object JSON.
        RawJsonValue editorState;   // Expected object JSON (optional).

        SceneSchema();

        const char* SchemaType() const override;
        int CurrentVersion() const override;

        void Clear();

    protected:
        void ResetDocumentFieldsState() override;
        bool DeserializeDocumentFields(yyjson_val* payload, int version, std::string* outError) override;
        bool SerializeDocumentFields(yyjson_mut_doc* doc, yyjson_mut_val* payload, int version, std::string* outError) const override;
        bool ValidateDocumentState(std::string* outError) const override;
};

} // namespace JsonSchema

#endif // SERIALIZATION_SCHEMA_PREFAB_SCENE_SCHEMAS_H
