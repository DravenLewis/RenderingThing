#include "Widgets/WorkspacePanel.h"

#include "EditorAssetUI.h"
#include "File.h"
#include "Logbot.h"
#include "MaterialAsset.h"
#include "ShaderAsset.h"
#include "StringUtils.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace {
    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse;

    std::string normalizedPathKey(const std::filesystem::path& path){
        std::error_code ec;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
        if(ec){
            normalized = path.lexically_normal();
        }
        return StringUtils::ToLowerCase(normalized.generic_string());
    }

    bool assetRefToAbsolutePath(const std::string& assetRef, std::filesystem::path& outPath){
        if(assetRef.empty()){
            return false;
        }
        if(StringUtils::BeginsWith(assetRef, ASSET_DELIMITER)){
            std::string rel = assetRef.substr(std::strlen(ASSET_DELIMITER));
            if(!rel.empty() && (rel[0] == '/' || rel[0] == '\\')){
                rel.erase(rel.begin());
            }
            outPath = std::filesystem::path(File::GetCWD()) / "res" / rel;
            return true;
        }
        outPath = std::filesystem::path(assetRef);
        return true;
    }

    std::string absolutePathToAssetRef(const std::filesystem::path& absolutePath){
        std::error_code ec;
        std::filesystem::path assetRoot = std::filesystem::weakly_canonical(std::filesystem::path(File::GetCWD()) / "res", ec);
        if(ec){
            assetRoot = (std::filesystem::path(File::GetCWD()) / "res").lexically_normal();
        }

        std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(absolutePath, ec);
        if(ec){
            normalizedPath = absolutePath.lexically_normal();
        }

        std::filesystem::path rel = normalizedPath.lexically_relative(assetRoot);
        if(!rel.empty() && !StringUtils::BeginsWith(rel.generic_string(), "..")){
            return std::string(ASSET_DELIMITER) + "/" + rel.generic_string();
        }
        return normalizedPath.generic_string();
    }
}

void WorkspacePanel::setAssetRoot(const std::filesystem::path& rootPath){
    assetRoot = rootPath;
    if(assetDir.empty()){
        assetDir = assetRoot;
    }
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
    }

    std::filesystem::path newPath = oldPath.parent_path() / normalizedNewName;
    if(newPath == oldPath){
        cancelAssetRename();
        return;
    }

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

    if(assetDir == oldPath){
        assetDir = newPath;
    }
    if(selectedAssetPath == oldPath){
        selectedAssetPath = newPath;
    }

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

    EditorAssetUI::InvalidateAllThumbnails();
    cancelAssetRename();
}

void WorkspacePanel::cancelAssetRename(){
    assetRenameActive = false;
    assetRenamePath.clear();
    assetRenameFocus = false;
    std::memset(assetRenameBuffer, 0, sizeof(assetRenameBuffer));
}

