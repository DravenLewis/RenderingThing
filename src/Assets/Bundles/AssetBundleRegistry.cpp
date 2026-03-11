/**
 * @file src/Assets/Bundles/AssetBundleRegistry.cpp
 * @brief Implementation for AssetBundleRegistry.
 */

#include "Assets/Bundles/AssetBundleRegistry.h"

#include "Foundation/IO/File.h"
#include "Foundation/Logging/Logbot.h"
#include "Foundation/Util/StringUtils.h"

#include <set>
#include <sstream>
#include <vector>

namespace {

inline static Logbot bundleRegistryLogger = Logbot::CreateInstance("AssetBundleRegistry");

void setBundleRegistryError(std::string* outError, const std::string& message){
    if(outError){
        *outError = message;
    }
}

std::filesystem::path NormalizeBundleVirtualPath(const std::filesystem::path& path){
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if(ec){
        normalized = path.lexically_normal();
    }
    return normalized;
}

char hexDigit(unsigned int value){
    return (value < 10U) ? static_cast<char>('0' + value) : static_cast<char>('A' + (value - 10U));
}

std::string encodeVirtualSegment(const std::string& value){
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for(unsigned char c : value){
        const bool keep =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '_' ||
            c == '.';
        if(keep){
            encoded.push_back(static_cast<char>(c));
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(hexDigit((c >> 4) & 0x0F));
        encoded.push_back(hexDigit(c & 0x0F));
    }
    return encoded;
}

int decodeHexNibble(char c){
    if(c >= '0' && c <= '9'){
        return c - '0';
    }
    if(c >= 'a' && c <= 'f'){
        return 10 + (c - 'a');
    }
    if(c >= 'A' && c <= 'F'){
        return 10 + (c - 'A');
    }
    return -1;
}

bool decodeVirtualSegment(const std::string& encoded, std::string& outValue){
    outValue.clear();
    outValue.reserve(encoded.size());

    for(size_t i = 0; i < encoded.size(); ++i){
        const char c = encoded[i];
        if(c != '%'){
            outValue.push_back(c);
            continue;
        }
        if((i + 2) >= encoded.size()){
            return false;
        }

        const int hi = decodeHexNibble(encoded[i + 1]);
        const int lo = decodeHexNibble(encoded[i + 2]);
        if(hi < 0 || lo < 0){
            return false;
        }

        outValue.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }

    return true;
}

} // namespace

AssetBundleRegistry AssetBundleRegistry::Instance;

std::string AssetBundleRegistry::NormalizeAliasKey(const std::string& alias){
    std::string normalized = AssetBundle::NormalizeAlias(alias);
    return StringUtils::ToLowerCase(normalized);
}

std::string AssetBundleRegistry::NormalizeBundlePathKey(const std::filesystem::path& path){
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if(ec){
        normalized = path.lexically_normal();
    }

    std::string key = normalized.generic_string();
    #ifdef _WIN32
        key = StringUtils::ToLowerCase(key);
    #endif
    return key;
}

std::string AssetBundleRegistry::VirtualEntryPrefix(){
    return "__bundle_virtual__";
}

void AssetBundleRegistry::clear(){
    for(const auto& kv : bundlesByAliasKey){
        if(kv.second){
            AssetManager::Instance.unregisterAliasProvider(kv.second->aliasToken());
            AssetManager::Instance.unmanageAliasAssets(kv.second->aliasToken());
        }
    }

    bundlesByAliasKey.clear();
    aliasKeyByBundlePath.clear();
}

bool AssetBundleRegistry::mountBundle(const std::filesystem::path& path, std::string* outError){
    if(!AssetBundle::IsBundlePath(path)){
        setBundleRegistryError(outError, "Asset bundles must use the .bundle.asset extension.");
        return false;
    }

    std::shared_ptr<AssetBundle> bundle = std::make_shared<AssetBundle>();
    if(!bundle || !bundle->open(path, outError)){
        return false;
    }

    const std::string bundlePathKey = NormalizeBundlePathKey(path);
    const std::string aliasKey = NormalizeAliasKey(bundle->alias());
    if(bundlePathKey.empty() || aliasKey.empty()){
        setBundleRegistryError(outError, "Failed to normalize bundle identity for: " + path.generic_string());
        return false;
    }

    auto previousPathAlias = aliasKeyByBundlePath.find(bundlePathKey);
    if(previousPathAlias != aliasKeyByBundlePath.end() && previousPathAlias->second != aliasKey){
        auto previousBundle = bundlesByAliasKey.find(previousPathAlias->second);
        if(previousBundle != bundlesByAliasKey.end() && previousBundle->second){
            AssetManager::Instance.unregisterAliasProvider(previousBundle->second->aliasToken());
            AssetManager::Instance.unmanageAliasAssets(previousBundle->second->aliasToken());
            bundlesByAliasKey.erase(previousBundle);
        }
        aliasKeyByBundlePath.erase(previousPathAlias);
    }

    auto aliasCollision = bundlesByAliasKey.find(aliasKey);
    if(aliasCollision != bundlesByAliasKey.end() && aliasCollision->second){
        const std::string oldPathKey = NormalizeBundlePathKey(aliasCollision->second->path());
        if(oldPathKey != bundlePathKey){
            aliasKeyByBundlePath.erase(oldPathKey);
        }
        AssetManager::Instance.unregisterAliasProvider(aliasCollision->second->aliasToken());
        AssetManager::Instance.unmanageAliasAssets(aliasCollision->second->aliasToken());
        bundlesByAliasKey.erase(aliasCollision);
    }

    const std::string aliasToken = bundle->aliasToken();
    const std::string cachePrefix = StringUtils::ToLowerCase(aliasToken);
    const std::shared_ptr<AssetBundle> capturedBundle = bundle;
    if(!AssetManager::Instance.registerAliasProvider(
        aliasToken,
        [capturedBundle, cachePrefix](const std::string& request, AssetManager::ResolvedRequest& outResolved, std::string* outResolverError) -> bool {
            std::string normalizedRequest = StringUtils::ReplaceAll(request.c_str(), "\\", "/");
            size_t slashPos = normalizedRequest.find('/');
            if(slashPos == std::string::npos || slashPos + 1 >= normalizedRequest.size()){
                setBundleRegistryError(outResolverError, "Bundle asset refs must include an entry path: " + request);
                return false;
            }

            const std::string entryPath = CompressedFile::NormalizeEntryPath(normalizedRequest.substr(slashPos + 1));
            if(entryPath.empty()){
                setBundleRegistryError(outResolverError, "Bundle asset ref path is invalid: " + request);
                return false;
            }

            outResolved.cacheKey = cachePrefix + "/" + entryPath;
            outResolved.loader = [capturedBundle, entryPath]() -> std::shared_ptr<Asset> {
                return capturedBundle->loadAsset(entryPath, nullptr);
            };
            return true;
        }))
    {
        setBundleRegistryError(outError, "Failed to register asset alias provider for bundle: " + bundle->alias());
        return false;
    }

    bundlesByAliasKey[aliasKey] = bundle;
    aliasKeyByBundlePath[bundlePathKey] = aliasKey;
    AssetManager::Instance.unmanageAliasAssets(aliasToken);
    return true;
}

void AssetBundleRegistry::unmountBundle(const std::filesystem::path& path){
    const std::string bundlePathKey = NormalizeBundlePathKey(path);
    auto aliasIt = aliasKeyByBundlePath.find(bundlePathKey);
    if(aliasIt == aliasKeyByBundlePath.end()){
        return;
    }

    auto bundleIt = bundlesByAliasKey.find(aliasIt->second);
    if(bundleIt != bundlesByAliasKey.end() && bundleIt->second){
        AssetManager::Instance.unregisterAliasProvider(bundleIt->second->aliasToken());
        AssetManager::Instance.unmanageAliasAssets(bundleIt->second->aliasToken());
        bundlesByAliasKey.erase(bundleIt);
    }

    aliasKeyByBundlePath.erase(aliasIt);
}

std::shared_ptr<AssetBundle> AssetBundleRegistry::getBundle(const std::string& alias) const{
    const std::string aliasKey = NormalizeAliasKey(alias);
    auto it = bundlesByAliasKey.find(aliasKey);
    if(it == bundlesByAliasKey.end()){
        return nullptr;
    }
    return it->second;
}

std::shared_ptr<AssetBundle> AssetBundleRegistry::getBundleByPath(const std::filesystem::path& path) const{
    const std::string bundlePathKey = NormalizeBundlePathKey(path);
    auto aliasIt = aliasKeyByBundlePath.find(bundlePathKey);
    if(aliasIt == aliasKeyByBundlePath.end()){
        return nullptr;
    }

    auto bundleIt = bundlesByAliasKey.find(aliasIt->second);
    if(bundleIt == bundlesByAliasKey.end()){
        return nullptr;
    }
    return bundleIt->second;
}

std::shared_ptr<AssetBundle> AssetBundleRegistry::getBundleForAssetRef(const std::string& assetRef, std::string* outEntryPath) const{
    if(outEntryPath){
        outEntryPath->clear();
    }
    if(assetRef.empty() || assetRef[0] != '@'){
        return nullptr;
    }

    std::string normalized = StringUtils::ReplaceAll(assetRef.c_str(), "\\", "/");
    size_t slashPos = normalized.find('/');
    std::string alias = (slashPos == std::string::npos) ? normalized.substr(1) : normalized.substr(1, slashPos - 1);
    std::shared_ptr<AssetBundle> bundle = getBundle(alias);
    if(!bundle){
        return nullptr;
    }

    if(outEntryPath && slashPos != std::string::npos && slashPos + 1 < normalized.size()){
        *outEntryPath = CompressedFile::NormalizeEntryPath(normalized.substr(slashPos + 1));
    }

    return bundle;
}

bool AssetBundleRegistry::loadAsset(const std::string& assetRef, std::shared_ptr<Asset>& outAsset, std::string* outError) const{
    outAsset.reset();

    std::string entryPath;
    std::shared_ptr<AssetBundle> bundle = getBundleForAssetRef(assetRef, &entryPath);
    if(!bundle){
        setBundleRegistryError(outError, "No mounted asset bundle matched: " + assetRef);
        return false;
    }

    outAsset = bundle->loadAsset(entryPath, outError);
    return static_cast<bool>(outAsset);
}

size_t AssetBundleRegistry::scanKnownLocations(std::string* outError){
    clear();

    std::vector<std::filesystem::path> candidatePaths;
    std::set<std::string> seenPaths;

    auto collectBundles = [&](const std::filesystem::path& root, bool recursive){
        std::error_code ec;
        if(!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)){
            return;
        }

        if(recursive){
            for(const auto& entry : std::filesystem::recursive_directory_iterator(
                    root,
                    std::filesystem::directory_options::skip_permission_denied,
                    ec)){
                if(ec){
                    break;
                }
                if(!entry.is_regular_file(ec) || !AssetBundle::IsBundlePath(entry.path())){
                    continue;
                }

                const std::string key = NormalizeBundlePathKey(entry.path());
                if(seenPaths.insert(key).second){
                    candidatePaths.push_back(entry.path());
                }
            }
        }else{
            for(const auto& entry : std::filesystem::directory_iterator(
                    root,
                    std::filesystem::directory_options::skip_permission_denied,
                    ec)){
                if(ec){
                    break;
                }
                if(!entry.is_regular_file(ec) || !AssetBundle::IsBundlePath(entry.path())){
                    continue;
                }

                const std::string key = NormalizeBundlePathKey(entry.path());
                if(seenPaths.insert(key).second){
                    candidatePaths.push_back(entry.path());
                }
            }
        }
    };

