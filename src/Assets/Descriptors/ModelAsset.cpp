#include "Assets/Descriptors/ModelAsset.h"

#include "Assets/Core/Asset.h"
#include "Assets/Core/AssetDescriptorUtils.h"
#include "Foundation/IO/File.h"
#include "Assets/Descriptors/MaterialAsset.h"
#include "Rendering/Materials/MaterialDefaults.h"
#include "Assets/Importers/OBJLoader.h"
#include "Foundation/Util/StringUtils.h"

#include <sstream>

namespace {
    std::string trimCopy(const std::string& value){
        return StringUtils::Trim(value);
    }

    std::string toLowerCopy(const std::string& value){
        return StringUtils::ToLowerCase(value);
    }

    int parseInt(const std::string& value, int fallback){
        try{
            return std::stoi(trimCopy(value));
        }catch(...){
            return fallback;
        }
    }

    bool isModelAssetPathInternal(const std::filesystem::path& path){
        const std::string lower = toLowerCopy(path.generic_string());
        return StringUtils::EndsWith(lower, ".model.asset");
    }

    bool readTextPath(const std::filesystem::path& path, std::string& outText, std::string* outError){
        return AssetDescriptorUtils::ReadTextPath(path, outText, outError);
    }

    bool readTextRefOrPath(const std::string& refOrPath, std::string& outText, std::string* outError){
        return AssetDescriptorUtils::ReadTextRefOrPath(refOrPath, outText, outError);
    }

    bool toAbsolutePathFromAssetRef(const std::string& assetRef, std::filesystem::path& outPath){
        return AssetDescriptorUtils::AssetRefToAbsolutePath(assetRef, outPath);
    }

    std::string toAssetRefFromAbsolutePath(const std::filesystem::path& absolutePath){
        return AssetDescriptorUtils::AbsolutePathToAssetRef(absolutePath);
    }

    bool parseModelAssetText(const std::string& text, ModelAssetData& outData){
        std::istringstream stream(text);
        std::string line;
        while(std::getline(stream, line)){
            std::string trimmed = trimCopy(line);
            if(trimmed.empty()){
                continue;
            }
            if(StringUtils::BeginsWith(trimmed, "#") || StringUtils::BeginsWith(trimmed, "//") || StringUtils::BeginsWith(trimmed, ";")){
                continue;
            }
            size_t eq = trimmed.find('=');
            if(eq == std::string::npos){
                continue;
            }

            const std::string key = toLowerCopy(trimCopy(trimmed.substr(0, eq)));
            const std::string value = trimCopy(trimmed.substr(eq + 1));
            if(key == "name"){
                outData.name = value;
            }else if(key == "@link-parent" || key == "link-parent" || key == "link_parent" || key == "linkparent"){
                outData.linkParentRef = value;
            }else if(key == "source_model" || key == "source" || key == "model_source" || key == "source_asset"){
                outData.sourceModelRef = value;
            }else if(key == "default_material" || key == "material_asset" || key == "material"){
                outData.defaultMaterialRef = value;
            }else if(key == "force_smooth_normals" || key == "smooth_normals"){
                outData.forceSmoothNormals = parseInt(value, outData.forceSmoothNormals);
            }
        }
        return true;
    }
}

