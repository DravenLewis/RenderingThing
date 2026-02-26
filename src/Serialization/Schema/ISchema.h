#ifndef SERIALIZATION_SCHEMA_ISCHEMA_H
#define SERIALIZATION_SCHEMA_ISCHEMA_H

#include <filesystem>
#include <string>

#include "Serialization/Json/JsonUtils.h"

namespace JsonSchema {

class ISchema {
    public:
        struct Header {
            std::string type;
            int version = 0;
            yyjson_val* payload = nullptr;
        };

        virtual ~ISchema() = default;

        bool LoadFromDocument(const JsonUtils::Document& doc, std::string* outError = nullptr);
        bool LoadFromText(const std::string& jsonText, std::string* outError = nullptr);
        bool LoadFromAbsolutePath(const std::filesystem::path& path, std::string* outError = nullptr);
        bool LoadFromAssetRef(const std::string& assetRef, std::string* outError = nullptr);

        bool BuildDocument(JsonUtils::MutableDocument& outDoc, std::string* outError = nullptr, int targetVersion = -1) const;
        bool WriteToString(std::string& outJson, std::string* outError = nullptr, bool pretty = true, int targetVersion = -1) const;
        bool SaveToAbsolutePath(const std::filesystem::path& path, std::string* outError = nullptr, bool pretty = true, int targetVersion = -1) const;
        bool SaveToAssetRef(const std::string& assetRef, std::string* outError = nullptr, bool pretty = true, int targetVersion = -1) const;

        virtual const char* SchemaType() const = 0;
        virtual int CurrentVersion() const = 0;

        // Shared version range helper. Override SupportsReadVersion()/SupportsWriteVersion()
        // if read/write compatibility differs.
        virtual int MinimumSupportedVersion() const { return CurrentVersion(); }
        virtual bool SupportsVersion(int version) const;
        virtual bool SupportsReadVersion(int version) const;
        virtual bool SupportsWriteVersion(int version) const;

    protected:
        virtual bool DeserializePayload(yyjson_val* payload, int version, std::string* outError) = 0;
        virtual bool SerializePayload(yyjson_mut_doc* doc, yyjson_mut_val* payload, int version, std::string* outError) const = 0;

        // Optional preflight validation hook before DeserializePayload().
        virtual bool ValidatePayload(yyjson_val* payload, int version, std::string* outError) const;
        virtual bool OnBeforeLoad(const Header& header, std::string* outError);
        virtual bool OnAfterLoad(const Header& header, std::string* outError);
        virtual bool OnBeforeSave(int version, std::string* outError) const;
        virtual bool OnAfterSave(int version, std::string* outError) const;

        bool ReadAndValidateHeader(const JsonUtils::Document& doc, Header& outHeader, std::string* outError = nullptr) const;
        bool ResolveWriteVersion(int requestedVersion, int& outVersion, std::string* outError = nullptr) const;
        bool GetValidatedSchemaType(std::string& outType, std::string* outError = nullptr) const;
};

} // namespace JsonSchema

#endif // SERIALIZATION_SCHEMA_ISCHEMA_H