    const std::filesystem::path exeDirectory = std::filesystem::path(File::GetCWD());
    collectBundles(exeDirectory, false);
    collectBundles(exeDirectory / "res", true);

    size_t mountedCount = 0;
    std::string firstError;
    for(const auto& candidate : candidatePaths){
        std::string mountError;
        if(mountBundle(candidate, &mountError)){
            ++mountedCount;
        }else{
            bundleRegistryLogger.Log(LOG_WARN, "Failed to mount bundle '%s': %s", candidate.generic_string().c_str(), mountError.c_str());
            if(firstError.empty()){
                firstError = mountError;
            }
        }
    }

    if(outError && !firstError.empty()){
        *outError = firstError;
    }

    return mountedCount;
}

std::filesystem::path AssetBundleRegistry::MakeVirtualEntryPath(
    const std::filesystem::path& bundlePath,
    const std::string& entryPath,
    bool isDirectory)
{
    const std::filesystem::path normalizedBundlePath = NormalizeBundleVirtualPath(bundlePath);
    std::filesystem::path virtualPath = std::filesystem::path(VirtualEntryPrefix()) /
                                        encodeVirtualSegment(normalizedBundlePath.generic_string());

    std::string normalizedEntry = isDirectory
        ? CompressedFile::NormalizeEntryPath(entryPath, true)
        : CompressedFile::NormalizeEntryPath(entryPath);
    if(normalizedEntry.empty()){
        return virtualPath.lexically_normal();
    }

    if(isDirectory && !normalizedEntry.empty() && normalizedEntry.back() == '/'){
        normalizedEntry.pop_back();
    }

    virtualPath /= std::filesystem::path(normalizedEntry);
    return virtualPath.lexically_normal();
}

