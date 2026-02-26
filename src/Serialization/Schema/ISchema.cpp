#include "Serialization/Schema/ISchema.h"

namespace {
    void SetSchemaError(std::string* outError, const std::string& message){
        if(outError){
            *outError = message;
        }
    }

    void PrefixSchemaError(std::string* outError, const std::string& prefix){
        if(!outError || outError->empty()){
            return;
        }
        *outError = prefix + *outError;
    }
}

namespace JsonSchema {

bool ISchema::SupportsVersion(int version) const{
    const int minVersion = MinimumSupportedVersion();
    const int maxVersion = CurrentVersion();
    if(minVersion <= 0 || maxVersion <= 0 || minVersion > maxVersion){
        return false;
    }
    return version >= minVersion && version <= maxVersion;
}

bool ISchema::SupportsReadVersion(int version) const{
    return SupportsVersion(version);
}

bool ISchema::SupportsWriteVersion(int version) const{
    return SupportsVersion(version);
}

bool ISchema::ValidatePayload(yyjson_val* payload, int, std::string* outError) const{
    if(!payload){
        SetSchemaError(outError, "JSON schema payload is null.");
        return false;
    }
    if(!yyjson_is_obj(payload)){
        SetSchemaError(outError, "JSON schema payload must be an object.");
        return false;
    }
    return true;
}

bool ISchema::OnBeforeLoad(const Header&, std::string*){
    return true;
}

bool ISchema::OnAfterLoad(const Header&, std::string*){
    return true;
}

bool ISchema::OnBeforeSave(int, std::string*) const{
    return true;
}

bool ISchema::OnAfterSave(int, std::string*) const{
    return true;
}

bool ISchema::GetValidatedSchemaType(std::string& outType, std::string* outError) const{
    const char* expectedType = SchemaType();
    outType = expectedType ? expectedType : "";
    if(outType.empty()){
        SetSchemaError(outError, "SchemaType() returned an empty type string.");
        return false;
    }
    return true;
}

bool ISchema::ReadAndValidateHeader(const JsonUtils::Document& doc, Header& outHeader, std::string* outError) const{
    outHeader = Header{};

    if(!JsonUtils::ReadStandardDocumentHeader(doc, outHeader.type, outHeader.version, &outHeader.payload, outError)){
        return false;
    }

    std::string expected;
    if(!GetValidatedSchemaType(expected, outError)){
        return false;
    }

    if(outHeader.type != expected){
        SetSchemaError(
            outError,
            "Schema type mismatch. Expected '" + expected + "' but got '" + outHeader.type + "'."
        );
        return false;
    }

    if(!SupportsReadVersion(outHeader.version)){
        SetSchemaError(
            outError,
            "Unsupported schema read version " + std::to_string(outHeader.version) +
            " for type '" + expected + "'."
        );
        return false;
    }

    return true;
}

bool ISchema::ResolveWriteVersion(int requestedVersion, int& outVersion, std::string* outError) const{
    std::string typeName;
    if(!GetValidatedSchemaType(typeName, outError)){
        return false;
    }

    const int currentVersion = CurrentVersion();
    if(currentVersion <= 0){
        SetSchemaError(outError, "CurrentVersion() must be > 0 for schema '" + typeName + "'.");
        return false;
    }

    const int versionToWrite = (requestedVersion < 0) ? currentVersion : requestedVersion;
    if(versionToWrite <= 0){
        SetSchemaError(outError, "Requested schema write version must be > 0.");
        return false;
    }

    if(!SupportsWriteVersion(versionToWrite)){
        SetSchemaError(
            outError,
            "Schema '" + typeName + "' does not support writing version " + std::to_string(versionToWrite) + "."
        );
        return false;
    }

    outVersion = versionToWrite;
    return true;
}

bool ISchema::LoadFromDocument(const JsonUtils::Document& doc, std::string* outError){
    Header header;
    if(!ReadAndValidateHeader(doc, header, outError)){
        return false;
    }

    if(!ValidatePayload(header.payload, header.version, outError)){
        return false;
    }

    if(!OnBeforeLoad(header, outError)){
        return false;
    }

    if(!DeserializePayload(header.payload, header.version, outError)){
        return false;
    }

    return OnAfterLoad(header, outError);
}

bool ISchema::LoadFromText(const std::string& jsonText, std::string* outError){
    JsonUtils::Document doc;
    if(!JsonUtils::LoadDocumentFromText(jsonText, doc, outError)){
        return false;
    }
    if(!LoadFromDocument(doc, outError)){
        std::string typeName;
        if(GetValidatedSchemaType(typeName, nullptr)){
            PrefixSchemaError(outError, "Failed to load schema '" + typeName + "' from JSON text: ");
        }
        return false;
    }
    return true;
}

bool ISchema::LoadFromAbsolutePath(const std::filesystem::path& path, std::string* outError){
    JsonUtils::Document doc;
    if(!JsonUtils::LoadDocumentFromAbsolutePath(path, doc, outError)){
        return false;
    }
    if(!LoadFromDocument(doc, outError)){
        std::string typeName;
        if(GetValidatedSchemaType(typeName, nullptr)){
            PrefixSchemaError(
                outError,
                "Failed to load schema '" + typeName + "' from '" + path.generic_string() + "': "
            );
        }
        return false;
    }
    return true;
}

bool ISchema::LoadFromAssetRef(const std::string& assetRef, std::string* outError){
    JsonUtils::Document doc;
    if(!JsonUtils::LoadDocumentFromAssetRef(assetRef, doc, outError)){
        return false;
    }
    if(!LoadFromDocument(doc, outError)){
        std::string typeName;
        if(GetValidatedSchemaType(typeName, nullptr)){
            PrefixSchemaError(
                outError,
                "Failed to load schema '" + typeName + "' from asset ref '" + assetRef + "': "
            );
        }
        return false;
    }
    return true;
}

bool ISchema::BuildDocument(JsonUtils::MutableDocument& outDoc, std::string* outError, int targetVersion) const{
    int resolvedVersion = 0;
    if(!ResolveWriteVersion(targetVersion, resolvedVersion, outError)){
        return false;
    }

    if(!OnBeforeSave(resolvedVersion, outError)){
        return false;
    }

    JsonUtils::StandardDocumentRefs refs;
    std::string typeName;
    if(!GetValidatedSchemaType(typeName, outError)){
        return false;
    }

    if(!JsonUtils::CreateStandardDocument(outDoc, typeName, resolvedVersion, refs, outError)){
        return false;
    }

    if(!refs.payload || !outDoc.get()){
        SetSchemaError(outError, "Failed to create schema payload object.");
        return false;
    }

    if(!SerializePayload(outDoc.get(), refs.payload, resolvedVersion, outError)){
        return false;
    }

    return OnAfterSave(resolvedVersion, outError);
}

bool ISchema::WriteToString(std::string& outJson, std::string* outError, bool pretty, int targetVersion) const{
    JsonUtils::MutableDocument doc;
    if(!BuildDocument(doc, outError, targetVersion)){
        std::string typeName;
        if(GetValidatedSchemaType(typeName, nullptr)){
            PrefixSchemaError(outError, "Failed to build schema '" + typeName + "' for JSON write: ");
        }
        return false;
    }
    return JsonUtils::WriteDocumentToString(doc, outJson, outError, pretty);
}

bool ISchema::SaveToAbsolutePath(const std::filesystem::path& path, std::string* outError, bool pretty, int targetVersion) const{
    JsonUtils::MutableDocument doc;
    if(!BuildDocument(doc, outError, targetVersion)){
        std::string typeName;
        if(GetValidatedSchemaType(typeName, nullptr)){
            PrefixSchemaError(
                outError,
                "Failed to build schema '" + typeName + "' for save to '" + path.generic_string() + "': "
            );
        }
        return false;
    }
    return JsonUtils::SaveDocumentToAbsolutePath(path, doc, outError, pretty);
}

bool ISchema::SaveToAssetRef(const std::string& assetRef, std::string* outError, bool pretty, int targetVersion) const{
    JsonUtils::MutableDocument doc;
    if(!BuildDocument(doc, outError, targetVersion)){
        std::string typeName;
        if(GetValidatedSchemaType(typeName, nullptr)){
            PrefixSchemaError(
                outError,
                "Failed to build schema '" + typeName + "' for asset ref save '" + assetRef + "': "
            );
        }
        return false;
    }
    return JsonUtils::SaveDocumentToAssetRef(assetRef, doc, outError, pretty);
}

} // namespace JsonSchema