namespace ModelAssetIO {

bool IsModelAssetPath(const std::filesystem::path& path){
    return isModelAssetPathInternal(path);
}

bool ResolveModelAssetRef(const std::string& modelAssetRefOrPath, std::string& outAssetRef, std::string* outError){
    std::string currentRef = trimCopy(modelAssetRefOrPath);
    if(currentRef.empty()){
        if(outError){
            *outError = "Model asset reference is empty.";
        }
        return false;
    }

    std::filesystem::path absolutePath;
    if(!toAbsolutePathFromAssetRef(currentRef, absolutePath)){
        if(outError){
            *outError = "Invalid model asset reference: " + currentRef;
        }
        return false;
    }

    if(!IsModelAssetPath(absolutePath)){
        if(outError){
            *outError = "Unsupported model asset path (expected .model.asset): " + absolutePath.generic_string();
        }
        return false;
    }

    std::error_code ec;
    if(!std::filesystem::exists(absolutePath, ec) || std::filesystem::is_directory(absolutePath, ec)){
        if(outError){
            *outError = "Model asset does not exist: " + absolutePath.generic_string();
        }
        return false;
    }

    if(AssetDescriptorUtils::IsAssetRef(currentRef)){
        outAssetRef = currentRef;
    }else{
        outAssetRef = toAssetRefFromAbsolutePath(absolutePath);
    }
    return true;
}

bool LoadFromAbsolutePath(const std::filesystem::path& path, ModelAssetData& outData, std::string* outError){
    if(!IsModelAssetPath(path)){
        if(outError){
            *outError = "Not a model asset path: " + path.generic_string();
        }
        return false;
    }

    outData = ModelAssetData{};
    std::string text;
    if(!readTextPath(path, text, outError)){
        return false;
    }
    parseModelAssetText(text, outData);
    if(outData.name.empty()){
        outData.name = path.filename().string();
    }
    return true;
}

bool LoadFromAssetRef(const std::string& assetRef, ModelAssetData& outData, std::string* outError){
    std::string resolvedAssetRef;
    if(!ResolveModelAssetRef(assetRef, resolvedAssetRef, outError)){
        return false;
    }

    std::filesystem::path resolvedPath;
    if(!toAbsolutePathFromAssetRef(resolvedAssetRef, resolvedPath)){
        if(outError){
            *outError = "Invalid resolved model asset path: " + resolvedAssetRef;
        }
        return false;
    }

    outData = ModelAssetData{};
    std::string text;
    if(!readTextRefOrPath(resolvedAssetRef, text, outError)){
        return false;
    }
    parseModelAssetText(text, outData);
    if(outData.name.empty()){
        outData.name = resolvedPath.filename().string();
    }
    return true;
}

bool SaveToAbsolutePath(const std::filesystem::path& path, const ModelAssetData& data, std::string* outError){
    if(!IsModelAssetPath(path)){
        if(outError){
            *outError = "Model asset files must use .model.asset extension.";
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

    auto writer = std::make_unique<FileWriter>(new File(path.string()));
    if(!writer){
        if(outError){
            *outError = "Failed to open file for write: " + path.generic_string();
        }
        return false;
    }

    const std::string modelName = data.name.empty() ? path.filename().string() : data.name;
    writer->putln(StringUtils::Format("name=%s", modelName.c_str()).c_str());
    writer->putln(StringUtils::Format("@link-parent=%s", data.linkParentRef.c_str()).c_str());
    writer->putln(StringUtils::Format("source_model=%s", data.sourceModelRef.c_str()).c_str());
    writer->putln(StringUtils::Format("default_material=%s", data.defaultMaterialRef.c_str()).c_str());
    writer->putln(StringUtils::Format("force_smooth_normals=%d", data.forceSmoothNormals).c_str());

    if(!writer->flush()){
        if(outError){
            *outError = "Failed to write file: " + path.generic_string();
        }
        writer->close();
        return false;
    }
    writer->close();
    AssetManager::Instance.unmanageAsset(path.generic_string());
    return true;
}

bool SaveToAssetRef(const std::string& assetRef, const ModelAssetData& data, std::string* outError){
    std::filesystem::path path;
    if(!toAbsolutePathFromAssetRef(assetRef, path)){
        if(outError){
            *outError = "Invalid model asset path: " + assetRef;
        }
        return false;
    }
    return SaveToAbsolutePath(path, data, outError);
}

std::shared_ptr<Model> InstantiateModel(const ModelAssetData& data, std::string* outError){
    const std::string sourceRef = trimCopy(data.sourceModelRef);
    if(sourceRef.empty()){
        if(outError){
            *outError = "Model asset has no source_model.";
        }
        return nullptr;
    }

    std::filesystem::path sourcePath;
    if(!toAbsolutePathFromAssetRef(sourceRef, sourcePath)){
        if(outError){
            *outError = "Invalid source_model path: " + sourceRef;
        }
        return nullptr;
    }

    const std::string sourceExt = toLowerCopy(sourcePath.extension().string());
    if(sourceExt.empty()){
        if(outError){
            *outError = "Model source has no file extension: " + sourceRef;
        }
        return nullptr;
    }

    auto sourceAsset = AssetManager::Instance.getOrLoad(sourceRef);
    if(!sourceAsset){
        sourceAsset = std::make_shared<Asset>(sourcePath.string());
        if(!sourceAsset || !sourceAsset->load()){
            if(outError){
                *outError = "Failed to load source model: " + sourceRef;
            }
            return nullptr;
        }
    }

    std::shared_ptr<Material> fallbackMaterial;
    if(!data.defaultMaterialRef.empty()){
        fallbackMaterial = MaterialAssetIO::InstantiateMaterialFromRef(data.defaultMaterialRef, nullptr, nullptr);
    }
    if(!fallbackMaterial){
        fallbackMaterial = MaterialDefaults::LitColorMaterial::Create(Color::WHITE);
    }

    if(sourceExt == ".obj"){
        const bool forceSmooth = (data.forceSmoothNormals != 0);
        auto model = OBJLoader::LoadFromAsset(sourceAsset, fallbackMaterial, forceSmooth);
        if(!model && outError){
            *outError = "OBJ model load failed for source: " + sourceRef;
        }
        return model;
    }

    if(outError){
        *outError = "Unsupported model source format: " + sourceExt + " (currently only .obj is implemented)";
    }
    return nullptr;
}

std::shared_ptr<Model> InstantiateModelFromRef(const std::string& modelAssetRefOrPath, std::string* outResolvedAssetRef, std::string* outError){
    std::string resolvedAssetRef;
    if(!ResolveModelAssetRef(modelAssetRefOrPath, resolvedAssetRef, outError)){
        return nullptr;
    }

    ModelAssetData data;
    if(!LoadFromAssetRef(resolvedAssetRef, data, outError)){
        return nullptr;
    }

    if(outResolvedAssetRef){
        *outResolvedAssetRef = resolvedAssetRef;
    }
    return InstantiateModel(data, outError);
}

} // namespace ModelAssetIO
