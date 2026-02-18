#include "ShaderAsset.h"

#include "Asset.h"
#include "File.h"
#include "ShaderProgram.h"
#include "StringUtils.h"

#include <sstream>
#include <cstring>

namespace {
    std::string toGenericLower(const std::filesystem::path& path){
        return StringUtils::ToLowerCase(path.generic_string());
    }

    std::string makeAssetRefFromRelative(const std::string& relative){
        if(relative.empty()){
            return std::string(ASSET_DELIMITER);
        }
        return std::string(ASSET_DELIMITER) + "/" + relative;
    }

    bool isAssetRef(const std::string& value){
        return StringUtils::BeginsWith(value, ASSET_DELIMITER);
    }

    bool readTextAsset(const std::string& assetRef, std::string& outText, std::string* outError){
        auto asset = AssetManager::Instance.getOrLoad(assetRef);
        if(!asset){
            if(outError){
                *outError = "Failed to load asset: " + assetRef;
            }
            return false;
        }

        outText = asset->asString();
        if(outText.empty() && !asset->asRaw().empty()){
            outText.assign(asset->asRaw().begin(), asset->asRaw().end());
        }
        return true;
    }

    bool readTextPath(const std::filesystem::path& path, std::string& outText, std::string* outError){
        std::error_code ec;
        const std::filesystem::path assetRoot = std::filesystem::weakly_canonical(std::filesystem::path(File::GetCWD()) / "res", ec);
        std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, ec);
        if(ec){
            normalizedPath = path.lexically_normal();
        }

        std::filesystem::path rel = normalizedPath.lexically_relative(assetRoot);
        if(!rel.empty() && !StringUtils::BeginsWith(rel.generic_string(), "..")){
            return readTextAsset(makeAssetRefFromRelative(rel.generic_string()), outText, outError);
        }

        auto asset = std::make_shared<Asset>(path.string());
        if(!asset || !asset->load()){
            if(outError){
                *outError = "Failed to load file: " + path.generic_string();
            }
            return false;
        }
        outText = asset->asString();
        if(outText.empty() && !asset->asRaw().empty()){
            outText.assign(asset->asRaw().begin(), asset->asRaw().end());
        }
        return true;
    }

    bool readTextRefOrPath(const std::string& refOrPath, std::string& outText, std::string* outError){
        if(refOrPath.empty()){
            outText.clear();
            return true;
        }
        if(isAssetRef(refOrPath)){
            return readTextAsset(refOrPath, outText, outError);
        }
        return readTextPath(std::filesystem::path(refOrPath), outText, outError);
    }

    std::string sanitizeCacheName(const std::string& value){
        std::string out = value;
        for(char& c : out){
            if((c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') ||
               c == '_' || c == '-'){
                continue;
            }
            c = '_';
        }
        return out;
    }

    void parseShaderAssetText(const std::string& text, ShaderAssetData& outData){
        std::istringstream stream(text);
        std::string line;
        while(std::getline(stream, line)){
            std::string trimmed = StringUtils::Trim(line);
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

            std::string key = StringUtils::ToLowerCase(StringUtils::Trim(trimmed.substr(0, eq)));
            std::string value = StringUtils::Trim(trimmed.substr(eq + 1));

            if(key == "cache_name" || key == "cache" || key == "name"){
                outData.cacheName = value;
            }else if(key == "vertex" || key == "vert" || key == "vertex_shader"){
                outData.vertexAssetRef = value;
            }else if(key == "fragment" || key == "frag" || key == "fragment_shader"){
                outData.fragmentAssetRef = value;
            }else if(key == "geometry" || key == "geom" || key == "geometry_shader"){
                outData.geometryAssetRef = value;
            }else if(key == "tesselation" || key == "tess" || key == "tesselation_shader"){
                outData.tesselationAssetRef = value;
            }else if(key == "compute" || key == "compute_shader"){
                outData.computeAssetRef = value;
            }else if(key == "task" || key == "task_shader"){
                outData.taskAssetRef = value;
            }else if(key == "rt" || key == "raytrace" || key == "raytrace_shader"){
                outData.rtAssetRef = value;
            }
        }
    }
}

