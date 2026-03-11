/**
 * @file src/Serialization/Schema/ISchema.h
 * @brief Declarations for ISchema.
 */

#ifndef SERIALIZATION_SCHEMA_ISCHEMA_H
#define SERIALIZATION_SCHEMA_ISCHEMA_H

#include <filesystem>
#include <string>

#include "Serialization/Json/JsonUtils.h"

namespace JsonSchema {

/// @brief Represents the ISchema type.
class ISchema {
    public:
        /// @brief Holds data for Header.
        struct Header {
            std::string type;
            int version = 0;
            yyjson_val* payload = nullptr;
        };

        /**
         * @brief Destroys this ISchema instance.
         */
        virtual ~ISchema() = default;

        /**
         * @brief Loads from document.
         * @param doc Value for doc.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool LoadFromDocument(const JsonUtils::Document& doc, std::string* outError = nullptr);
        /**
         * @brief Loads from text.
         * @param jsonText Value for json text.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool LoadFromText(const std::string& jsonText, std::string* outError = nullptr);
        /**
         * @brief Loads from absolute path.
         * @param path Filesystem path for path.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool LoadFromAbsolutePath(const std::filesystem::path& path, std::string* outError = nullptr);
        /**
         * @brief Loads from asset ref.
         * @param assetRef Reference to asset.
         * @param outError Output value for error.
         * @return True when the operation succeeds; otherwise false.
         */
        bool LoadFromAssetRef(const std::string& assetRef, std::string* outError = nullptr);

        /**
         * @brief Builds document.
         * @param outDoc Output value for doc.
         * @param outError Output value for error.
         * @param targetVersion Value for target version.
         * @return True when the operation succeeds; otherwise false.
         */
        bool BuildDocument(JsonUtils::MutableDocument& outDoc, std::string* outError = nullptr, int targetVersion = -1) const;
        /**
         * @brief Writes to string.
         * @param outJson Buffer that receives json data.
         * @param outError Output value for error.
         * @param pretty Value for pretty.
         * @param targetVersion Value for target version.
         * @return True when the operation succeeds; otherwise false.
         */
        bool WriteToString(std::string& outJson, std::string* outError = nullptr, bool pretty = true, int targetVersion = -1) const;
        /**
         * @brief Saves to absolute path.
         * @param path Filesystem path for path.
         * @param outError Output value for error.
         * @param pretty Value for pretty.
         * @param targetVersion Value for target version.
         * @return True when the operation succeeds; otherwise false.
         */
        bool SaveToAbsolutePath(const std::filesystem::path& path, std::string* outError = nullptr, bool pretty = true, int targetVersion = -1) const;
        /**
         * @brief Saves to asset ref.
         * @param assetRef Reference to asset.
         * @param outError Output value for error.
         * @param pretty Value for pretty.
         * @param targetVersion Value for target version.
         * @return True when the operation succeeds; otherwise false.
         */
        bool SaveToAssetRef(const std::string& assetRef, std::string* outError = nullptr, bool pretty = true, int targetVersion = -1) const;

        /**
         * @brief Returns the schema type identifier.
         * @return Pointer to the resulting object.
         */
        virtual const char* SchemaType() const = 0;
        /**
         * @brief Returns the current schema version.
         * @return Computed numeric result.
         */
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