std::filesystem::path WorkspacePanel::makeUniquePathWithSuffix(const std::filesystem::path& desiredPath, const std::string& suffix) const{
    if(!std::filesystem::exists(desiredPath)){
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
    }while(std::filesystem::exists(candidate));
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

void WorkspacePanel::draw(float x, float y, float w, float h, std::filesystem::path& selectedAssetPath){
    if(assetRoot.empty()){
        assetRoot = std::filesystem::path(File::GetCWD()) / "res";
    }
    if(assetDir.empty()){
        assetDir = assetRoot;
    }

    struct BrowserEntry{
        std::filesystem::path path;
        bool isDirectory = false;
        bool isUp = false;
        bool isRelated = false;
        bool forceDataIcon = false;
        std::string parentKey;
    };

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
        if(!std::filesystem::exists(normalizedPath, ec) || std::filesystem::is_directory(normalizedPath, ec)){
            return false;
        }

        outLinkedPath = normalizedPath;
        return true;
    };

    auto collectBrowserEntries = [&](std::vector<BrowserEntry>& outEntries,
                                     std::unordered_map<std::string, std::filesystem::path>& outLinkedByParent){
        outEntries.clear();
        outLinkedByParent.clear();

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

        std::unordered_set<std::string> hiddenRelatedAssetKeys;
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

            if(normalizedPathKey(linkedPath.parent_path()) != normalizedPathKey(assetDir)){
                continue;
            }

            const std::string parentKey = normalizedPathKey(entry.path);
            outLinkedByParent[parentKey] = linkedPath;

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
            if(!MaterialAssetIO::IsMaterialObjectPath(normalizedParentPath)){
                continue;
            }
            if(normalizedPathKey(normalizedParentPath.parent_path()) != normalizedPathKey(assetDir)){
                continue;
            }

            const std::string parentKey = normalizedPathKey(normalizedParentPath);
            if(parentKey.empty()){
                continue;
            }

            outLinkedByParent[parentKey] = entry.path;
            const std::string linkedKey = normalizedPathKey(entry.path);
            if(!linkedKey.empty()){
                hiddenRelatedAssetKeys.insert(linkedKey);
            }
        }

        for(const auto& entry : baseEntries){
            const std::string entryKey = normalizedPathKey(entry.path);
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

            BrowserEntry relatedEntry;
            relatedEntry.path = linkedIt->second;
            relatedEntry.isRelated = true;
            relatedEntry.forceDataIcon = true;
            relatedEntry.parentKey = entryKey;
            outEntries.push_back(relatedEntry);
        }
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

    auto formatEntryLabel = [&](const BrowserEntry& entry) -> std::string{
        if(entry.isUp){
            return "..";
        }

        std::string label = entry.path.filename().string();
        if(!entry.isRelated){
            const std::string lowerName = StringUtils::ToLowerCase(label);
            if(StringUtils::EndsWith(lowerName, ".material") && !StringUtils::EndsWith(lowerName, ".material.asset")){
                label = entry.path.stem().string();
            }
        }else{
            label = std::string("-> ") + label;
        }

        return label;
    };

    ImGui::SetNextWindowPos(ImVec2(x, y));
   ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Workspace", nullptr, kPanelFlags);

    bool logUpdated = false;
    uint64_t version = Logbot::GetLogVersion();
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

    auto makeUniqueSiblingPath = [&](const std::filesystem::path& desiredPath) -> std::filesystem::path{
        if(!std::filesystem::exists(desiredPath)){
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
        }while(std::filesystem::exists(candidate));
        return candidate;
    };

    auto createFolderAndBeginRename = [&](){
        std::filesystem::path folderPath = makeUniqueSiblingPath(assetDir / "New Folder");

        std::error_code ec;
        if(std::filesystem::create_directories(folderPath, ec)){
            beginAssetRename(folderPath, selectedAssetPath);
        }else{
            LogBot.Log(LOG_ERRO, "Failed to create folder: %s", folderPath.string().c_str());
        }
    };

    auto createDefaultFile = [&](){
        std::filesystem::path filePath = makeUniqueSiblingPath(assetDir / "NewFile.txt");
        File file(filePath.string());
        if(file.createFile()){
            selectedAssetPath = filePath;
            beginAssetRename(filePath, selectedAssetPath);
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
        }else{
            LogBot.Log(LOG_ERRO, "Failed to create shader asset: %s", error.c_str());
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
            if(!std::filesystem::exists(materialPath) && !std::filesystem::exists(materialAssetPath)){
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
        }else{
            LogBot.Log(LOG_ERRO, "Failed to create material object: %s", error.c_str());
        }
    };

    auto pasteClipboardToCurrentDirectory = [&]() -> bool{
        std::error_code ec;
        if(clipboardAssetPath.empty() || !std::filesystem::exists(clipboardAssetPath, ec)){
            return false;
        }

        const std::filesystem::path sourcePath = clipboardAssetPath;
        const std::filesystem::path targetPath = makeUniqueSiblingPath(assetDir / sourcePath.filename());
        const bool sourceIsDir = std::filesystem::is_directory(sourcePath, ec);
        if(ec){
            return false;
        }

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
            selectedAssetPath = targetPath;
            expandedRelatedAssets.clear();
        }
        return success;
    };

    auto drawContextMenu = [&](const char* popupId, const std::filesystem::path& targetPath, bool hasTarget, const char* confirmDeletePopupId){
        if(!ImGui::BeginPopup(popupId)){
            return;
        }

        std::error_code ec;
        bool canTarget = hasTarget && !targetPath.empty();
        bool canPaste = !hasTarget && !clipboardAssetPath.empty() && std::filesystem::exists(clipboardAssetPath, ec);

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
                    if(ImGui::MenuItem("Shader Asset")){
                        createDefaultShaderAsset();
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
        if(ImGui::MenuItem("Cut", nullptr, false, canTarget)){
            clipboardAssetPath = targetPath;
            clipboardAssetCutMode = true;
        }
        if(ImGui::MenuItem("Copy", nullptr, false, canTarget)){
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
        if(ImGui::MenuItem("Rename", nullptr, false, canTarget)){
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
            std::error_code ec;
            const bool isDir = std::filesystem::is_directory(pendingDeleteAssetPath, ec);
            ImGui::Text("Delete %s?", isDir ? "directory" : "file");
            ImGui::TextWrapped("%s", pendingDeleteAssetPath.string().c_str());
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
                for(const auto& deletePath : deleteTargets){
                    std::error_code existsEc;
                    if(!std::filesystem::exists(deletePath, existsEc)){
                        continue;
                    }

                    File file(deletePath.string());
                    if(!file.deleteFile()){
                        hadDeleteError = true;
                        continue;
                    }

                    deletedAny = true;
                    clearDeletedPathState(deletePath);
                }

                if(deletedAny){
                    EditorAssetUI::InvalidateAllThumbnails();
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
                std::error_code ec;
                if(assetRenamePath.empty() || !std::filesystem::exists(assetRenamePath, ec)){
                    cancelAssetRename();
                }
            }

            ImGui::Text("Directory: %s", assetDir.string().c_str());
            ImGui::Separator();

            if(ImGui::Button("Pop Out Picker")){
                showAssetPickerWindow = true;
            }
            drawDeleteConfirmationPopup("Confirm Delete Asset Main");

            ImGui::Separator();

            constexpr float kFooterBottomPadding = 5.0f;
            float footerReserve = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + kFooterBottomPadding + 2.0f;
            ImGui::BeginChild("AssetList", ImVec2(0.0f, -footerReserve), false);
            bool openAssetContextMenu = false;

            if(std::filesystem::exists(assetDir)){
                std::vector<BrowserEntry> entries;
                std::unordered_map<std::string, std::filesystem::path> linkedByParent;
                collectBrowserEntries(entries, linkedByParent);

                float cellWidth = assetTileSize + 20.0f;
                int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellWidth));
                ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX;
                if(ImGui::BeginTable("AssetGridTable", columns, tableFlags)){
                    for(const auto& entry : entries){
                        EditorAssetUI::AssetTransaction tx{};
                        const auto& path = entry.path;
                        if(entry.isUp){
                            tx.absolutePath = path;
                            tx.assetRef = std::string(ASSET_DELIMITER) + "/..";
                            tx.extension.clear();
                            tx.kind = EditorAssetUI::AssetKind::Directory;
                            tx.isDirectory = true;
                        }else if(!EditorAssetUI::BuildTransaction(path, assetRoot, tx)){
                            continue;
                        }

                        ImGui::TableNextColumn();
                        ImGui::PushID(path.string().c_str());

                        EditorAssetUI::AssetTransaction drawTx = tx;
                        if(entry.forceDataIcon){
                            drawTx.kind = EditorAssetUI::AssetKind::Unknown;
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

                        const std::string pathKey = normalizedPathKey(path);
                        const bool hasRelatedChildren =
                            (!entry.isUp && !entry.isDirectory && !entry.isRelated && linkedByParent.find(pathKey) != linkedByParent.end());
                        bool toggledChildren = false;
                        if(hasRelatedChildren){
                            bool expanded = expandedRelatedAssets.find(pathKey) != expandedRelatedAssets.end();
                            if(drawRelatedToggle(assetTileSize, expanded)){
                                toggledChildren = true;
                                if(expanded){
                                    expandedRelatedAssets.erase(pathKey);
                                    auto linkedIt = linkedByParent.find(pathKey);
                                    if(linkedIt != linkedByParent.end() && selectedAssetPath == linkedIt->second){
                                        selectedAssetPath = path;
                                    }
                                }else{
                                    expandedRelatedAssets.insert(pathKey);
                                }
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
                            if(label.size() > 22){
                                label = label.substr(0, 19) + "...";
                            }
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
                        }else if(clicked){
                            selectedAssetPath = path;
                        }

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
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
                     ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                     !ImGui::IsAnyItemHovered()){
                contextMenuHasTarget = false;
                contextMenuTargetPath.clear();
                ImGui::OpenPopup("AssetContextMenuMain");
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
            ImGui::Separator();

            std::string latestLogLine = logLines.empty() ? std::string("No log output.") : logLines.back();
            if(latestLogLine.size() > 180){
                latestLogLine = latestLogLine.substr(0, 177) + "...";
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
                    label = std::string("Latest: ") + latestLogLine;
                }
            }else{
                label = std::string("Latest: ") + latestLogLine;
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

    if(showAssetPickerWindow){
        ImGui::SetNextWindowSize(ImVec2(760.0f, 500.0f), ImGuiCond_FirstUseEver);
        if(ImGui::Begin("Asset Picker", &showAssetPickerWindow)){
            ImGui::Text("Directory: %s", assetDir.string().c_str());
            if(ImGui::Button("Root")){
                assetDir = assetRoot;
            }
            ImGui::SameLine();
            if(ImGui::Button("Up") && assetDir != assetRoot){
                assetDir = assetDir.parent_path();
            }

            ImGui::Separator();
            float pickerTileSize = std::clamp(assetTileSize, 56.0f, 128.0f);
            bool openPickerContextMenu = false;
            if(std::filesystem::exists(assetDir)){
                std::vector<BrowserEntry> entries;
                std::unordered_map<std::string, std::filesystem::path> linkedByParent;
                collectBrowserEntries(entries, linkedByParent);

                float cellWidth = pickerTileSize + 20.0f;
                int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellWidth));
                if(ImGui::BeginTable("AssetPickerGridTable", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX)){
                    for(const auto& entry : entries){
                        EditorAssetUI::AssetTransaction tx{};
                        const auto& path = entry.path;
                        if(entry.isUp){
                            tx.absolutePath = path;
                            tx.assetRef = std::string(ASSET_DELIMITER) + "/..";
                            tx.extension.clear();
                            tx.kind = EditorAssetUI::AssetKind::Directory;
                            tx.isDirectory = true;
                        }else if(!EditorAssetUI::BuildTransaction(path, assetRoot, tx)){
                            continue;
                        }

                        ImGui::TableNextColumn();
                        ImGui::PushID(path.string().c_str());
                        EditorAssetUI::AssetTransaction drawTx = tx;
                        if(entry.forceDataIcon){
                            drawTx.kind = EditorAssetUI::AssetKind::Unknown;
                        }
                        bool selected = (path == selectedAssetPath);
                        bool doubleClicked = false;
                        bool clicked = EditorAssetUI::DrawAssetTile("asset_picker_tile", drawTx, pickerTileSize, selected, &doubleClicked);
                        ImVec2 tileMin = ImGui::GetItemRectMin();
                        ImVec2 tileMax = ImGui::GetItemRectMax();
                        bool tileRightClicked = ImGui::IsMouseHoveringRect(tileMin, tileMax, false) && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
                        if(tileRightClicked){
                            pickerContextMenuTargetPath = path;
                            pickerContextMenuHasTarget = true;
                            selectedAssetPath = path;
                            openPickerContextMenu = true;
                            clicked = false;
                            doubleClicked = false;
                        }
                        if(!entry.isUp){
                            EditorAssetUI::BeginAssetDragSource(tx);
                        }

                        const std::string pathKey = normalizedPathKey(path);
                        const bool hasRelatedChildren =
                            (!entry.isUp && !entry.isDirectory && !entry.isRelated && linkedByParent.find(pathKey) != linkedByParent.end());
                        bool toggledChildren = false;
                        if(hasRelatedChildren){
                            bool expanded = expandedRelatedAssets.find(pathKey) != expandedRelatedAssets.end();
                            if(drawRelatedToggle(pickerTileSize, expanded)){
                                toggledChildren = true;
                                if(expanded){
                                    expandedRelatedAssets.erase(pathKey);
                                    auto linkedIt = linkedByParent.find(pathKey);
                                    if(linkedIt != linkedByParent.end() && selectedAssetPath == linkedIt->second){
                                        selectedAssetPath = path;
                                    }
                                }else{
                                    expandedRelatedAssets.insert(pathKey);
                                }
                                clicked = false;
                                doubleClicked = false;
                            }
                        }

                        std::string label = formatEntryLabel(entry);
                        if(label.size() > 24){
                            label = label.substr(0, 21) + "...";
                        }
                        ImGui::TextWrapped("%s", label.c_str());
                        bool labelClicked = !toggledChildren && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                        bool labelDoubleClicked = !toggledChildren && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                        bool labelRightClicked = !toggledChildren && ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
                        clicked = clicked || labelClicked;
                        doubleClicked = doubleClicked || labelDoubleClicked;
                        if(labelRightClicked){
                            pickerContextMenuTargetPath = path;
                            pickerContextMenuHasTarget = true;
                            selectedAssetPath = path;
                            openPickerContextMenu = true;
                            clicked = false;
                            doubleClicked = false;
                        }

                        if(entry.isDirectory){
                            if(doubleClicked){
                                assetDir = path;
                                selectedAssetPath.clear();
                            }else if(clicked){
                                selectedAssetPath = path;
                            }
                        }else if(clicked){
                            selectedAssetPath = path;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }else{
                ImGui::TextUnformatted("Directory missing.");
            }

            if(openPickerContextMenu){
                ImGui::OpenPopup("AssetContextMenuPicker");
            }else if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
                     ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                     !ImGui::IsAnyItemHovered()){
                selectedAssetPath.clear();
                pickerContextMenuHasTarget = false;
                pickerContextMenuTargetPath.clear();
            }else if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
                     ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                     !ImGui::IsAnyItemHovered()){
                pickerContextMenuHasTarget = false;
                pickerContextMenuTargetPath.clear();
                ImGui::OpenPopup("AssetContextMenuPicker");
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
    }
}
