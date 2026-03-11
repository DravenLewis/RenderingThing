/**
 * @file src/Assets/Core/AssetDescriptorUtils.cpp
 * @brief Implementation for AssetDescriptorUtils.
 */

#include "Assets/Core/AssetDescriptorUtils.h"

#include "Assets/Bundles/AssetBundleRegistry.h"
#include "Assets/Core/Asset.h"
#include "Foundation/IO/File.h"
#include "Foundation/Util/StringUtils.h"

#include <cstring>

namespace AssetDescriptorUtils {

std::filesystem::path GetAssetRootPath(){
    return std::filesystem::path(File::GetCWD()) / "res";
}

bool IsAssetRef(const std::string& value){
    return !value.empty() && value[0] == '@';
}

std::string MakeAssetRefFromRelative(const std::string& relative){
    if(relative.empty()){
        return std::string(ASSET_DELIMITER);
    }
    return std::string(ASSET_DELIMITER) + "/" + relative;
}

bool AssetRefToAbsolutePath(const std::string& assetRef, std::filesystem::path& outPath){
    if(assetRef.empty()){
        return false;
    }
    if(StringUtils::BeginsWith(assetRef, ASSET_DELIMITER)){
        std::string rel = assetRef.substr(std::strlen(ASSET_DELIMITER));
        if(!rel.empty() && (rel[0] == '/' || rel[0] == '\\')){
            rel.erase(rel.begin());
        }
        outPath = GetAssetRootPath() / rel;
        return true;
    }
    if(IsAssetRef(assetRef)){
        std::string entryPath;
        std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleForAssetRef(assetRef, &entryPath);
        if(!bundle){
            return false;
        }

        outPath = AssetBundleRegistry::MakeVirtualEntryPath(bundle->path(), entryPath, false);
        return true;
    }
    outPath = std::filesystem::path(assetRef);
    return true;
}

std::string AbsolutePathToAssetRef(const std::filesystem::path& absolutePath){
    const std::string rawPath = absolutePath.generic_string();
    if(IsAssetRef(rawPath)){
        return rawPath;
    }

    std::filesystem::path bundlePath;
    std::string entryPath;
    if(AssetBundleRegistry::DecodeVirtualEntryPath(absolutePath, bundlePath, entryPath)){
        std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
        if(bundle){
            if(entryPath.empty()){
                return bundle->aliasToken();
            }

            std::string normalizedEntry = entryPath;
            if(!normalizedEntry.empty() && normalizedEntry.back() == '/'){
                normalizedEntry.pop_back();
            }
            return bundle->aliasToken() + "/" + normalizedEntry;
        }
    }

    std::error_code ec;
    std::filesystem::path assetRoot = std::filesystem::weakly_canonical(GetAssetRootPath(), ec);
    if(ec){
        assetRoot = GetAssetRootPath().lexically_normal();
    }

    std::filesystem::path absolute = std::filesystem::weakly_canonical(absolutePath, ec);
    if(ec){
        absolute = absolutePath.lexically_normal();
    }

    std::filesystem::path rel = absolute.lexically_relative(assetRoot);
    if(rel.empty()){
        return absolute.generic_string();
    }
    if(StringUtils::BeginsWith(rel.generic_string(), "..")){
        return absolute.generic_string();
    }
    return MakeAssetRefFromRelative(rel.generic_string());
}

bool PathExists(const std::filesystem::path& path, bool* outIsDirectory){
    if(outIsDirectory){
        *outIsDirectory = false;
    }

    std::filesystem::path bundlePath;
    std::string entryPath;
    if(AssetBundleRegistry::DecodeVirtualEntryPath(path, bundlePath, entryPath)){
        std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
        if(!bundle){
            return false;
        }

        if(entryPath.empty()){
            if(outIsDirectory){
                *outIsDirectory = true;
            }
            return true;
        }

        const std::string normalizedFile = CompressedFile::NormalizeEntryPath(entryPath);
        const std::string normalizedDir = CompressedFile::NormalizeEntryPath(entryPath, true);
        for(const auto& entry : bundle->getEntries()){
            if(entry.path == normalizedFile){
                if(outIsDirectory){
                    *outIsDirectory = (entry.kind == "directory");
                }
                return true;
            }
            if(entry.path == normalizedDir && entry.kind == "directory"){
                if(outIsDirectory){
                    *outIsDirectory = true;
                }
                return true;
            }
        }
        return false;
    }

    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if(!exists || ec){
        return false;
    }

    if(outIsDirectory){
        *outIsDirectory = std::filesystem::is_directory(path, ec);
    }
    return !ec;
}

bool ReadTextAsset(const std::string& assetRef, std::string& outText, std::string* outError){
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

bool WriteTextAsset(const std::string& assetRef, const std::string& text, std::string* outError){
    std::filesystem::path resolvedPath;
    if(!AssetRefToAbsolutePath(assetRef, resolvedPath)){
        if(outError){
            *outError = "Failed to resolve asset path: " + assetRef;
        }
        return false;
    }

    return WriteTextPath(resolvedPath, text, outError);
}

bool ReadTextPath(const std::filesystem::path& path, std::string& outText, std::string* outError){
    std::filesystem::path bundlePath;
    std::string entryPath;
    if(AssetBundleRegistry::DecodeVirtualEntryPath(path, bundlePath, entryPath)){
        std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
        if(!bundle){
            if(outError){
                *outError = "Failed to resolve mounted bundle for: " + path.generic_string();
            }
            return false;
        }

        if(entryPath.empty()){
            if(outError){
                *outError = "Cannot read directory contents as text: " + path.generic_string();
            }
            return false;
        }

        return bundle->readEntryText(entryPath, outText, outError);
    }

    std::error_code ec;
    const std::filesystem::path assetRoot = std::filesystem::weakly_canonical(GetAssetRootPath(), ec);
    std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, ec);
    if(ec){
        normalizedPath = path.lexically_normal();
    }
    if(!std::filesystem::exists(normalizedPath, ec) || std::filesystem::is_directory(normalizedPath, ec)){
        if(outError){
            *outError = "Failed to load file: " + normalizedPath.generic_string();
        }
        return false;
    }

    std::filesystem::path rel = normalizedPath.lexically_relative(assetRoot);
    if(!rel.empty() && !StringUtils::BeginsWith(rel.generic_string(), "..")){
        return ReadTextAsset(MakeAssetRefFromRelative(rel.generic_string()), outText, outError);
    }

    auto asset = std::make_shared<Asset>(normalizedPath.string());
    if(!asset || !asset->load()){
        if(outError){
            *outError = "Failed to load file: " + normalizedPath.generic_string();
        }
        return false;
    }

    outText = asset->asString();
    if(outText.empty() && !asset->asRaw().empty()){
        outText.assign(asset->asRaw().begin(), asset->asRaw().end());
    }
    return true;
}

bool WriteTextPath(const std::filesystem::path& path, const std::string& text, std::string* outError){
    std::filesystem::path bundlePath;
    std::string entryPath;
    if(AssetBundleRegistry::DecodeVirtualEntryPath(path, bundlePath, entryPath)){
        std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
        if(!bundle){
            if(outError){
                *outError = "Failed to resolve mounted bundle for: " + path.generic_string();
            }
            return false;
        }
        if(entryPath.empty()){
            if(outError){
                *outError = "Cannot write text into a bundle directory root.";
            }
            return false;
        }

        BinaryBuffer data(text.begin(), text.end());
        if(!bundle->addOrUpdateFileFromBuffer(entryPath, data, AbsolutePathToAssetRef(path), outError)){
            return false;
        }
        if(!bundle->save(outError)){
            return false;
        }

        AssetManager::Instance.unmanageAliasAssets(bundle->aliasToken());
        return true;
    }

    const std::filesystem::path parent = path.parent_path();
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

    writer->put(text.c_str());
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

bool ReadTextRefOrPath(const std::string& refOrPath, std::string& outText, std::string* outError){
    if(refOrPath.empty()){
        outText.clear();
        return true;
    }
    if(IsAssetRef(refOrPath)){
        return ReadTextAsset(refOrPath, outText, outError);
    }
    return ReadTextPath(std::filesystem::path(refOrPath), outText, outError);
}

bool WriteTextRefOrPath(const std::string& refOrPath, const std::string& text, std::string* outError){
    if(refOrPath.empty()){
        return true;
    }
    if(IsAssetRef(refOrPath)){
        return WriteTextAsset(refOrPath, text, outError);
    }
    return WriteTextPath(std::filesystem::path(refOrPath), text, outError);
}

} // namespace AssetDescriptorUtils
