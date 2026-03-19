/**
 * @file src/Editor/Widgets/WorkspacePanel.cpp
 * @brief Implementation for WorkspacePanel.
 */

#include "Editor/Widgets/WorkspacePanel.h"

#include "Editor/Core/EditorAssetUI.h"
#include "Editor/Widgets/ECSViewPanel.h"
#include "Assets/Bundles/AssetBundleRegistry.h"
#include "Assets/Core/Asset.h"
#include "Assets/Core/AssetDescriptorUtils.h"
#include "Foundation/IO/File.h"
#include "Foundation/Logging/Logbot.h"
#include "Assets/Descriptors/EffectAsset.h"
#include "Assets/Descriptors/ImageAsset.h"
#include "Assets/Descriptors/MaterialAsset.h"
#include "Assets/Descriptors/LensFlareAsset.h"
#include "Assets/Descriptors/ModelAsset.h"
#include "Assets/Descriptors/SkyboxAsset.h"
#include "Assets/Descriptors/ShaderAsset.h"
#include "Assets/Bundles/AssetBundle.h"
#include "Foundation/Util/StringUtils.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace {
    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;
    constexpr int kWorkspaceDirectoryValidationIntervalFrames = 15;
    constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    std::string normalizedPathKey(const std::filesystem::path& path){
        // UI cache/map keys are used for stable identity within the current session and do
        // not require filesystem canonicalization. Avoid weakly_canonical() here because it
        // performs filesystem I/O (CreateFileW on Windows) and was showing up in render-thread
        // samples while the workspace panel is visible.
        std::filesystem::path normalized = path.lexically_normal();
        return StringUtils::ToLowerCase(normalized.generic_string());
    }

    bool assetRefToAbsolutePath(const std::string& assetRef, std::filesystem::path& outPath){
        return AssetDescriptorUtils::AssetRefToAbsolutePath(assetRef, outPath);
    }

    std::string absolutePathToAssetRef(const std::filesystem::path& absolutePath){
        return AssetDescriptorUtils::AbsolutePathToAssetRef(absolutePath);
    }

    bool pathExists(const std::filesystem::path& path, bool* outIsDirectory = nullptr){
        return AssetDescriptorUtils::PathExists(path, outIsDirectory);
    }

    void hashBytes(std::uint64_t& hash, const void* data, size_t size){
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for(size_t i = 0; i < size; ++i){
            hash ^= static_cast<std::uint64_t>(bytes[i]);
            hash *= kFnvPrime;
        }
    }

    void hashString(std::uint64_t& hash, const std::string& value){
        hashBytes(hash, value.data(), value.size());
    }

    void hashBool(std::uint64_t& hash, bool value){
        const unsigned char encoded = value ? 1U : 0U;
        hashBytes(hash, &encoded, sizeof(encoded));
    }

    void hashUnsigned64(std::uint64_t& hash, std::uint64_t value){
        hashBytes(hash, &value, sizeof(value));
    }

    std::uint64_t fileWriteStamp(const std::filesystem::path& path){
        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(path, ec);
        if(ec){
            return 0;
        }
        return static_cast<std::uint64_t>(writeTime.time_since_epoch().count());
    }

    std::uint64_t computeDirectorySnapshotHash(const std::filesystem::path& directory){
        std::error_code ec;
        if(!std::filesystem::exists(directory, ec) || ec){
            return 0xA5A5A5A5A5A5A5A5ull;
        }
        if(!std::filesystem::is_directory(directory, ec) || ec){
            return 0x5A5A5A5A5A5A5A5Aull;
        }

        struct DirectoryEntryFingerprint {
            std::string nameKey;
            bool isDirectory = false;
            std::uint64_t writeStamp = 0;
        };

        std::vector<DirectoryEntryFingerprint> entries;
        for(const auto& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, ec)){
            if(ec){
                break;
            }

            std::error_code entryEc;
            const std::filesystem::path entryPath = entry.path().lexically_normal();
            const bool isDirectory = entry.is_directory(entryEc);
            if(entryEc){
                continue;
            }

            DirectoryEntryFingerprint fingerprint;
            fingerprint.nameKey = StringUtils::ToLowerCase(entryPath.filename().generic_string());
            fingerprint.isDirectory = isDirectory;
            fingerprint.writeStamp = fileWriteStamp(entryPath);
            entries.push_back(fingerprint);
        }

        std::sort(entries.begin(), entries.end(), [](const DirectoryEntryFingerprint& a, const DirectoryEntryFingerprint& b){
            if(a.isDirectory != b.isDirectory){
                return a.isDirectory > b.isDirectory;
            }
            return a.nameKey < b.nameKey;
        });

        std::uint64_t hash = kFnvOffsetBasis;
        hashString(hash, normalizedPathKey(directory));
        hashUnsigned64(hash, static_cast<std::uint64_t>(entries.size()));
        for(const auto& entry : entries){
            hashString(hash, entry.nameKey);
            hashBool(hash, entry.isDirectory);
            hashUnsigned64(hash, entry.writeStamp);
        }
        return hash;
    }
}

WorkspacePanel::~WorkspacePanel(){
    if(assetChangeListenerHandle >= 0){
        AssetManager::Instance.removeChangeListener(assetChangeListenerHandle);
        assetChangeListenerHandle = -1;
    }
}

void WorkspacePanel::setAssetRoot(const std::filesystem::path& rootPath){
    if(assetRoot == rootPath){
        if(assetDir.empty()){
            assetDir = assetRoot;
            browserCacheDirty = true;
            resetExternalDirectoryTracking();
        }
        return;
    }
    assetRoot = rootPath;
    if(assetDir.empty()){
        assetDir = assetRoot;
    }
    browserCacheDirty = true;
    resetExternalDirectoryTracking();
}

void WorkspacePanel::ensureAssetChangeListenerRegistered(){
    if(assetChangeListenerHandle >= 0){
        return;
    }

    assetChangeListenerHandle = AssetManager::Instance.addChangeListener(
        [this](const AssetManager::AssetChangeEvent& event){
            handleAssetChanged(event.cacheKey);
        }
    );
}

void WorkspacePanel::handleAssetChanged(const std::string& cacheKey){
    if(cacheKey.empty() || assetDir.empty()){
        return;
    }

    const std::filesystem::path changedPath(cacheKey);
    const std::string currentDirKey = normalizedPathKey(assetDir);
    if(currentDirKey.empty()){
        return;
    }

    if(AssetBundleRegistry::IsVirtualEntryPath(assetDir)){
        std::filesystem::path bundlePath;
        std::string entryPath;
        if(AssetBundleRegistry::DecodeVirtualEntryPath(assetDir, bundlePath, entryPath) &&
           normalizedPathKey(bundlePath) == normalizedPathKey(changedPath)){
            browserCacheDirty = true;
        }
        return;
    }

    const std::string changedPathKey = normalizedPathKey(changedPath);
    const std::string changedParentKey = normalizedPathKey(changedPath.parent_path());
    if(changedPathKey == currentDirKey || changedParentKey == currentDirKey){
        browserCacheDirty = true;
        observedDirectorySnapshotValid = false;
    }
}

void WorkspacePanel::pollExternalDirectoryChanges(std::filesystem::path& selectedAssetPath){
    if(assetDir.empty() || AssetBundleRegistry::IsVirtualEntryPath(assetDir)){
        resetExternalDirectoryTracking();
        return;
    }

    const std::filesystem::path normalizedDir = assetDir.lexically_normal();
    const int frameNow = ImGui::GetFrameCount();
    if(!observedDirectorySnapshotValid || observedDirectoryPath != normalizedDir){
        observedDirectoryPath = normalizedDir;
        observedDirectorySnapshotHash = computeDirectorySnapshotHash(normalizedDir);
        observedDirectorySnapshotValid = true;
        lastExternalDirectoryValidationFrame = frameNow;
        return;
    }

    if((frameNow - lastExternalDirectoryValidationFrame) < kWorkspaceDirectoryValidationIntervalFrames){
        return;
    }
    lastExternalDirectoryValidationFrame = frameNow;

    const std::uint64_t snapshotHash = computeDirectorySnapshotHash(normalizedDir);
    if(snapshotHash == observedDirectorySnapshotHash){
        return;
    }

    observedDirectorySnapshotHash = snapshotHash;
    browserCacheDirty = true;

    bool currentDirIsDirectory = false;
    if(!pathExists(assetDir, &currentDirIsDirectory) || !currentDirIsDirectory){
        std::filesystem::path fallback = assetDir;
        while(!fallback.empty()){
            const std::filesystem::path parent = fallback.parent_path();
            if(parent.empty() || parent == fallback){
                break;
            }
            fallback = parent;

            bool fallbackIsDirectory = false;
            if(pathExists(fallback, &fallbackIsDirectory) && fallbackIsDirectory){
                assetDir = fallback;
                break;
            }
        }

        bool assetRootIsDirectory = false;
        if((assetDir.empty() || !pathExists(assetDir, &currentDirIsDirectory) || !currentDirIsDirectory) &&
           pathExists(assetRoot, &assetRootIsDirectory) && assetRootIsDirectory){
            assetDir = assetRoot;
        }

        observedDirectoryPath = assetDir.lexically_normal();
        observedDirectorySnapshotHash = computeDirectorySnapshotHash(observedDirectoryPath);
    }

    if(!selectedAssetPath.empty() &&
       !AssetBundleRegistry::IsVirtualEntryPath(selectedAssetPath) &&
       !pathExists(selectedAssetPath)){
        selectedAssetPath.clear();
    }
}

void WorkspacePanel::resetExternalDirectoryTracking(){
    lastExternalDirectoryValidationFrame = -100000;
    observedDirectoryPath.clear();
    observedDirectorySnapshotHash = 0;
    observedDirectorySnapshotValid = false;
}

void WorkspacePanel::beginAssetRename(const std::filesystem::path& path, std::filesystem::path& selectedAssetPath){
    if(path.empty()){
        return;
    }
    assetRenameActive = true;
    assetRenamePath = path;
    std::memset(assetRenameBuffer, 0, sizeof(assetRenameBuffer));
    const std::string name = path.filename().string();
    std::strncpy(assetRenameBuffer, name.c_str(), sizeof(assetRenameBuffer) - 1);
    assetRenameBuffer[sizeof(assetRenameBuffer) - 1] = '\0';
    assetRenameFocus = true;
    selectedAssetPath = path;
}