bool AssetBundleRegistry::DecodeVirtualEntryPath(
    const std::filesystem::path& virtualPath,
    std::filesystem::path& outBundlePath,
    std::string& outEntryPath,
    bool* outIsDirectory)
{
    outBundlePath.clear();
    outEntryPath.clear();
    if(outIsDirectory){
        *outIsDirectory = false;
    }

    std::vector<std::string> segments;
    for(const auto& part : virtualPath.lexically_normal()){
        const std::string segment = part.generic_string();
        if(!segment.empty() && segment != "."){
            segments.push_back(segment);
        }
    }

    if(segments.size() < 2 || segments[0] != VirtualEntryPrefix()){
        return false;
    }

    std::string decodedBundlePath;
    if(!decodeVirtualSegment(segments[1], decodedBundlePath) || decodedBundlePath.empty()){
        return false;
    }

    outBundlePath = std::filesystem::path(decodedBundlePath).lexically_normal();
    if(segments.size() == 2){
        if(outIsDirectory){
            *outIsDirectory = true;
        }
        return true;
    }

    std::ostringstream joined;
    for(size_t i = 2; i < segments.size(); ++i){
        if(i > 2){
            joined << '/';
        }
        joined << segments[i];
    }

    const std::string candidateFile = CompressedFile::NormalizeEntryPath(joined.str());
    const std::string candidateDir = CompressedFile::NormalizeEntryPath(joined.str(), true);
    if(candidateFile.empty() && candidateDir.empty()){
        return false;
    }

    std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(outBundlePath);
    if(bundle){
        for(const auto& entry : bundle->getEntries()){
            if(!candidateDir.empty() && entry.path == candidateDir && entry.kind == "directory"){
                outEntryPath = candidateDir;
                if(outIsDirectory){
                    *outIsDirectory = true;
                }
                return true;
            }
        }
        for(const auto& entry : bundle->getEntries()){
            if(!candidateFile.empty() && entry.path == candidateFile){
                outEntryPath = candidateFile;
                if(outIsDirectory){
                    *outIsDirectory = (entry.kind == "directory");
                }
                return true;
            }
        }
    }

    outEntryPath = !candidateFile.empty() ? candidateFile : candidateDir;
    return true;
}

bool AssetBundleRegistry::IsVirtualEntryPath(const std::filesystem::path& path){
    std::vector<std::string> segments;
    for(const auto& part : path.lexically_normal()){
        const std::string segment = part.generic_string();
        if(!segment.empty() && segment != "."){
            segments.push_back(segment);
        }
    }
    return !segments.empty() && segments[0] == VirtualEntryPrefix();
}