namespace ShaderAssetIO {

bool IsShaderAssetPath(const std::filesystem::path& path){
    return StringUtils::EndsWith(toGenericLower(path), ".shader.asset");
}

std::filesystem::path AssetRefToAbsolutePath(const std::string& assetRef){
    const std::filesystem::path assetRoot = std::filesystem::path(File::GetCWD()) / "res";
    if(assetRef.empty()){
        return {};
    }
    if(assetRef == ASSET_DELIMITER){
        return assetRoot;
    }
    if(StringUtils::BeginsWith(assetRef, ASSET_DELIMITER)){
        std::string rel = assetRef.substr(std::strlen(ASSET_DELIMITER));
        if(!rel.empty() && (rel[0] == '/' || rel[0] == '\\')){
            rel.erase(rel.begin());
        }
        return (assetRoot / rel).lexically_normal();
    }

    std::filesystem::path path(assetRef);
    if(path.is_absolute()){
        return path.lexically_normal();
    }
    return (assetRoot / path).lexically_normal();
}

std::string AbsolutePathToAssetRef(const std::filesystem::path& absolutePath){
    std::error_code ec;
    const std::filesystem::path assetRoot = std::filesystem::weakly_canonical(std::filesystem::path(File::GetCWD()) / "res", ec);
    std::filesystem::path normalized = std::filesystem::weakly_canonical(absolutePath, ec);
    if(ec){
        normalized = absolutePath.lexically_normal();
    }
    std::filesystem::path rel = normalized.lexically_relative(assetRoot);
    if(rel.empty() || StringUtils::BeginsWith(rel.generic_string(), "..")){
        return normalized.generic_string();
    }
    return makeAssetRefFromRelative(rel.generic_string());
}

bool LoadFromAbsolutePath(const std::filesystem::path& path, ShaderAssetData& outData, std::string* outError){
    outData = ShaderAssetData{};

    std::string text;
    if(!readTextPath(path, text, outError)){
        return false;
    }

    parseShaderAssetText(text, outData);

    if(outData.cacheName.empty()){
        std::string fallback = path.stem().string();
        if(fallback == ".shader"){
            fallback = path.filename().string();
        }
        if(fallback.empty()){
            fallback = "ShaderAssetProgram";
        }
        outData.cacheName = sanitizeCacheName(fallback);
    }

    return true;
}

bool LoadFromAssetRef(const std::string& assetRef, ShaderAssetData& outData, std::string* outError){
    outData = ShaderAssetData{};

    std::string text;
    if(!readTextRefOrPath(assetRef, text, outError)){
        return false;
    }

    parseShaderAssetText(text, outData);

    if(outData.cacheName.empty()){
        outData.cacheName = sanitizeCacheName(std::filesystem::path(assetRef).filename().string());
        if(outData.cacheName.empty()){
            outData.cacheName = "ShaderAssetProgram";
        }
    }
    return true;
}

bool SaveToAbsolutePath(const std::filesystem::path& path, const ShaderAssetData& data, std::string* outError){
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

    std::string cacheName = data.cacheName.empty() ? "ShaderAssetProgram" : data.cacheName;
    writer->putln(StringUtils::Format("cache_name=%s", cacheName.c_str()).c_str());
    writer->putln(StringUtils::Format("vertex=%s", data.vertexAssetRef.c_str()).c_str());
    writer->putln(StringUtils::Format("fragment=%s", data.fragmentAssetRef.c_str()).c_str());
    if(!data.geometryAssetRef.empty()){
        writer->putln(StringUtils::Format("geometry=%s", data.geometryAssetRef.c_str()).c_str());
    }
    if(!data.tesselationAssetRef.empty()){
        writer->putln(StringUtils::Format("tesselation=%s", data.tesselationAssetRef.c_str()).c_str());
    }
    if(!data.computeAssetRef.empty()){
        writer->putln(StringUtils::Format("compute=%s", data.computeAssetRef.c_str()).c_str());
    }
    if(!data.taskAssetRef.empty()){
        writer->putln(StringUtils::Format("task=%s", data.taskAssetRef.c_str()).c_str());
    }
    if(!data.rtAssetRef.empty()){
        writer->putln(StringUtils::Format("rt=%s", data.rtAssetRef.c_str()).c_str());
    }
    if(!writer->flush()){
        if(outError){
            *outError = "Failed to write file: " + path.generic_string();
        }
        writer->close();
        return false;
    }
    writer->close();
    return true;
}

bool SaveToAssetRef(const std::string& assetRef, const ShaderAssetData& data, std::string* outError){
    return SaveToAbsolutePath(AssetRefToAbsolutePath(assetRef), data, outError);
}

std::shared_ptr<ShaderProgram> CompileProgram(const ShaderAssetData& data,
                                              const std::string& cacheNameOverride,
                                              bool forceRecompile,
                                              std::string* outError){
    if(!data.isComplete()){
        if(outError){
            *outError = "Shader asset is missing vertex or fragment path.";
        }
        return nullptr;
    }

    std::string vertexCode;
    std::string fragmentCode;
    if(!readTextRefOrPath(data.vertexAssetRef, vertexCode, outError)){
        return nullptr;
    }
    if(!readTextRefOrPath(data.fragmentAssetRef, fragmentCode, outError)){
        return nullptr;
    }
    std::string geometryCode;
    std::string tesselationCode;
    std::string computeCode;
    std::string taskCode;
    std::string rtCode;
    if(!readTextRefOrPath(data.geometryAssetRef, geometryCode, outError)) return nullptr;
    if(!readTextRefOrPath(data.tesselationAssetRef, tesselationCode, outError)) return nullptr;
    if(!readTextRefOrPath(data.computeAssetRef, computeCode, outError)) return nullptr;
    if(!readTextRefOrPath(data.taskAssetRef, taskCode, outError)) return nullptr;
    if(!readTextRefOrPath(data.rtAssetRef, rtCode, outError)) return nullptr;

    std::string cacheName = cacheNameOverride.empty() ? data.cacheName : cacheNameOverride;
    if(cacheName.empty()){
        cacheName = "ShaderAssetProgram";
    }
    cacheName = sanitizeCacheName(cacheName);

    if(forceRecompile){
        ShaderCacheManager::INSTANCE.programCache.erase(cacheName);
    }

    auto program = ShaderCacheManager::INSTANCE.getOrCompile(
        cacheName,
        vertexCode,
        fragmentCode,
        geometryCode,
        tesselationCode,
        computeCode,
        taskCode,
        rtCode
    );
    if(!program || program->getID() == 0){
        if(outError){
            *outError = "Shader compile/link failed for cache '" + cacheName + "'.";
            if(program){
                *outError += "\n" + program->getLog();
            }
        }
        return nullptr;
    }

    return program;
}

} // namespace ShaderAssetIO