void WorkspacePanel::commitAssetRename(std::filesystem::path& selectedAssetPath){
    if(!assetRenameActive || assetRenamePath.empty()){
        cancelAssetRename();
        return;
    }

    std::string requestedName = StringUtils::Trim(assetRenameBuffer);
    if(requestedName.empty()){
        cancelAssetRename();
        return;
    }

    std::filesystem::path oldPath = assetRenamePath;
    const bool oldPathIsMaterialObject = MaterialAssetIO::IsMaterialObjectPath(oldPath);
    const bool oldPathIsMaterialAsset = MaterialAssetIO::IsMaterialAssetPath(oldPath);
    const bool oldPathIsSkyboxAsset = SkyboxAssetIO::IsSkyboxAssetPath(oldPath);
    const bool oldPathIsLensFlareAsset = LensFlareAssetIO::IsLensFlareAssetPath(oldPath);
    const bool oldPathIsEffectAsset = EffectAssetIO::IsEffectAssetPath(oldPath);
    const bool oldPathIsImageAsset = ImageAssetIO::IsImageAssetPath(oldPath);
    const bool oldPathIsBundleAsset = AssetBundle::IsBundlePath(oldPath);

    std::string normalizedNewName = requestedName;
    std::string normalizedNewNameLower = StringUtils::ToLowerCase(normalizedNewName);
    auto ensureSuffix = [&](const std::string& suffix){
        const std::string suffixLower = StringUtils::ToLowerCase(suffix);
        if(!StringUtils::EndsWith(normalizedNewNameLower, suffixLower)){
            normalizedNewName += suffix;
            normalizedNewNameLower = StringUtils::ToLowerCase(normalizedNewName);
        }
    };

    if(oldPathIsMaterialObject){
        // Keep material object extension even if the user types only a base name.
        ensureSuffix(".material");
    }else if(oldPathIsMaterialAsset){
        const bool hasKnownMaterialAssetSuffix =
            StringUtils::EndsWith(normalizedNewNameLower, ".mat.asset") ||
            StringUtils::EndsWith(normalizedNewNameLower, ".material.asset");
        if(!hasKnownMaterialAssetSuffix){
            const std::string oldFileNameLower = StringUtils::ToLowerCase(oldPath.filename().string());
            if(StringUtils::EndsWith(oldFileNameLower, ".material.asset")){
                ensureSuffix(".material.asset");
            }else{
                ensureSuffix(".mat.asset");
            }
        }
    }else if(oldPathIsSkyboxAsset){
        ensureSuffix(".skybox.asset");
    }else if(oldPathIsLensFlareAsset){
        ensureSuffix(".flare.asset");
    }else if(oldPathIsEffectAsset){
        ensureSuffix(".effect.asset");
    }else if(oldPathIsImageAsset){
        ensureSuffix(".image.asset");
    }else if(oldPathIsBundleAsset){
        ensureSuffix(".bundle.asset");
    }

    std::filesystem::path newPath = oldPath.parent_path() / normalizedNewName;
    if(newPath == oldPath){
        cancelAssetRename();
        return;
    }

    const bool oldPathIsVirtual = AssetBundleRegistry::IsVirtualEntryPath(oldPath);
    bool oldPathIsDirectory = false;
    pathExists(oldPath, &oldPathIsDirectory);

    auto retargetPath = [&](std::filesystem::path& candidate){
        if(candidate.empty()){
            return;
        }

        if(candidate == oldPath){
            candidate = newPath;
            return;
        }

        if(!oldPathIsDirectory){
            return;
        }

        const std::filesystem::path relative = candidate.lexically_relative(oldPath);
        if(relative.empty()){
            return;
        }

        auto firstSegment = relative.begin();
        if(firstSegment == relative.end()){
            return;
        }

        const std::string firstName = firstSegment->generic_string();
        if(firstName == "." || firstName == ".."){
            return;
        }

        candidate = (newPath / relative).lexically_normal();
    };

    std::filesystem::path oldLinkedPath;
    bool hasOldLinkedPath = false;
    if(oldPathIsMaterialObject){
        std::string oldLinkedAssetRef;
        std::string linkResolveError;
        if(MaterialAssetIO::ResolveMaterialAssetRef(oldPath.generic_string(), oldLinkedAssetRef, &linkResolveError)){
            if(assetRefToAbsolutePath(oldLinkedAssetRef, oldLinkedPath)){
                std::error_code linkedEc;
                std::filesystem::path normalizedLinked = std::filesystem::weakly_canonical(oldLinkedPath, linkedEc);
                oldLinkedPath = linkedEc ? oldLinkedPath.lexically_normal() : normalizedLinked;
                hasOldLinkedPath = true;
            }
        }
    }

    if(oldPathIsVirtual){
        std::filesystem::path bundlePath;
        std::string oldEntryPath;
        if(!AssetBundleRegistry::DecodeVirtualEntryPath(oldPath, bundlePath, oldEntryPath)){
            LogBot.Log(LOG_ERRO, "Rename failed: invalid virtual bundle path (%s).", oldPath.string().c_str());
            assetRenameFocus = true;
            return;
        }

        std::filesystem::path newBundlePath;
        std::string newEntryPath;
        if(!AssetBundleRegistry::DecodeVirtualEntryPath(newPath, newBundlePath, newEntryPath) ||
           normalizedPathKey(newBundlePath) != normalizedPathKey(bundlePath)){
            LogBot.Log(LOG_ERRO, "Rename failed: bundle entry rename must stay within the same bundle.");
            assetRenameFocus = true;
            return;
        }

        bool targetIsDirectory = false;
        if(pathExists(newPath, &targetIsDirectory)){
            LogBot.Log(LOG_WARN, "Rename cancelled: target already exists (%s).", newPath.string().c_str());
            assetRenameFocus = true;
            return;
        }

        std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
        if(!bundle){
            LogBot.Log(LOG_ERRO, "Rename failed: bundle is no longer mounted (%s).", bundlePath.string().c_str());
            assetRenameFocus = true;
            return;
        }

        std::string renameError;
        if(!bundle->renameEntry(oldEntryPath, newEntryPath, &renameError) ||
           !bundle->save(&renameError)){
            LogBot.Log(LOG_ERRO,
                       "Rename failed (%s -> %s): %s",
                       oldPath.string().c_str(),
                       newPath.string().c_str(),
                       renameError.c_str());
            assetRenameFocus = true;
            return;
        }

        AssetManager::Instance.unmanageAliasAssets(bundle->aliasToken());

        retargetPath(assetDir);
        retargetPath(selectedAssetPath);
        retargetPath(clipboardAssetPath);
        retargetPath(contextMenuTargetPath);
        retargetPath(pickerContextMenuTargetPath);
        retargetPath(pendingDeleteAssetPath);

        if(oldPathIsMaterialObject){
            MaterialObjectData objectData;
            std::string objectError;
            if(MaterialAssetIO::LoadMaterialObjectFromAbsolutePath(newPath, objectData, &objectError)){
                objectData.name = newPath.stem().string();
                if(!MaterialAssetIO::SaveMaterialObjectToAbsolutePath(newPath, objectData, &objectError)){
                    LogBot.Log(LOG_WARN,
                               "Failed to update material object after rename (%s): %s",
                               newPath.string().c_str(),
                               objectError.c_str());
                }
            }else{
                LogBot.Log(LOG_WARN,
                           "Failed to reload renamed material object (%s): %s",
                           newPath.string().c_str(),
                           objectError.c_str());
            }

            if(hasOldLinkedPath){
                MaterialAssetData linkedData;
                std::string linkedDataError;
                if(MaterialAssetIO::LoadFromAbsolutePath(oldLinkedPath, linkedData, &linkedDataError)){
                    linkedData.linkParentRef = absolutePathToAssetRef(newPath);
                    if(!MaterialAssetIO::SaveToAbsolutePath(oldLinkedPath, linkedData, &linkedDataError)){
                        LogBot.Log(LOG_WARN,
                                   "Failed to update material asset link parent after rename (%s): %s",
                                   oldLinkedPath.string().c_str(),
                                   linkedDataError.c_str());
                    }
                }else{
                    LogBot.Log(LOG_WARN,
                               "Failed to load linked material asset for parent update (%s): %s",
                               oldLinkedPath.string().c_str(),
                               linkedDataError.c_str());
                }
            }
        }

        EditorAssetUI::InvalidateAllThumbnails();
        browserCacheDirty = true;
        cancelAssetRename();
        return;
    }

    std::error_code ec;
    if(std::filesystem::exists(newPath, ec)){
        LogBot.Log(LOG_WARN, "Rename cancelled: target already exists (%s).", newPath.string().c_str());
        assetRenameFocus = true;
        return;
    }

    std::filesystem::rename(oldPath, newPath, ec);
    if(ec){
        LogBot.Log(LOG_ERRO, "Rename failed (%s -> %s): %s", oldPath.string().c_str(), newPath.string().c_str(), ec.message().c_str());
        assetRenameFocus = true;
        return;
    }

    retargetPath(assetDir);
    retargetPath(selectedAssetPath);
    retargetPath(clipboardAssetPath);
    retargetPath(contextMenuTargetPath);
    retargetPath(pickerContextMenuTargetPath);
    retargetPath(pendingDeleteAssetPath);

    if(oldPathIsMaterialObject){
        std::filesystem::path resolvedLinkedPath = oldLinkedPath;
        if(hasOldLinkedPath){
            const std::string oldBaseName = oldPath.stem().string();
            const std::string newBaseName = newPath.stem().string();
            const std::string linkedFileNameLower = StringUtils::ToLowerCase(oldLinkedPath.filename().string());

            std::string linkedSuffix;
            if(linkedFileNameLower == StringUtils::ToLowerCase(oldBaseName + ".mat.asset")){
                linkedSuffix = ".mat.asset";
            }else if(linkedFileNameLower == StringUtils::ToLowerCase(oldBaseName + ".material.asset")){
                linkedSuffix = ".material.asset";
            }

            if(!linkedSuffix.empty()){
                std::filesystem::path desiredLinkedPath = oldLinkedPath.parent_path() / (newBaseName + linkedSuffix);
                if(desiredLinkedPath != oldLinkedPath){
                    std::error_code linkedRenameEc;
                    if(std::filesystem::exists(desiredLinkedPath, linkedRenameEc)){
                        LogBot.Log(LOG_WARN, "Linked material asset rename skipped: target exists (%s).", desiredLinkedPath.string().c_str());
                    }else{
                        linkedRenameEc.clear();
                        std::filesystem::rename(oldLinkedPath, desiredLinkedPath, linkedRenameEc);
                        if(linkedRenameEc){
                            LogBot.Log(LOG_WARN,
                                       "Linked material asset rename failed (%s -> %s): %s",
                                       oldLinkedPath.string().c_str(),
                                       desiredLinkedPath.string().c_str(),
                                       linkedRenameEc.message().c_str());
                        }else{
                            resolvedLinkedPath = desiredLinkedPath;
                            if(selectedAssetPath == oldLinkedPath){
                                selectedAssetPath = desiredLinkedPath;
                            }
                        }
                    }
                }
            }
        }

        MaterialObjectData objectData;
        std::string objectError;
        if(MaterialAssetIO::LoadMaterialObjectFromAbsolutePath(newPath, objectData, &objectError)){
            objectData.name = newPath.stem().string();

            if(!resolvedLinkedPath.empty()){
                std::string linkedAssetRef;
                std::string linkedRefError;
                if(MaterialAssetIO::ResolveMaterialAssetRef(resolvedLinkedPath.generic_string(), linkedAssetRef, &linkedRefError)){
                    objectData.materialAssetRef = linkedAssetRef;
                }else{
                    LogBot.Log(LOG_WARN,
                               "Failed to update material reference after rename (%s): %s",
                               resolvedLinkedPath.string().c_str(),
                               linkedRefError.c_str());
                }
            }

            if(!MaterialAssetIO::SaveMaterialObjectToAbsolutePath(newPath, objectData, &objectError)){
                LogBot.Log(LOG_WARN, "Failed to update material object after rename (%s): %s", newPath.string().c_str(), objectError.c_str());
            }
        }else{
            LogBot.Log(LOG_WARN, "Failed to reload renamed material object (%s): %s", newPath.string().c_str(), objectError.c_str());
        }

        if(!resolvedLinkedPath.empty()){
            MaterialAssetData linkedData;
            std::string linkedDataError;
            if(MaterialAssetIO::LoadFromAbsolutePath(resolvedLinkedPath, linkedData, &linkedDataError)){
                linkedData.linkParentRef = absolutePathToAssetRef(newPath);
                if(!MaterialAssetIO::SaveToAbsolutePath(resolvedLinkedPath, linkedData, &linkedDataError)){
                    LogBot.Log(LOG_WARN,
                               "Failed to update material asset link parent after rename (%s): %s",
                               resolvedLinkedPath.string().c_str(),
                               linkedDataError.c_str());
                }
            }else{
                LogBot.Log(LOG_WARN,
                           "Failed to load linked material asset for parent update (%s): %s",
                           resolvedLinkedPath.string().c_str(),
                           linkedDataError.c_str());
            }
        }
    }

    if(oldPathIsBundleAsset){
        AssetBundleRegistry::Instance.unmountBundle(oldPath);
        std::string mountError;
        AssetBundleRegistry::Instance.mountBundle(newPath, &mountError);
        if(!mountError.empty()){
            LogBot.Log(LOG_WARN, "Renamed bundle but failed to mount new path: %s", mountError.c_str());
        }
    }

    EditorAssetUI::InvalidateAllThumbnails();
    browserCacheDirty = true;
    cancelAssetRename();
}

