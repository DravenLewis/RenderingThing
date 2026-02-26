#ifndef SERIALIZATION_SCHEMA_MANIFEST_SCHEMAS_H
#define SERIALIZATION_SCHEMA_MANIFEST_SCHEMAS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Serialization/Schema/ISchema.h"

namespace JsonSchema {

class ManifestSchemaBase : public ISchema {
    protected:
        bool DeserializePayload(yyjson_val* payload, int version, std::string* outError) override final;
        bool SerializePayload(yyjson_mut_doc* doc, yyjson_mut_val* payload, int version, std::string* outError) const override final;

        virtual void ResetManifestState() = 0;
        virtual bool DeserializeManifestPayload(yyjson_val* payload, int version, std::string* outError) = 0;
        virtual bool SerializeManifestPayload(yyjson_mut_doc* doc, yyjson_mut_val* payload, int version, std::string* outError) const = 0;

        bool RequireStringField(yyjson_val* obj, const char* key, std::string& outValue, std::string* outError) const;
        bool RequireBoolField(yyjson_val* obj, const char* key, bool& outValue, std::string* outError) const;
        bool RequireIntField(yyjson_val* obj, const char* key, int& outValue, std::string* outError) const;
        bool RequireUInt64Field(yyjson_val* obj, const char* key, std::uint64_t& outValue, std::string* outError) const;
        bool RequireObjectField(yyjson_val* obj, const char* key, yyjson_val*& outValue, std::string* outError) const;
        bool RequireArrayField(yyjson_val* obj, const char* key, yyjson_val*& outValue, std::string* outError) const;

        bool ReadOptionalStringField(yyjson_val* obj, const char* key, std::string& outValue, std::string* outError) const;
        bool ReadOptionalBoolField(yyjson_val* obj, const char* key, bool& outValue, std::string* outError) const;
        bool ReadOptionalIntField(yyjson_val* obj, const char* key, int& outValue, std::string* outError) const;
        bool ReadOptionalUInt64Field(yyjson_val* obj, const char* key, std::uint64_t& outValue, std::string* outError) const;

        bool ReadStringArray(yyjson_val* arrValue, std::vector<std::string>& outValues, std::string* outError, const char* debugName = nullptr) const;
        bool ReadStringArrayField(yyjson_val* obj, const char* key, std::vector<std::string>& outValues, std::string* outError, bool required = false) const;

        bool WriteStringArrayField(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const std::vector<std::string>& values, std::string* outError) const;
        bool WriteStringMapField(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, const std::map<std::string, std::string>& values, std::string* outError) const;
};

class AssetManifestSchema : public ManifestSchemaBase {
    public:
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

        AssetManifestSchema();

        const char* SchemaType() const override;
        int CurrentVersion() const override;

        void Clear();

    protected:
        bool OnBeforeSave(int version, std::string* outError) const override;

        void ResetManifestState() override;
        bool DeserializeManifestPayload(yyjson_val* payload, int version, std::string* outError) override;
        bool SerializeManifestPayload(yyjson_mut_doc* doc, yyjson_mut_val* payload, int version, std::string* outError) const override;

    private:
        bool ValidateState(std::string* outError) const;
};

class GameManifestSchema : public ManifestSchemaBase {
    public:
        struct GameInfo {
            std::string name;
            std::string version;
        };

        struct WindowConfig {
            std::string title;
            int width = 1280;
            int height = 720;
            bool resizable = true;
        };

        struct RenderConfig {
            std::string defaultPipeline = "forward";
        };

        struct AssetSource {
            std::string kind = "folder"; // "folder" or "bundle"
            std::string ref;
            std::string alias;
            bool enabled = true;
        };

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

        GameManifestSchema();

        const char* SchemaType() const override;
        int CurrentVersion() const override;

        void Clear();

    protected:
        bool OnBeforeSave(int version, std::string* outError) const override;

        void ResetManifestState() override;
        bool DeserializeManifestPayload(yyjson_val* payload, int version, std::string* outError) override;
        bool SerializeManifestPayload(yyjson_mut_doc* doc, yyjson_mut_val* payload, int version, std::string* outError) const override;

    private:
        bool ValidateState(std::string* outError) const;
};

} // namespace JsonSchema

#endif // SERIALIZATION_SCHEMA_MANIFEST_SCHEMAS_H
