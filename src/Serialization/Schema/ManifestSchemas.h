/**
 * @file src/Serialization/Schema/ManifestSchemas.h
 * @brief Declarations for ManifestSchemas.
 */

#ifndef SERIALIZATION_SCHEMA_MANIFEST_SCHEMAS_H
#define SERIALIZATION_SCHEMA_MANIFEST_SCHEMAS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Serialization/Schema/ISchema.h"

namespace JsonSchema {

/// @brief Represents the ManifestSchemaBase type.
class ManifestSchemaBase : public ISchema {
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
         * @brief Resets manifest state.
         */
        virtual void ResetManifestState() = 0;
        /**
         * @brief Deserializes manifest payload.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        virtual bool DeserializeManifestPayload(JsonUtils::JsonVal* payload, int version, std::string* outError) = 0;
        /**
         * @brief Serializes manifest payload.
         * @param doc Value for doc.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        virtual bool SerializeManifestPayload(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, int version, std::string* outError) const = 0;

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
         * @brief Checks whether require bool field.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param outValue Output value for value.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool RequireBoolField(JsonUtils::JsonVal* obj, const char* key, bool& outValue, std::string* outError) const;
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
         * @brief Reads string array.
         * @param arrValue Value for arr value.
         * @param outValues Output value for values.
         * @param outError Output value for error.
         * @param debugName Name used for debug name.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ReadStringArray(JsonUtils::JsonVal* arrValue, std::vector<std::string>& outValues, std::string* outError, const char* debugName = nullptr) const;
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
         * @brief Writes string map field.
         * @param doc Value for doc.
         * @param obj Value for obj.
         * @param key Value for key.
         * @param values Value for values.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool WriteStringMapField(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* obj, const char* key, const std::map<std::string, std::string>& values, std::string* outError) const;
};

/// @brief Represents the AssetManifestSchema type.
class AssetManifestSchema : public ManifestSchemaBase {
    public:
        /// @brief Holds data for Entry.
        struct Entry {
            std::string path;
            std::string kind = "file"; // "file" or "directory"
            std::string alias;
            std::string sourceRef;
            std::uint64_t size = 0;
            std::string hash;
            std::string compression;
            bool readOnly = false;
            std::vector<std::string> tags;
        };

        std::string bundleAlias;
        std::string rootEntry;
        std::vector<Entry> entries;

        /**
         * @brief Constructs a new AssetManifestSchema instance.
         */
        AssetManifestSchema();

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
         * @brief Checks whether on before save.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool OnBeforeSave(int version, std::string* outError) const override;

        /**
         * @brief Resets manifest state.
         */
        void ResetManifestState() override;
        /**
         * @brief Deserializes manifest payload.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool DeserializeManifestPayload(JsonUtils::JsonVal* payload, int version, std::string* outError) override;
        /**
         * @brief Serializes manifest payload.
         * @param doc Value for doc.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool SerializeManifestPayload(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, int version, std::string* outError) const override;

    private:
        /**
         * @brief Checks whether validate state.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ValidateState(std::string* outError) const;
};

/// @brief Represents the GameManifestSchema type.
class GameManifestSchema : public ManifestSchemaBase {
    public:
        /// @brief Holds data for GameInfo.
        struct GameInfo {
            std::string name;
            std::string version;
        };

        /// @brief Holds data for WindowConfig.
        struct WindowConfig {
            std::string title;
            int width = 1280;
            int height = 720;
            bool resizable = true;
        };

        /// @brief Holds data for RenderConfig.
        struct RenderConfig {
            std::string defaultPipeline = "forward";
        };

        /// @brief Holds data for AssetSource.
        struct AssetSource {
            std::string kind = "folder"; // "folder" or "bundle"
            std::string ref;
            std::string alias;
            bool enabled = true;
        };

        /// @brief Holds data for SceneEntry.
        struct SceneEntry {
            std::string id;
            std::string ref;
            std::string name;
        };

        GameInfo game;
        WindowConfig window;
        RenderConfig render;
        std::vector<AssetSource> assetSources;
        std::vector<SceneEntry> scenes;
        std::string startupScene;

        /**
         * @brief Constructs a new GameManifestSchema instance.
         */
        GameManifestSchema();

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
         * @brief Checks whether on before save.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool OnBeforeSave(int version, std::string* outError) const override;

        /**
         * @brief Resets manifest state.
         */
        void ResetManifestState() override;
        /**
         * @brief Deserializes manifest payload.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool DeserializeManifestPayload(JsonUtils::JsonVal* payload, int version, std::string* outError) override;
        /**
         * @brief Serializes manifest payload.
         * @param doc Value for doc.
         * @param payload Value for payload.
         * @param version Value for version.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool SerializeManifestPayload(yyjson_mut_doc* doc, JsonUtils::JsonMutVal* payload, int version, std::string* outError) const override;

    private:
        /**
         * @brief Checks whether validate state.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool ValidateState(std::string* outError) const;
};

} // namespace JsonSchema

#endif // SERIALIZATION_SCHEMA_MANIFEST_SCHEMAS_H