void WorkspacePanel::cancelAssetRename(){
    assetRenameActive = false;
    assetRenamePath.clear();
    assetRenameFocus = false;
    std::memset(assetRenameBuffer, 0, sizeof(assetRenameBuffer));
}

std::filesystem::path WorkspacePanel::makeUniquePathWithSuffix(const std::filesystem::path& desiredPath, const std::string& suffix) const{
    if(!pathExists(desiredPath)){
        return desiredPath;
    }

    const std::filesystem::path parent = desiredPath.parent_path();
    std::string stem = desiredPath.stem().string();
    if(!suffix.empty() && StringUtils::EndsWith(StringUtils::ToLowerCase(desiredPath.filename().string()), StringUtils::ToLowerCase(suffix))){
        const std::string fileName = desiredPath.filename().string();
        if(fileName.size() > suffix.size()){
            stem = fileName.substr(0, fileName.size() - suffix.size());
        }
    }

    int suffixIndex = 1;
    std::filesystem::path candidate;
    do{
        candidate = parent / (stem + "_" + std::to_string(suffixIndex) + suffix);
        suffixIndex++;
    }while(pathExists(candidate));
    return candidate;
}

bool WorkspacePanel::createMaterialWithLinkedAsset(const std::filesystem::path& materialPath, std::filesystem::path& outMaterialAssetPath, std::string* outError) const{
    const std::string lower = StringUtils::ToLowerCase(materialPath.filename().string());
    if(!StringUtils::EndsWith(lower, ".material")){
        if(outError){
            *outError = "Material file must end with .material";
        }
        return false;
    }

    const std::string baseName = materialPath.stem().string();
    std::filesystem::path materialAssetPath = materialPath.parent_path() / (baseName + ".mat.asset");

    MaterialAssetData data;
    data.name = materialAssetPath.filename().string();
    data.linkParentRef = absolutePathToAssetRef(materialPath);
    data.type = MaterialAssetType::PBR;
    // Use default built-in PBR material shader path (no extra .shader.asset generation).
    data.shaderAssetRef = "";
    data.color = Color::WHITE;
    data.useEnvMap = 1;
    data.envStrength = 1.0f;
    if(!MaterialAssetIO::SaveToAbsolutePath(materialAssetPath, data, outError)){
        return false;
    }

    std::string materialAssetRef;
    if(!MaterialAssetIO::ResolveMaterialAssetRef(materialAssetPath.generic_string(), materialAssetRef, outError)){
        return false;
    }

    MaterialObjectData objectData;
    objectData.name = baseName;
    objectData.materialAssetRef = materialAssetRef;
    if(!MaterialAssetIO::SaveMaterialObjectToAbsolutePath(materialPath, objectData, outError)){
        return false;
    }

    outMaterialAssetPath = materialAssetPath;
    return true;
}

void WorkspacePanel::draw(float x,
                          float y,
                          float w,
                          float h,
                          std::filesystem::path& selectedAssetPath,
                          const EntityDropToPrefabFn& onEntityDropToPrefab){
    if(assetRoot.empty()){
        assetRoot = std::filesystem::path(File::GetCWD()) / "res";
    }
    if(assetDir.empty()){
        assetDir = assetRoot;
    }

    ensureAssetChangeListenerRegistered();
    pollExternalDirectoryChanges(selectedAssetPath);

    /// @brief Represents Browser Entry data.
    struct BrowserEntry{
        std::filesystem::path path;
        bool isDirectory = false;
        bool isUp = false;
        bool isRelated = false;
        bool forceDataIcon = false;
        std::string parentKey;
        std::string normalizedKey;
    };
    using LinkedChildrenMap = std::unordered_map<std::string, std::vector<std::filesystem::path>>;

    // Editor UI was rebuilding this browser tree every frame (directory scan + sort + link resolution).
    // Cache it briefly; this keeps the panel responsive while avoiding constant filesystem churn.
    static std::vector<BrowserEntry> s_browserEntriesCache;
    static LinkedChildrenMap s_browserLinkedCache;
    static std::filesystem::path s_browserCacheAssetRootPath;
    static std::filesystem::path s_browserCacheAssetDirPath;

    auto resolveLinkedMaterialAssetPath = [&](const std::filesystem::path& materialPath, std::filesystem::path& outLinkedPath) -> bool{
        std::string resolvedAssetRef;
        std::string error;
        if(!MaterialAssetIO::ResolveMaterialAssetRef(materialPath.generic_string(), resolvedAssetRef, &error)){
            return false;
        }

        std::filesystem::path linkedAbsolutePath;
        if(!assetRefToAbsolutePath(resolvedAssetRef, linkedAbsolutePath)){
            return false;
        }

        std::error_code ec;
        std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(linkedAbsolutePath, ec);
        if(ec){
            normalizedPath = linkedAbsolutePath.lexically_normal();
        }
        bool isDirectory = false;
        if(!pathExists(normalizedPath, &isDirectory) || isDirectory){
            return false;
        }

        outLinkedPath = normalizedPath;
        return true;
    };

    auto collectBrowserEntries = [&](std::vector<BrowserEntry>& outEntries,
                                     LinkedChildrenMap& outLinkedByParent){
        outEntries.clear();
        outLinkedByParent.clear();

        std::filesystem::path bundlePath;
        std::string bundleEntryPath;
        bool bundleDirectory = false;
        if(AssetBundleRegistry::DecodeVirtualEntryPath(assetDir, bundlePath, bundleEntryPath, &bundleDirectory)){
            std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
            if(!bundle || !bundleDirectory){
                return;
            }

            std::string currentDir = bundleEntryPath;
            if(!currentDir.empty() && currentDir.back() == '/'){
                currentDir.pop_back();
            }

            std::vector<BrowserEntry> baseEntries;
            if(currentDir.empty()){
                baseEntries.push_back({bundlePath.parent_path(), true, true, false, false, ""});
            }else{
                const std::string parentDir = std::filesystem::path(currentDir).parent_path().generic_string();
                baseEntries.push_back({
                    AssetBundleRegistry::MakeVirtualEntryPath(bundlePath, parentDir, true),
                    true,
                    true,
                    false,
                    false,
                    ""
                });
            }

            for(const auto& entry : bundle->getEntries()){
                const bool isDirectory = (entry.kind == "directory");
                std::string displayPath = entry.path;
                if(isDirectory && !displayPath.empty() && displayPath.back() == '/'){
                    displayPath.pop_back();
                }
                if(displayPath.empty()){
                    continue;
                }

                const std::string parentDir = std::filesystem::path(displayPath).parent_path().generic_string();
                if(currentDir.empty()){
                    if(!parentDir.empty() && parentDir != "."){
                        continue;
                    }
                }else if(parentDir != currentDir){
                    continue;
                }

                BrowserEntry browserEntry;
                browserEntry.path = AssetBundleRegistry::MakeVirtualEntryPath(bundlePath, entry.path, isDirectory);
                browserEntry.isDirectory = isDirectory;
                baseEntries.push_back(browserEntry);
            }

            std::sort(baseEntries.begin(), baseEntries.end(), [](const BrowserEntry& a, const BrowserEntry& b){
                if(a.isUp != b.isUp){
                    return a.isUp;
                }
                if(a.isDirectory != b.isDirectory){
                    return a.isDirectory > b.isDirectory;
                }
                const std::string aLabel = a.isUp ? std::string("..") : a.path.filename().string();
                const std::string bLabel = b.isUp ? std::string("..") : b.path.filename().string();
                return aLabel < bLabel;
            });

            for(auto& baseEntry : baseEntries){
                if(baseEntry.normalizedKey.empty()){
                    baseEntry.normalizedKey = normalizedPathKey(baseEntry.path);
                }
            }

            outEntries = std::move(baseEntries);
            return;
        }

        std::error_code ec;
        if(!std::filesystem::exists(assetDir, ec)){
            return;
        }

        std::vector<BrowserEntry> baseEntries;
        if(assetDir != assetRoot){
            baseEntries.push_back({assetDir.parent_path(), true, true, false, false, ""});
        }

        for(const auto& entry : std::filesystem::directory_iterator(assetDir, std::filesystem::directory_options::skip_permission_denied, ec)){
            baseEntries.push_back({entry.path(), entry.is_directory(), false, false, false, ""});
        }

        std::sort(baseEntries.begin(), baseEntries.end(), [](const BrowserEntry& a, const BrowserEntry& b){
            if(a.isUp != b.isUp){
                return a.isUp;
            }
            if(a.isDirectory != b.isDirectory){
                return a.isDirectory > b.isDirectory;
            }
            const std::string aLabel = a.isUp ? std::string("..") : a.path.filename().string();
            const std::string bLabel = b.isUp ? std::string("..") : b.path.filename().string();
            return aLabel < bLabel;
        });
        for(auto& baseEntry : baseEntries){
            if(baseEntry.normalizedKey.empty()){
                baseEntry.normalizedKey = normalizedPathKey(baseEntry.path);
            }
        }

        const std::string assetDirKey = normalizedPathKey(assetDir);
        std::unordered_set<std::string> hiddenRelatedAssetKeys;
        auto addLinkedChild = [&](const std::string& parentKey, const std::filesystem::path& childPath){
            if(parentKey.empty()){
                return;
            }
            auto& linkedChildren = outLinkedByParent[parentKey];
            const std::string childKey = normalizedPathKey(childPath);
            for(const auto& existing : linkedChildren){
                if(normalizedPathKey(existing) == childKey){
                    return;
                }
            }
            linkedChildren.push_back(childPath);
        };

        for(const auto& entry : baseEntries){
            if(entry.isUp || entry.isDirectory){
                continue;
            }

            const std::string lowerName = StringUtils::ToLowerCase(entry.path.filename().string());
            if(!StringUtils::EndsWith(lowerName, ".material")){
                continue;
            }

            std::filesystem::path linkedPath;
            if(!resolveLinkedMaterialAssetPath(entry.path, linkedPath)){
                continue;
            }

            if(normalizedPathKey(linkedPath.parent_path()) != assetDirKey){
                continue;
            }

            const std::string parentKey = normalizedPathKey(entry.path);
            addLinkedChild(parentKey, linkedPath);

            const std::string linkedKey = normalizedPathKey(linkedPath);
            if(!linkedKey.empty()){
                hiddenRelatedAssetKeys.insert(linkedKey);
            }
        }

        // Support persisted child -> parent material links so existing assets stay grouped
        // even if the .material file reference is stale.
        for(const auto& entry : baseEntries){
            if(entry.isUp || entry.isDirectory){
                continue;
            }
            if(!MaterialAssetIO::IsMaterialAssetPath(entry.path)){
                continue;
            }

            MaterialAssetData linkedMaterialData;
            std::string linkedLoadError;
            if(!MaterialAssetIO::LoadFromAbsolutePath(entry.path, linkedMaterialData, &linkedLoadError)){
                continue;
            }

            const std::string parentRef = StringUtils::Trim(linkedMaterialData.linkParentRef);
            if(parentRef.empty()){
                continue;
            }

            std::filesystem::path parentPath;
            if(!assetRefToAbsolutePath(parentRef, parentPath)){
                continue;
            }

            std::error_code parentEc;
            std::filesystem::path normalizedParentPath = std::filesystem::weakly_canonical(parentPath, parentEc);
            if(parentEc){
                normalizedParentPath = parentPath.lexically_normal();
            }
            if(!std::filesystem::exists(normalizedParentPath, parentEc) || std::filesystem::is_directory(normalizedParentPath, parentEc)){
                continue;
            }
            if(normalizedPathKey(normalizedParentPath.parent_path()) != assetDirKey){
                continue;
            }

            const std::string parentKey = normalizedPathKey(normalizedParentPath);
            if(parentKey.empty()){
                continue;
            }

            addLinkedChild(parentKey, entry.path);
            const std::string linkedKey = normalizedPathKey(entry.path);
            if(!linkedKey.empty()){
                hiddenRelatedAssetKeys.insert(linkedKey);
            }
        }

        // Support persisted child -> parent model links for .model.asset wrappers.
        for(const auto& entry : baseEntries){
            if(entry.isUp || entry.isDirectory){
                continue;
            }
            if(!ModelAssetIO::IsModelAssetPath(entry.path)){
                continue;
            }

            ModelAssetData modelData;
            std::string modelLoadError;
            if(!ModelAssetIO::LoadFromAbsolutePath(entry.path, modelData, &modelLoadError)){
                continue;
            }

            const std::string parentRef = StringUtils::Trim(modelData.linkParentRef);
            if(parentRef.empty()){
                continue;
            }

            std::filesystem::path parentPath;
            if(!assetRefToAbsolutePath(parentRef, parentPath)){
                continue;
            }

            std::error_code parentEc;
            std::filesystem::path normalizedParentPath = std::filesystem::weakly_canonical(parentPath, parentEc);
            if(parentEc){
                normalizedParentPath = parentPath.lexically_normal();
            }
            if(!std::filesystem::exists(normalizedParentPath, parentEc) || std::filesystem::is_directory(normalizedParentPath, parentEc)){
                continue;
            }
            if(normalizedPathKey(normalizedParentPath.parent_path()) != assetDirKey){
                continue;
            }

            const std::string parentKey = normalizedPathKey(normalizedParentPath);
            if(parentKey.empty()){
                continue;
            }

            addLinkedChild(parentKey, entry.path);
            const std::string linkedKey = normalizedPathKey(entry.path);
            if(!linkedKey.empty()){
                hiddenRelatedAssetKeys.insert(linkedKey);
            }
        }

        // Support persisted child -> parent image links for .image.asset wrappers.
        for(const auto& entry : baseEntries){
            if(entry.isUp || entry.isDirectory){
                continue;
            }
            if(!ImageAssetIO::IsImageAssetPath(entry.path)){
                continue;
            }

            ImageAssetData imageData;
            std::string imageLoadError;
            if(!ImageAssetIO::LoadFromAbsolutePath(entry.path, imageData, &imageLoadError)){
                continue;
            }

            const std::string parentRef = StringUtils::Trim(imageData.linkParentRef);
            if(parentRef.empty()){
                continue;
            }

            std::filesystem::path parentPath;
            if(!assetRefToAbsolutePath(parentRef, parentPath)){
                continue;
            }

            std::error_code parentEc;
            std::filesystem::path normalizedParentPath = std::filesystem::weakly_canonical(parentPath, parentEc);
            if(parentEc){
                normalizedParentPath = parentPath.lexically_normal();
            }
            if(!std::filesystem::exists(normalizedParentPath, parentEc) || std::filesystem::is_directory(normalizedParentPath, parentEc)){
                continue;
            }
            if(normalizedPathKey(normalizedParentPath.parent_path()) != assetDirKey){
                continue;
            }

            const std::string parentKey = normalizedPathKey(normalizedParentPath);
            if(parentKey.empty()){
                continue;
            }

            addLinkedChild(parentKey, entry.path);
            const std::string linkedKey = normalizedPathKey(entry.path);
            if(!linkedKey.empty()){
                hiddenRelatedAssetKeys.insert(linkedKey);
            }
        }

        for(const auto& entry : baseEntries){
            const std::string& entryKey = entry.normalizedKey;
            if(!entry.isUp && !entry.isDirectory && hiddenRelatedAssetKeys.find(entryKey) != hiddenRelatedAssetKeys.end()){
                continue;
            }

            outEntries.push_back(entry);

            if(entry.isUp || entry.isDirectory){
                continue;
            }

            auto linkedIt = outLinkedByParent.find(entryKey);
            if(linkedIt == outLinkedByParent.end()){
                continue;
            }

            if(expandedRelatedAssets.find(entryKey) == expandedRelatedAssets.end()){
                continue;
            }

            std::vector<std::filesystem::path> linkedChildren = linkedIt->second;
            std::sort(linkedChildren.begin(), linkedChildren.end(), [](const std::filesystem::path& a, const std::filesystem::path& b){
                return a.filename().string() < b.filename().string();
            });

            for(const auto& linkedPath : linkedChildren){
                BrowserEntry relatedEntry;
                relatedEntry.path = linkedPath;
                relatedEntry.isRelated = true;
                relatedEntry.forceDataIcon = false;
                relatedEntry.parentKey = entryKey;
                relatedEntry.normalizedKey = normalizedPathKey(linkedPath);
                outEntries.push_back(relatedEntry);
            }
        }
    };

    auto getBrowserEntriesCached = [&](const std::vector<BrowserEntry>*& outEntries,
                                       const LinkedChildrenMap*& outLinkedByParent,
                                       bool forceRefresh = false){
        // Avoid per-frame normalized string cache-key building, but still rebuild when the
        // visible directory/root changes.
        const bool cachePathChanged =
            (s_browserCacheAssetRootPath != assetRoot) ||
            (s_browserCacheAssetDirPath != assetDir);
        if(forceRefresh || browserCacheDirty || s_browserEntriesCache.empty() || cachePathChanged){
            collectBrowserEntries(s_browserEntriesCache, s_browserLinkedCache);
            s_browserCacheAssetRootPath = assetRoot;
            s_browserCacheAssetDirPath = assetDir;
            browserCacheDirty = false;
            if(AssetBundleRegistry::IsVirtualEntryPath(assetDir)){
                resetExternalDirectoryTracking();
            }else{
                observedDirectoryPath = assetDir.lexically_normal();
                observedDirectorySnapshotHash = computeDirectorySnapshotHash(observedDirectoryPath);
                observedDirectorySnapshotValid = true;
                lastExternalDirectoryValidationFrame = ImGui::GetFrameCount();
            }
        }
        outEntries = &s_browserEntriesCache;
        outLinkedByParent = &s_browserLinkedCache;
    };

    auto drawRelatedToggle = [&](float iconSize, bool expanded) -> bool{
        ImVec2 tileMin = ImGui::GetItemRectMin();
        ImVec2 tileMax = ImGui::GetItemRectMax();
        const float buttonSize = std::clamp(iconSize * 0.22f, 14.0f, 18.0f);
        ImVec2 buttonMin(tileMax.x - buttonSize - 4.0f, tileMin.y + 4.0f);
        ImVec2 buttonMax(buttonMin.x + buttonSize, buttonMin.y + buttonSize);

        bool hovered = ImGui::IsMouseHoveringRect(buttonMin, buttonMax, false);
        bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 bg = hovered ? IM_COL32(92, 92, 92, 240) : IM_COL32(58, 58, 58, 224);
        drawList->AddRectFilled(buttonMin, buttonMax, bg, 3.0f);
        drawList->AddRect(buttonMin, buttonMax, IM_COL32(22, 22, 22, 255), 3.0f, 0, 1.0f);

        const char* symbol = expanded ? "-" : "+";
        ImVec2 textSize = ImGui::CalcTextSize(symbol);
        ImVec2 textPos(
            buttonMin.x + ((buttonSize - textSize.x) * 0.5f),
            buttonMin.y + ((buttonSize - textSize.y) * 0.5f) - 0.5f
        );
        drawList->AddText(textPos, IM_COL32(236, 236, 236, 255), symbol);

        return clicked;
    };

    auto truncateAssetLabel = [&](const std::string& label) -> std::string{
        constexpr size_t kMaxLabelChars = 10;
        constexpr size_t kKeepChars = 7;
        if(label.size() <= kMaxLabelChars){
            return label;
        }
        return label.substr(0, kKeepChars) + "...";
    };
    constexpr float kLinkedDrawerChildTileOffset = 8.0f;

    auto formatEntryLabel = [&](const BrowserEntry& entry) -> std::string{
        if(entry.isUp){
            return "..";
        }

        std::string label = entry.path.filename().string();
        const std::string lowerName = StringUtils::ToLowerCase(label);
        if(StringUtils::EndsWith(lowerName, ".material") && !StringUtils::EndsWith(lowerName, ".material.asset")){
            label = entry.path.stem().string();
        }else if(StringUtils::EndsWith(lowerName, ".material.asset")){
            label = label.substr(0, label.size() - std::strlen(".material.asset"));
        }else if(StringUtils::EndsWith(lowerName, ".mat.asset")){
            label = label.substr(0, label.size() - std::strlen(".mat.asset"));
        }else if(StringUtils::EndsWith(lowerName, ".model.asset")){
            label = label.substr(0, label.size() - std::strlen(".model.asset"));
        }else if(StringUtils::EndsWith(lowerName, ".shader.asset")){
            label = label.substr(0, label.size() - std::strlen(".shader.asset"));
        }else if(StringUtils::EndsWith(lowerName, ".effect.asset")){
            label = label.substr(0, label.size() - std::strlen(".effect.asset"));
        }else if(StringUtils::EndsWith(lowerName, ".image.asset")){
            label = label.substr(0, label.size() - std::strlen(".image.asset"));
        }else if(StringUtils::EndsWith(lowerName, ".bundle.asset")){
            label = label.substr(0, label.size() - std::strlen(".bundle.asset"));
        }

        return truncateAssetLabel(label);
    };

    auto drawLinkedDrawerGroupBackdrop = [&](const BrowserEntry& entry,
                                             bool parentExpanded,
                                             size_t childCount,
                                             int currentColumn,
                                             int totalColumns,
                                             float tileSize,
                                             float cellWidth){
        if(entry.isRelated || !parentExpanded || childCount == 0 || totalColumns <= 0){
            return;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImDrawList* overlayDrawList = ImGui::GetForegroundDrawList();
        if(!drawList || !overlayDrawList){
            return;
        }

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const int remainingColumns = std::max(0, totalColumns - currentColumn);
        const int visibleChildCount = std::min<int>(static_cast<int>(childCount), std::max(0, remainingColumns - 1));
        const int spanCells = 1 + visibleChildCount;
        if(spanCells <= 1){
            return;
        }

        const float lastVisibleChildRight =
            cursor.x + ((float)visibleChildCount * cellWidth) + kLinkedDrawerChildTileOffset + tileSize;
        const float drawerRightPadding = 8.0f;
        const float groupSpanLeft = cursor.x;
        const float groupSpanRight = cursor.x + ((float)spanCells * cellWidth);
        const float groupEdgeInset = 2.0f;
        const float stripTop = cursor.y - 4.0f;
        const float stripBottom = cursor.y + tileSize + 6.0f;
        ImVec2 bgMin(std::max(cursor.x - 6.0f, groupSpanLeft + groupEdgeInset), stripTop);
        ImVec2 bgMax(std::min(lastVisibleChildRight + drawerRightPadding, groupSpanRight - groupEdgeInset), stripBottom);
        if(bgMax.x <= bgMin.x){
            return;
        }

        const ImVec2 windowMin = ImGui::GetWindowPos();
        const ImVec2 windowMax(windowMin.x + ImGui::GetWindowSize().x, windowMin.y + ImGui::GetWindowSize().y);
        drawList->PushClipRect(windowMin, windowMax, false);
        overlayDrawList->PushClipRect(windowMin, windowMax, false);

        // keep the fill subtle so child tiles remain readable; use a single strong outer border.
        ImU32 fill = IM_COL32(22, 25, 32, 58);
        ImU32 border = IM_COL32(64, 170, 255, 235);
        ImU32 trayFill = IM_COL32(10, 12, 16, 112);
        drawList->AddRectFilled(bgMin, bgMax, fill, 8.0f);
        overlayDrawList->AddRect(bgMin, bgMax, border, 8.0f, 0, 2.0f);

        // connector chip between parent tile and first linked child
        const float firstChildTileLeft = cursor.x + cellWidth + kLinkedDrawerChildTileOffset;
        const float parentTileRight = cursor.x + tileSize;
        const float connectorGap = Math3D::Max(0.0f, firstChildTileLeft - parentTileRight);
        const float chipW = std::clamp(connectorGap - 8.0f, 12.0f, 24.0f);
        const float chipH = 16.0f;
        const float chipCenterX = (parentTileRight + firstChildTileLeft) * 0.5f;
        const float chipCenterY = cursor.y + (tileSize * 0.5f);
        ImVec2 chipMin(chipCenterX - (chipW * 0.5f), chipCenterY - (chipH * 0.5f));
        ImVec2 chipMax(chipCenterX + (chipW * 0.5f), chipCenterY + (chipH * 0.5f));

        // tray begins just after the connector chip, like a slide-out drawer.
        const float trayStartX = chipMax.x - 2.0f;
        if(trayStartX < bgMax.x - 8.0f){
            ImVec2 trayMin(trayStartX, stripTop + 4.0f);
            ImVec2 trayMax(bgMax.x - 8.0f, stripBottom - 4.0f);
            drawList->AddRectFilled(trayMin, trayMax, trayFill, 6.0f);
        }

        // darker container behind the linked child tiles so the grouping reads as a drawer.
        ImVec2 linkedContainerMin(firstChildTileLeft - 6.0f, stripTop + 2.0f);
        ImVec2 linkedContainerMax(lastVisibleChildRight + 6.0f, stripBottom - 2.0f);
        linkedContainerMin.x = std::max(linkedContainerMin.x, bgMin.x + 6.0f);
        linkedContainerMax.x = std::min(linkedContainerMax.x, bgMax.x - 6.0f);
        if(linkedContainerMax.x > linkedContainerMin.x){
            drawList->AddRectFilled(linkedContainerMin, linkedContainerMax, IM_COL32(7, 9, 13, 168), 6.0f);
            drawList->AddRect(linkedContainerMin, linkedContainerMax, IM_COL32(34, 40, 50, 220), 6.0f, 0, 1.0f);
        }

        overlayDrawList->AddRectFilled(chipMin, chipMax, IM_COL32(24, 26, 31, 245), 4.0f);
        overlayDrawList->AddRect(chipMin, chipMax, IM_COL32(78, 88, 106, 230), 4.0f, 0, 1.0f);
        const char* arrowText = ">";
        ImVec2 arrowSize = ImGui::CalcTextSize(arrowText);
        ImVec2 arrowPos(
            chipMin.x + ((chipW - arrowSize.x) * 0.5f),
            chipMin.y + ((chipH - arrowSize.y) * 0.5f) - 0.5f
        );
        overlayDrawList->AddText(arrowPos, IM_COL32(236, 243, 255, 255), arrowText);

        overlayDrawList->PopClipRect();
        drawList->PopClipRect();
    };

    auto drawClippedAssetGrid = [&](const char* tableId,
                                    int columns,
                                    float tileSize,
                                    float cellWidth,
                                    ImGuiTableFlags tableFlags,
                                    const std::vector<BrowserEntry>& entryList,
                                    auto&& drawEntry){
        if(!ImGui::BeginTable(tableId, columns, tableFlags)){
            return;
        }
        for(int col = 0; col < columns; ++col){
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, cellWidth);
        }

        const int totalEntries = static_cast<int>(entryList.size());
        const int rowCount = (totalEntries + columns - 1) / columns;
        const float rowHeight = tileSize + (ImGui::GetTextLineHeightWithSpacing() * 2.4f) + 20.0f;
        ImGuiListClipper clipper;
        clipper.Begin(rowCount, rowHeight);
        while(clipper.Step()){
            for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row){
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
                for(int col = 0; col < columns; ++col){
                    const int idx = (row * columns) + col;
                    if(idx >= totalEntries){
                        break;
                    }
                    ImGui::TableSetColumnIndex(col);
                    drawEntry(entryList[(size_t)idx]);
                }
            }
        }

        ImGui::EndTable();
    };

    const bool bundleViewActive = AssetBundleRegistry::IsVirtualEntryPath(assetDir);
    if(bundleViewActive){
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.095f, 0.135f, 1.0f));
    }
    ImGui::SetNextWindowPos(ImVec2(x, y));
   ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Workspace", nullptr, kPanelFlags);

    uint64_t version = Logbot::GetLogVersion();
    if(version != lastLogSummaryVersion){
        latestLogLine = Logbot::GetLastLogLine();
        lastLogSummaryVersion = version;
    }

    auto makeUniqueSiblingPath = [&](const std::filesystem::path& desiredPath) -> std::filesystem::path{
        if(!pathExists(desiredPath)){
            return desiredPath;
        }
        const std::filesystem::path parent = desiredPath.parent_path();
        const std::string stem = desiredPath.stem().string();
        const std::string ext = desiredPath.extension().string();
        int suffix = 1;
        std::filesystem::path candidate;
        do{
            candidate = parent / (stem + "_" + std::to_string(suffix) + ext);
            suffix++;
        }while(pathExists(candidate));
        return candidate;
    };

    auto createFolderAndBeginRename = [&](){
        std::filesystem::path folderPath = makeUniqueSiblingPath(assetDir / "New Folder");

        std::filesystem::path bundlePath;
        std::string entryPath;
        if(AssetBundleRegistry::DecodeVirtualEntryPath(folderPath, bundlePath, entryPath)){
            std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
            std::string error;
            if(bundle &&
               bundle->ensureDirectory(entryPath, &error) &&
               bundle->save(&error)){
                AssetManager::Instance.unmanageAliasAssets(bundle->aliasToken());
                selectedAssetPath = folderPath;
                browserCacheDirty = true;
            }else{
                LogBot.Log(LOG_ERRO, "Failed to create bundle folder: %s", error.c_str());
            }
            return;
        }

        std::error_code ec;
        if(std::filesystem::create_directories(folderPath, ec)){
            beginAssetRename(folderPath, selectedAssetPath);
            browserCacheDirty = true;
        }else{
            LogBot.Log(LOG_ERRO, "Failed to create folder: %s", folderPath.string().c_str());
        }
    };

    auto createDefaultFile = [&](){
        std::filesystem::path filePath = makeUniqueSiblingPath(assetDir / "NewFile.txt");
        std::string error;
        if(AssetDescriptorUtils::WriteTextPath(filePath, "", &error)){
            selectedAssetPath = filePath;
            if(!AssetBundleRegistry::IsVirtualEntryPath(filePath)){
                beginAssetRename(filePath, selectedAssetPath);
            }
            browserCacheDirty = true;
        }else if(!error.empty()){
            LogBot.Log(LOG_ERRO, "Failed to create file: %s", error.c_str());
        }
    };

    auto createDefaultShaderAsset = [&](){
        std::filesystem::path path = makeUniquePathWithSuffix(assetDir / "NewShader.shader.asset", ".shader.asset");
        ShaderAssetData data;
        data.cacheName = path.filename().string();
        data.vertexAssetRef = std::string(ASSET_DELIMITER) + "/shader/Shader_Vert_Lit.vert";
        data.fragmentAssetRef = std::string(ASSET_DELIMITER) + "/shader/Shader_Frag_PBR.frag";
        std::string error;
        if(ShaderAssetIO::SaveToAbsolutePath(path, data, &error)){
            selectedAssetPath = path;
            browserCacheDirty = true;
        }else{
            LogBot.Log(LOG_ERRO, "Failed to create shader asset: %s", error.c_str());
        }
    };

    auto createDefaultSkyboxAsset = [&](){
        std::filesystem::path path = makeUniquePathWithSuffix(assetDir / "NewSkybox.skybox.asset", ".skybox.asset");
        SkyboxAssetData data;
        data.name = path.filename().string();
        std::string error;
        if(SkyboxAssetIO::SaveToAbsolutePath(path, data, &error)){
            selectedAssetPath = path;
            browserCacheDirty = true;
        }else{
            LogBot.Log(LOG_ERRO, "Failed to create skybox asset: %s", error.c_str());
        }
    };

    auto createDefaultLensFlareAsset = [&](){
        std::filesystem::path path = makeUniquePathWithSuffix(assetDir / "NewLensFlare.flare.asset", ".flare.asset");
        LensFlareAssetData data;
        data.name = path.filename().string();
        data.intensity = 1.0f;
        data.spriteScale = 132.0f;
        data.glareThreshold = 1.0f;
        data.glareIntensity = 0.14f;
        data.glareLengthPx = 96.0f;
        LensFlareElementData ring;
        ring.type = LensFlareElementType::Circle;
        ring.tint = Math3D::Vec3(0.85f, 0.95f, 1.0f);
        ring.intensity = 0.30f;
        ring.axisPosition = 0.42f;
        ring.sizeScale = 1.65f;
        ring.polygonSides = 64;
        data.elements.push_back(ring);

        LensFlareElementData core;
        core.type = LensFlareElementType::Polygon;
        core.tint = Math3D::Vec3(1.0f, 0.96f, 0.76f);
        core.intensity = 0.85f;
        core.axisPosition = 1.0f;
        core.sizeScale = 0.95f;
        core.polygonSides = 8;
        data.elements.push_back(core);

        LensFlareElementData ghost;
        ghost.type = LensFlareElementType::Polygon;
        ghost.tint = Math3D::Vec3(1.0f, 0.88f, 0.42f);
        ghost.intensity = 0.48f;
        ghost.axisPosition = -0.55f;
        ghost.sizeScale = 0.72f;
        ghost.polygonSides = 6;
        data.elements.push_back(ghost);
        std::string error;
        if(LensFlareAssetIO::SaveToAbsolutePath(path, data, &error)){
            selectedAssetPath = path;
            browserCacheDirty = true;
        }else{
            LogBot.Log(LOG_ERRO, "Failed to create lens flare asset: %s", error.c_str());
        }
    };

    auto createDefaultEffectAsset = [&](){
        std::filesystem::path path = makeUniquePathWithSuffix(assetDir / "NewEffect.effect.asset", ".effect.asset");
        EffectAssetData data;
        data.name = path.stem().stem().string();
        data.vertexAssetRef = std::string(ASSET_DELIMITER) + "/shader/PostFX_Screen.vert";
        data.fragmentAssetRef.clear();
        EffectInputBindingData screenColorInput;
        screenColorInput.uniformName = "screenTexture";
        screenColorInput.source = EffectInputSource::ScreenColor;
        screenColorInput.textureSlot = 0;
        data.inputs.push_back(screenColorInput);
        std::string error;
        if(EffectAssetIO::SaveToAbsolutePath(path, data, &error)){
            selectedAssetPath = path;
            browserCacheDirty = true;
        }else{
            LogBot.Log(LOG_ERRO, "Failed to create effect asset: %s", error.c_str());
        }
    };

    auto createDefaultBundleAsset = [&](){
        if(AssetBundleRegistry::IsVirtualEntryPath(assetDir)){
            LogBot.Log(LOG_WARN, "Nested asset bundles are not supported.");
            return;
        }
        std::filesystem::path path = makeUniquePathWithSuffix(assetDir / "NewBundle.bundle.asset", ".bundle.asset");
        auto bundle = std::make_shared<AssetBundle>();
        std::string error;
        if(bundle && bundle->createEmpty(path, path.stem().stem().string(), &error)){
            std::string mountError;
            AssetBundleRegistry::Instance.mountBundle(path, &mountError);
            if(!mountError.empty()){
                LogBot.Log(LOG_WARN, "Created bundle but failed to mount it immediately: %s", mountError.c_str());
            }
            selectedAssetPath = path;
            browserCacheDirty = true;
        }else{
            LogBot.Log(LOG_ERRO, "Failed to create bundle asset: %s", error.c_str());
        }
    };

    auto createDefaultMaterialObject = [&](){
        std::string baseName = "NewMaterial";
        const std::string originalBaseName = baseName;
        std::filesystem::path materialPath;
        std::filesystem::path materialAssetPath;
        int suffixIndex = 1;
        while(true){
            materialPath = assetDir / (baseName + ".material");
            materialAssetPath = assetDir / (baseName + ".mat.asset");
            if(!pathExists(materialPath) && !pathExists(materialAssetPath)){
                break;
            }
            baseName = originalBaseName + "_" + std::to_string(suffixIndex);
            suffixIndex++;
        }

        std::string error;
        std::filesystem::path createdMaterialAssetPath;
        if(createMaterialWithLinkedAsset(materialPath, createdMaterialAssetPath, &error)){
            selectedAssetPath = materialPath;
            EditorAssetUI::InvalidateAllThumbnails();
            browserCacheDirty = true;
        }else{
            LogBot.Log(LOG_ERRO, "Failed to create material object: %s", error.c_str());
        }
    };

    auto pasteClipboardToCurrentDirectory = [&]() -> bool{
        bool sourceIsDir = false;
        if(clipboardAssetPath.empty() || !pathExists(clipboardAssetPath, &sourceIsDir)){
            return false;
        }

        const std::filesystem::path sourcePath = clipboardAssetPath;
        const std::filesystem::path targetPath = makeUniqueSiblingPath(assetDir / sourcePath.filename());

        if(AssetBundleRegistry::IsVirtualEntryPath(assetDir)){
            std::string error;
            std::filesystem::path targetBundlePath;
            std::string targetEntryPath;
            if(!AssetBundleRegistry::DecodeVirtualEntryPath(targetPath, targetBundlePath, targetEntryPath)){
                return false;
            }

            std::shared_ptr<AssetBundle> targetBundle = AssetBundleRegistry::Instance.getBundleByPath(targetBundlePath);
            if(!targetBundle){
                return false;
            }

            auto removeMovedSource = [&]() -> bool{
                if(!clipboardAssetCutMode){
                    return true;
                }

                std::filesystem::path sourceBundlePath;
                std::string sourceEntryPath;
                if(AssetBundleRegistry::DecodeVirtualEntryPath(sourcePath, sourceBundlePath, sourceEntryPath)){
                    std::shared_ptr<AssetBundle> sourceBundle = AssetBundleRegistry::Instance.getBundleByPath(sourceBundlePath);
                    if(!sourceBundle ||
                       !sourceBundle->removeEntry(sourceEntryPath, &error) ||
                       !sourceBundle->save(&error)){
                        return false;
                    }
                    AssetManager::Instance.unmanageAliasAssets(sourceBundle->aliasToken());
                }else{
                    File sourceFile(sourcePath.string());
                    if(!sourceFile.deleteFile()){
                        error = "Failed to remove the original item after paste.";
                        return false;
                    }
                }

                clipboardAssetPath.clear();
                clipboardAssetCutMode = false;
                return true;
            };

            bool staged = false;
            std::filesystem::path sourceBundlePath;
            std::string sourceEntryPath;
            if(AssetBundleRegistry::DecodeVirtualEntryPath(sourcePath, sourceBundlePath, sourceEntryPath)){
                if(sourceIsDir){
                    error = "Copying bundle folders is not supported yet.";
                    return false;
                }

                BinaryBuffer bytes;
                std::shared_ptr<AssetBundle> sourceBundle = AssetBundleRegistry::Instance.getBundleByPath(sourceBundlePath);
                if(!sourceBundle || !sourceBundle->readEntryBytes(sourceEntryPath, bytes, &error)){
                    return false;
                }

                staged = targetBundle->addOrUpdateFileFromBuffer(
                    targetEntryPath,
                    bytes,
                    absolutePathToAssetRef(sourcePath),
                    &error
                );
            }else if(sourceIsDir){
                staged = targetBundle->ensureDirectory(targetEntryPath, &error);
                if(staged){
                    std::error_code walkEc;
                    for(const auto& entry : std::filesystem::recursive_directory_iterator(
                            sourcePath,
                            std::filesystem::directory_options::skip_permission_denied,
                            walkEc)){
                        if(walkEc){
                            error = walkEc.message();
                            staged = false;
                            break;
                        }

                        std::error_code relEc;
                        const std::filesystem::path rel = std::filesystem::relative(entry.path(), sourcePath, relEc);
                        if(relEc){
                            error = relEc.message();
                            staged = false;
                            break;
                        }

                        const std::string bundleEntryPath = (std::filesystem::path(targetEntryPath) / rel).generic_string();
                        if(entry.is_directory(walkEc)){
                            staged = targetBundle->ensureDirectory(bundleEntryPath, &error);
                        }else if(entry.is_regular_file(walkEc)){
                            staged = targetBundle->addOrUpdateFileFromAbsolutePath(bundleEntryPath, entry.path(), &error);
                        }else{
                            continue;
                        }

                        if(!staged){
                            break;
                        }
                    }
                }
            }else{
                staged = targetBundle->addOrUpdateFileFromAbsolutePath(targetEntryPath, sourcePath, &error);
            }

            if(!staged || !targetBundle->save(&error)){
                if(!error.empty()){
                    LogBot.Log(LOG_ERRO, "Failed to paste into bundle: %s", error.c_str());
                }
                return false;
            }

            AssetManager::Instance.unmanageAliasAssets(targetBundle->aliasToken());
            if(!removeMovedSource() && !error.empty()){
                LogBot.Log(LOG_WARN, "Bundle paste completed but cleanup failed: %s", error.c_str());
            }
            selectedAssetPath = targetPath;
            expandedRelatedAssets.clear();
            browserCacheDirty = true;
            return true;
        }

        std::error_code ec;

        bool success = false;
        if(clipboardAssetCutMode){
            std::filesystem::rename(sourcePath, targetPath, ec);
            if(ec){
                ec.clear();
                if(sourceIsDir){
                    std::filesystem::copy(sourcePath, targetPath, std::filesystem::copy_options::recursive, ec);
                }else{
                    std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::none, ec);
                }
                if(!ec){
                    std::error_code deleteEc;
                    if(sourceIsDir){
                        std::filesystem::remove_all(sourcePath, deleteEc);
                    }else{
                        std::filesystem::remove(sourcePath, deleteEc);
                    }
                    success = !deleteEc;
                }
            }else{
                success = true;
            }
            if(success){
                clipboardAssetPath.clear();
                clipboardAssetCutMode = false;
            }
        }else{
            if(sourceIsDir){
                std::filesystem::copy(sourcePath, targetPath, std::filesystem::copy_options::recursive, ec);
            }else{
                std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::none, ec);
            }
            success = !ec;
        }

        if(success){
            if(sourceIsDir || AssetBundle::IsBundlePath(sourcePath) || AssetBundle::IsBundlePath(targetPath)){
                AssetBundleRegistry::Instance.scanKnownLocations(nullptr);
            }
            selectedAssetPath = targetPath;
            expandedRelatedAssets.clear();
            browserCacheDirty = true;
        }
        return success;
    };

    auto drawContextMenu = [&](const char* popupId, const std::filesystem::path& targetPath, bool hasTarget, const char* confirmDeletePopupId){
        if(!ImGui::BeginPopupContextWindow(popupId, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)){
            return;
        }

        const bool currentDirIsVirtual = AssetBundleRegistry::IsVirtualEntryPath(assetDir);
        bool canTarget = hasTarget && !targetPath.empty();
        bool targetIsDirectory = false;
        if(canTarget){
            canTarget = pathExists(targetPath, &targetIsDirectory);
        }

        bool clipboardIsDirectory = false;
        const bool clipboardExists = !clipboardAssetPath.empty() && pathExists(clipboardAssetPath, &clipboardIsDirectory);
        const bool clipboardIsVirtual = AssetBundleRegistry::IsVirtualEntryPath(clipboardAssetPath);
        const bool canPaste = !hasTarget &&
                              clipboardExists &&
                              (currentDirIsVirtual
                                   ? (!clipboardIsVirtual || !clipboardIsDirectory)
                                   : !clipboardIsVirtual);
        const bool canCut = canTarget && !currentDirIsVirtual;
        const bool canCopy = canTarget && (!currentDirIsVirtual || !targetIsDirectory);
        const bool canRename = canTarget;

        ImGui::Separator();
        if(!hasTarget){
            if(ImGui::BeginMenu("New")){
                if(ImGui::MenuItem("File")){
                    createDefaultFile();
                }
                if(ImGui::MenuItem("Folder")){
                    createFolderAndBeginRename();
                }
                if(ImGui::BeginMenu("Asset")){
                    if(!currentDirIsVirtual && ImGui::MenuItem("Asset Bundle")){
                        createDefaultBundleAsset();
                    }
                    if(ImGui::MenuItem("Shader Asset")){
                        createDefaultShaderAsset();
                    }
                    if(ImGui::MenuItem("Skybox Asset")){
                        createDefaultSkyboxAsset();
                    }
                    if(ImGui::MenuItem("Lens Flare Asset")){
                        createDefaultLensFlareAsset();
                    }
                    if(ImGui::MenuItem("Effect Asset")){
                        createDefaultEffectAsset();
                    }
                    if(ImGui::MenuItem("Material (.material)")){
                        createDefaultMaterialObject();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
        }

        ImGui::Separator();
        if(ImGui::MenuItem("Cut", nullptr, false, canCut)){
            clipboardAssetPath = targetPath;
            clipboardAssetCutMode = true;
        }
        if(ImGui::MenuItem("Copy", nullptr, false, canCopy)){
            clipboardAssetPath = targetPath;
            clipboardAssetCutMode = false;
        }
        if(ImGui::MenuItem("Paste", nullptr, false, canPaste)){
            pasteClipboardToCurrentDirectory();
        }
        if(ImGui::MenuItem("Delete", nullptr, false, canTarget)){
            pendingDeleteAssetPath = targetPath;
            deletePopupOpenRequested = true;
            deletePopupIdToOpen = confirmDeletePopupId;
            ImGui::CloseCurrentPopup();
        }
        if(ImGui::MenuItem("Rename", nullptr, false, canRename)){
            beginAssetRename(targetPath, selectedAssetPath);
        }
        ImGui::Separator();
        ImGui::EndPopup();
    };

    auto drawDeleteConfirmationPopup = [&](const char* popupId){
        if(deletePopupOpenRequested && deletePopupIdToOpen == popupId){
            ImGui::OpenPopup(popupId);
            deletePopupOpenRequested = false;
            deletePopupIdToOpen.clear();
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if(viewport){
            const float modalWidth = std::clamp(viewport->Size.x * 0.36f, 420.0f, 780.0f);
            const float modalHeight = modalWidth / 3.0f; // 3:1 (wide) dialog.
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(modalWidth, modalHeight), ImGuiCond_Appearing);
        }

        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.82f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.065f, 1.0f));
        if(!ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)){
            ImGui::PopStyleColor(2);
            return;
        }

        if(pendingDeleteAssetPath.empty()){
            ImGui::TextUnformatted("No asset selected.");
        }else{
            bool isDir = false;
            pathExists(pendingDeleteAssetPath, &isDir);
            ImGui::Text("Delete %s?", isDir ? "directory" : "file");
            ImGui::TextWrapped("%s", absolutePathToAssetRef(pendingDeleteAssetPath).c_str());
            ImGui::TextDisabled("This cannot be undone.");
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const float yesWidth = ImGui::CalcTextSize("Yes, Delete").x + (style.FramePadding.x * 2.0f);
        const float cancelWidth = ImGui::CalcTextSize("Cancel").x + (style.FramePadding.x * 2.0f);
        const float buttonsWidth = yesWidth + style.ItemSpacing.x + cancelWidth;
        const float footerY = ImGui::GetWindowHeight() - style.WindowPadding.y - ImGui::GetFrameHeight();
        if(ImGui::GetCursorPosY() < footerY){
            ImGui::SetCursorPosY(footerY);
        }
        const float rightX = ImGui::GetWindowContentRegionMax().x - buttonsWidth;
        if(ImGui::GetCursorPosX() < rightX){
            ImGui::SetCursorPosX(rightX);
        }

        if(ImGui::Button("Yes, Delete", ImVec2(yesWidth, 0.0f))){
            if(!pendingDeleteAssetPath.empty()){
                std::vector<std::filesystem::path> deleteTargets;
                deleteTargets.push_back(pendingDeleteAssetPath);

                // Deleting a .material should also remove its linked .mat.asset companion when present.
                if(MaterialAssetIO::IsMaterialObjectPath(pendingDeleteAssetPath)){
                    std::filesystem::path linkedPath;
                    if(resolveLinkedMaterialAssetPath(pendingDeleteAssetPath, linkedPath)){
                        if(normalizedPathKey(linkedPath) != normalizedPathKey(pendingDeleteAssetPath)){
                            deleteTargets.push_back(linkedPath);
                        }
                    }
                }

                auto clearDeletedPathState = [&](const std::filesystem::path& deletedPath){
                    const std::string deletedKey = normalizedPathKey(deletedPath);
                    if(assetDir == deletedPath){
                        assetDir = assetDir.parent_path();
                    }
                    if(selectedAssetPath == deletedPath){
                        selectedAssetPath.clear();
                    }
                    if(assetRenameActive && assetRenamePath == deletedPath){
                        cancelAssetRename();
                    }
                    if(!clipboardAssetPath.empty() && normalizedPathKey(clipboardAssetPath) == deletedKey){
                        clipboardAssetPath.clear();
                        clipboardAssetCutMode = false;
                    }
                    if(!contextMenuTargetPath.empty() && normalizedPathKey(contextMenuTargetPath) == deletedKey){
                        contextMenuTargetPath.clear();
                        contextMenuHasTarget = false;
                    }
                    if(!pickerContextMenuTargetPath.empty() && normalizedPathKey(pickerContextMenuTargetPath) == deletedKey){
                        pickerContextMenuTargetPath.clear();
                        pickerContextMenuHasTarget = false;
                    }
                };

                bool hadDeleteError = false;
                bool deletedAny = false;
                bool refreshBundleRegistry = false;
                for(const auto& deletePath : deleteTargets){
                    bool deletingDirectory = false;
                    if(!pathExists(deletePath, &deletingDirectory)){
                        continue;
                    }

                    std::filesystem::path bundlePath;
                    std::string entryPath;
                    if(AssetBundleRegistry::DecodeVirtualEntryPath(deletePath, bundlePath, entryPath)){
                        std::shared_ptr<AssetBundle> bundle = AssetBundleRegistry::Instance.getBundleByPath(bundlePath);
                        std::string error;
                        if(!bundle ||
                           !bundle->removeEntry(entryPath, &error) ||
                           !bundle->save(&error)){
                            hadDeleteError = true;
                            continue;
                        }

                        AssetManager::Instance.unmanageAliasAssets(bundle->aliasToken());
                        deletedAny = true;
                        clearDeletedPathState(deletePath);
                        continue;
                    }

                    File file(deletePath.string());
                    if(!file.deleteFile()){
                        hadDeleteError = true;
                        continue;
                    }

                    deletedAny = true;
                    refreshBundleRegistry =
                        refreshBundleRegistry ||
                        deletingDirectory ||
                        AssetBundle::IsBundlePath(deletePath);
                    clearDeletedPathState(deletePath);
                }

                if(deletedAny){
                    if(refreshBundleRegistry){
                        AssetBundleRegistry::Instance.scanKnownLocations(nullptr);
                    }
                    EditorAssetUI::InvalidateAllThumbnails();
                    browserCacheDirty = true;
                }
                if(hadDeleteError){
                    LogBot.Log(LOG_WARN, "One or more delete operations failed.");
                }
                expandedRelatedAssets.clear();
            }
            deletePopupOpenRequested = false;
            deletePopupIdToOpen.clear();
            pendingDeleteAssetPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        if(ImGui::Button("Cancel", ImVec2(cancelWidth, 0.0f))){
            deletePopupOpenRequested = false;
            deletePopupIdToOpen.clear();
            pendingDeleteAssetPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        ImGui::PopStyleColor(2);
    };

    if(ImGui::BeginTabBar("BottomTabs")){
        if(ImGui::BeginTabItem("Assets")){
            if(assetRenameActive){
                if(assetRenamePath.empty() || !pathExists(assetRenamePath)){
                    cancelAssetRename();
                }
            }

            ImGui::Text("Directory: %s", absolutePathToAssetRef(assetDir).c_str());
            drawDeleteConfirmationPopup("Confirm Delete Asset Main");

            ImGui::Separator();

            constexpr float kFooterBottomPadding = 5.0f;
            float footerReserve = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + kFooterBottomPadding + 2.0f;
            ImGui::BeginChild("AssetList", ImVec2(0.0f, -footerReserve), false);
            bool openAssetContextMenu = false;
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            bool assetDirIsDirectory = false;
            if(pathExists(assetDir, &assetDirIsDirectory) && assetDirIsDirectory){
                const std::vector<BrowserEntry>* entries = nullptr;
                const LinkedChildrenMap* linkedByParent = nullptr;
                getBrowserEntriesCached(entries, linkedByParent);
                if(!entries || !linkedByParent){
                    entries = &s_browserEntriesCache;
                    linkedByParent = &s_browserLinkedCache;
                }

                float cellWidth = assetTileSize + 20.0f;
                int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellWidth));
                ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX;
                drawClippedAssetGrid("AssetGridTable", columns, assetTileSize, cellWidth, tableFlags, *entries, [&](const BrowserEntry& entry){
                        EditorAssetUI::AssetTransaction tx{};
                        const auto& path = entry.path;
                        if(entry.isUp){
                            if(!EditorAssetUI::BuildTransaction(path, assetRoot, tx)){
                                tx.absolutePath = path;
                                tx.assetRef = absolutePathToAssetRef(path);
                                tx.extension.clear();
                                tx.kind = EditorAssetUI::AssetKind::Directory;
                                tx.isDirectory = true;
                            }
                        }else if(!EditorAssetUI::BuildTransaction(path, assetRoot, tx)){
                            return;
                        }

                        ImGui::PushID(path.string().c_str());

                        EditorAssetUI::AssetTransaction drawTx = tx;
                        if(entry.forceDataIcon){
                            drawTx.kind = EditorAssetUI::AssetKind::Unknown;
                        }

                        const std::string& pathKey = entry.normalizedKey;
                        const bool hasRelatedChildren =
                            (!entry.isUp && !entry.isDirectory && !entry.isRelated && linkedByParent->find(pathKey) != linkedByParent->end());
                        const bool parentExpanded = hasRelatedChildren &&
                            (expandedRelatedAssets.find(pathKey) != expandedRelatedAssets.end());
                        size_t relatedChildCount = 0;
                        if(hasRelatedChildren){
                            auto linkedIt = linkedByParent->find(pathKey);
                            if(linkedIt != linkedByParent->end()){
                                relatedChildCount = linkedIt->second.size();
                            }
                        }
                        drawLinkedDrawerGroupBackdrop(
                            entry,
                            parentExpanded,
                            relatedChildCount,
                            ImGui::TableGetColumnIndex(),
                            columns,
                            assetTileSize,
                            cellWidth
                        );

                        if(entry.isRelated){
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kLinkedDrawerChildTileOffset);
                        }

                        bool selected = (path == selectedAssetPath);
                        bool doubleClicked = false;
                        bool clicked = EditorAssetUI::DrawAssetTile("asset_tile", drawTx, assetTileSize, selected, &doubleClicked);
                        ImVec2 tileMin = ImGui::GetItemRectMin();
                        ImVec2 tileMax = ImGui::GetItemRectMax();
                        bool tileRightClicked = ImGui::IsMouseHoveringRect(tileMin, tileMax, false) && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
                        if(tileRightClicked){
                            contextMenuTargetPath = path;
                            contextMenuHasTarget = true;
                            selectedAssetPath = path;
                            openAssetContextMenu = true;
                            clicked = false;
                            doubleClicked = false;
                        }
                        if(!entry.isUp){
                            EditorAssetUI::BeginAssetDragSource(tx);
                        }

                        bool toggledChildren = false;
                        if(hasRelatedChildren){
                            bool expanded = parentExpanded;
                            if(drawRelatedToggle(assetTileSize, expanded)){
                                toggledChildren = true;
                                if(expanded){
                                    expandedRelatedAssets.erase(pathKey);
                                    auto linkedIt = linkedByParent->find(pathKey);
                                    if(linkedIt != linkedByParent->end()){
                                        for(const auto& linkedPath : linkedIt->second){
                                            if(selectedAssetPath == linkedPath){
                                                selectedAssetPath = path;
                                                break;
                                            }
                                        }
                                    }
                                }else{
                                    expandedRelatedAssets.insert(pathKey);
                                }
                                browserCacheDirty = true;
                                clicked = false;
                                doubleClicked = false;
                            }
                        }

                        const bool renameThisEntry = assetRenameActive && !entry.isUp && !entry.isRelated && (assetRenamePath == path);
                        if(renameThisEntry){
                            if(assetRenameFocus){
                                ImGui::SetKeyboardFocusHere();
                                assetRenameFocus = false;
                            }
                            bool submitted = ImGui::InputText("##AssetRename", assetRenameBuffer, sizeof(assetRenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                            const bool deactivated = ImGui::IsItemDeactivated();
                            const bool active = ImGui::IsItemActive();
                            if(submitted || (deactivated && !active)){
                                commitAssetRename(selectedAssetPath);
                            }
                            if(active && ImGui::IsKeyPressed(ImGuiKey_Escape)){
                                cancelAssetRename();
                            }
                            bool renameRightClicked = ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
                            if(renameRightClicked){
                                contextMenuTargetPath = path;
                                contextMenuHasTarget = true;
                                selectedAssetPath = path;
                                openAssetContextMenu = true;
                            }
                        }else{
                            std::string label = formatEntryLabel(entry);
                            ImGui::TextWrapped("%s", label.c_str());
                            bool labelClicked = !toggledChildren && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                            bool labelDoubleClicked = !toggledChildren && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                            bool labelRightClicked = !toggledChildren && ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
                            clicked = clicked || labelClicked;
                            doubleClicked = doubleClicked || labelDoubleClicked;
                            if(labelRightClicked){
                                contextMenuTargetPath = path;
                                contextMenuHasTarget = true;
                                selectedAssetPath = path;
                                openAssetContextMenu = true;
                                clicked = false;
                                doubleClicked = false;
                            }
                        }

                        if(entry.isDirectory){
                            if(doubleClicked){
                                assetDir = path;
                                selectedAssetPath.clear();
                                if(assetRenameActive){
                                    cancelAssetRename();
                                }
                            }else if(clicked){
                                selectedAssetPath = path;
                            }
                        }else if(doubleClicked &&
                                 AssetBundle::IsBundlePath(path) &&
                                 !AssetBundleRegistry::IsVirtualEntryPath(path)){
                            assetDir = AssetBundleRegistry::MakeVirtualEntryPath(path, "", true);
                            selectedAssetPath.clear();
                            browserCacheDirty = true;
                        }else if(clicked){
                            selectedAssetPath = path;
                        }

                        ImGui::PopID();
                });
            }else{
                ImGui::TextUnformatted("Directory missing.");
            }

            if(openAssetContextMenu){
                ImGui::OpenPopup("AssetContextMenuMain");
            }else if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
                     ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                     !ImGui::IsAnyItemHovered()){
                selectedAssetPath.clear();
                contextMenuHasTarget = false;
                contextMenuTargetPath.clear();
            }else if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
                     ImGui::IsMouseReleased(ImGuiMouseButton_Right)){
                contextMenuHasTarget = false;
                contextMenuTargetPath.clear();
            }
            drawContextMenu("AssetContextMenuMain", contextMenuTargetPath, contextMenuHasTarget, "Confirm Delete Asset Main");

            if(!selectedAssetPath.empty() &&
               ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
               ImGui::IsKeyPressed(ImGuiKey_Delete, false)){
                pendingDeleteAssetPath = selectedAssetPath;
                deletePopupOpenRequested = true;
                deletePopupIdToOpen = "Confirm Delete Asset Main";
            }

            ImGui::EndChild();

            if(onEntityDropToPrefab && ImGui::BeginDragDropTarget()){
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ECSViewPanel::EntityDragPayloadType);
                if(payload && payload->Data && payload->DataSize > 0){
                    const char* rawEntityId = static_cast<const char*>(payload->Data);
                    std::string entityId = StringUtils::Trim(std::string(rawEntityId));
                    if(!entityId.empty()){
                        std::string exportError;
                        if(onEntityDropToPrefab(entityId, assetDir, &exportError)){
                            expandedRelatedAssets.clear();
                            browserCacheDirty = true;
                        }else{
                            if(exportError.empty()){
                                exportError = "Unknown export error.";
                            }
                            LogBot.Log(LOG_ERRO, "Failed to create prefab from dropped entity: %s", exportError.c_str());
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::Separator();

            std::string latestFooterLogLine = latestLogLine.empty() ? std::string("No log output.") : latestLogLine;
            if(latestFooterLogLine.size() > 180){
                latestFooterLogLine = latestFooterLogLine.substr(0, 177) + "...";
            }

            float rightPaneWidth = 220.0f;
            float totalWidth = ImGui::GetContentRegionAvail().x;
            float leftPaneWidth = totalWidth - rightPaneWidth - 8.0f;
            if(leftPaneWidth < 120.0f){
                leftPaneWidth = Math3D::Max(120.0f, totalWidth * 0.45f);
                rightPaneWidth = Math3D::Max(120.0f, totalWidth - leftPaneWidth - 8.0f);
            }

            std::string label;
            if(!selectedAssetPath.empty()){
                EditorAssetUI::AssetTransaction selectedTx;
                if(EditorAssetUI::BuildTransaction(selectedAssetPath, assetRoot, selectedTx)){
                    label = std::string("Selected: ") + selectedTx.assetRef;
                }else{
                    selectedAssetPath.clear();
                    label = std::string("Latest: ") + latestFooterLogLine;
                }
            }else{
                label = std::string("Latest: ") + latestFooterLogLine;
            }
            if(ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(leftPaneWidth, 0.0f))){
                requestOpenLogTab = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("Icon Size");
            ImGui::SameLine();
            const float sliderWidth = 140.0f;
            ImGui::SetNextItemWidth(sliderWidth);
            ImGui::SliderFloat("##IconSizeBottom", &assetTileSize, 56.0f, 112.0f, "%.0f px");
            ImGui::Dummy(ImVec2(0.0f, kFooterBottomPadding));

            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags logTabFlags = requestOpenLogTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if(ImGui::BeginTabItem("Log", nullptr, logTabFlags)){
            requestOpenLogTab = false;
            bool logUpdated = false;
            if(version != lastLogVersion){
                logBuffer = Logbot::GetLogHistory();
                lastLogVersion = version;
                logUpdated = true;
                logLines.clear();
                logColors.clear();

                std::string current;
                current.reserve(256);
                for(char c : logBuffer){
                    if(c == '\n'){
                        if(!current.empty()){
                            ImVec4 color(0.8f, 0.8f, 0.8f, 1.0f);
                            if(current.find("[Warning]") != std::string::npos){
                                color = ImVec4(1.0f, 0.86f, 0.2f, 1.0f);
                            }else if(current.find("[ERROR]") != std::string::npos){
                                color = ImVec4(1.0f, 0.25f, 0.25f, 1.0f);
                            }else if(current.find("[FATAL ERROR]") != std::string::npos){
                                color = ImVec4(1.0f, 0.1f, 0.1f, 1.0f);
                            }else if(current.find("[Info]") != std::string::npos){
                                color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
                            }else if(current.find("[Unknown]") != std::string::npos){
                                color = ImVec4(0.53f, 0.53f, 0.53f, 1.0f);
                            }
                            logLines.push_back(current);
                            logColors.push_back(color);
                        }
                        current.clear();
                    }else{
                        current.push_back(c);
                    }
                }
                if(!current.empty()){
                    ImVec4 color(0.8f, 0.8f, 0.8f, 1.0f);
                    if(current.find("[Warning]") != std::string::npos){
                        color = ImVec4(1.0f, 0.86f, 0.2f, 1.0f);
                    }else if(current.find("[ERROR]") != std::string::npos){
                        color = ImVec4(1.0f, 0.25f, 0.25f, 1.0f);
                    }else if(current.find("[FATAL ERROR]") != std::string::npos){
                        color = ImVec4(1.0f, 0.1f, 0.1f, 1.0f);
                    }else if(current.find("[Info]") != std::string::npos){
                        color = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
                    }else if(current.find("[Unknown]") != std::string::npos){
                        color = ImVec4(0.53f, 0.53f, 0.53f, 1.0f);
                    }
                    logLines.push_back(current);
                    logColors.push_back(color);
                }
            }
            ImGui::BeginChild("LogView", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            for(size_t i = 0; i < logLines.size(); ++i){
                ImGui::TextColored(logColors[i], "%s", logLines[i].c_str());
            }
            if(logUpdated){
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
    if(bundleViewActive){
        ImGui::PopStyleColor();
    }

    if(showAssetPickerWindow){
        static EditorAssetUI::AssetBrowserWidget s_workspacePickerBrowser;
        s_workspacePickerBrowser.setAssetRoot(assetRoot);
        s_workspacePickerBrowser.setCurrentDirectory(assetDir);
        s_workspacePickerBrowser.setSelectedPath(selectedAssetPath);
        s_workspacePickerBrowser.resetRequestedKinds();
        s_workspacePickerBrowser.setTileSize(assetTileSize);

        ImGui::SetNextWindowSize(ImVec2(760.0f, 500.0f), ImGuiCond_FirstUseEver);
        const bool pickerBundleViewActive = s_workspacePickerBrowser.isBrowsingBundle();
        if(pickerBundleViewActive){
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.095f, 0.135f, 1.0f));
        }
        if(ImGui::Begin("Asset Picker", &showAssetPickerWindow)){
            const std::filesystem::path previousDirectory = assetDir;
            const std::filesystem::path previousSelection = selectedAssetPath;
            const EditorAssetUI::AssetBrowserWidget::DrawResult drawResult =
                s_workspacePickerBrowser.draw("WorkspaceAssetPickerBrowser");

            assetDir = s_workspacePickerBrowser.getCurrentDirectory();
            selectedAssetPath = s_workspacePickerBrowser.getSelectedPath();

            if(assetDir != previousDirectory){
                browserCacheDirty = true;
            }

            if(drawResult.itemContextRequested){
                pickerContextMenuTargetPath = selectedAssetPath;
                pickerContextMenuHasTarget = !selectedAssetPath.empty();
                ImGui::OpenPopup("AssetContextMenuPicker");
            }else if(drawResult.backgroundContextRequested){
                pickerContextMenuHasTarget = false;
                pickerContextMenuTargetPath.clear();
                ImGui::OpenPopup("AssetContextMenuPicker");
            }else if(selectedAssetPath.empty() && !previousSelection.empty()){
                pickerContextMenuHasTarget = false;
                pickerContextMenuTargetPath.clear();
            }

            drawContextMenu("AssetContextMenuPicker", pickerContextMenuTargetPath, pickerContextMenuHasTarget, "Confirm Delete Asset Picker");
            drawDeleteConfirmationPopup("Confirm Delete Asset Picker");

            if(!selectedAssetPath.empty() &&
               ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
               ImGui::IsKeyPressed(ImGuiKey_Delete, false)){
                pendingDeleteAssetPath = selectedAssetPath;
                deletePopupOpenRequested = true;
                deletePopupIdToOpen = "Confirm Delete Asset Picker";
            }

        }
        ImGui::End();
        if(pickerBundleViewActive){
            ImGui::PopStyleColor();
        }
    }
}
