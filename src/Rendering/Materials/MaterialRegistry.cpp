/**
 * @file src/Rendering/Materials/MaterialRegistry.cpp
 * @brief Implementation for MaterialRegistry.
 */

#include "Rendering/Materials/MaterialRegistry.h"

#include "Assets/Core/Asset.h"
#include "Foundation/IO/File.h"
#include "Assets/Descriptors/MaterialAsset.h"
#include "Rendering/Materials/MaterialDefaults.h"
#include "Rendering/Materials/PBRMaterial.h"
#include "Foundation/Util/StringUtils.h"
#include "Rendering/Textures/Texture.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace {
    std::shared_ptr<Texture> getDefaultTexture(){
        static std::shared_ptr<Texture> tex;
        if(tex){
            return tex;
        }
        auto asset = AssetManager::Instance.getOrLoad(std::string(ASSET_DELIMITER) + "/images/uv.png");
        if(asset){
            tex = Texture::Load(asset);
        }
        if(!tex){
            tex = Texture::CreateEmpty(1, 1);
        }
        return tex;
    }
}

MaterialRegistry& MaterialRegistry::Instance(){
    static MaterialRegistry registry;
    return registry;
}

void MaterialRegistry::ensureBuiltIns(){
    if(builtInsInitialized){
        return;
    }

    builtInFactories.clear();
    builtInFactories.push_back({"Builtin/PBR", [](){
        return std::static_pointer_cast<Material>(PBRMaterial::Create(Color::WHITE));
    }});
    builtInFactories.push_back({"Builtin/Glass", [](){
        return std::static_pointer_cast<Material>(PBRMaterial::CreateGlass());
    }});
    builtInFactories.push_back({"Builtin/Water", [](){
        return std::static_pointer_cast<Material>(PBRMaterial::CreateWater());
    }});
    builtInFactories.push_back({"Builtin/Color", [](){
        return std::static_pointer_cast<Material>(MaterialDefaults::ColorMaterial::Create(Color::WHITE));
    }});
    builtInFactories.push_back({"Builtin/Image", [](){
        return std::static_pointer_cast<Material>(MaterialDefaults::ImageMaterial::Create(getDefaultTexture(), Color::WHITE, Math3D::Vec2(0.0f, 0.0f)));
    }});
    builtInFactories.push_back({"Builtin/LitColor", [](){
        return std::static_pointer_cast<Material>(MaterialDefaults::LitColorMaterial::Create(Color::WHITE));
    }});
    builtInFactories.push_back({"Builtin/LitImage", [](){
        return std::static_pointer_cast<Material>(MaterialDefaults::LitImageMaterial::Create(getDefaultTexture(), Color::WHITE));
    }});
    builtInFactories.push_back({"Builtin/FlatColor", [](){
        return std::static_pointer_cast<Material>(MaterialDefaults::FlatColorMaterial::Create(Color::WHITE));
    }});
    builtInFactories.push_back({"Builtin/FlatImage", [](){
        return std::static_pointer_cast<Material>(MaterialDefaults::FlatImageMaterial::Create(getDefaultTexture(), Color::WHITE));
    }});

    builtInsInitialized = true;
}

void MaterialRegistry::rebuildEntries(){
    ensureBuiltIns();
    entries.clear();

    for(const auto& builtIn : builtInFactories){
        MaterialRegistryEntry entry;
        entry.id = builtIn.first;
        entry.displayName = builtIn.first;
        entry.isBuiltIn = true;
        entries.push_back(entry);
    }

    std::filesystem::path assetRoot = std::filesystem::path(File::GetCWD()) / "res";
    std::error_code ec;
    if(!std::filesystem::exists(assetRoot, ec)){
        return;
    }

    for(const auto& fileEntry : std::filesystem::recursive_directory_iterator(assetRoot, std::filesystem::directory_options::skip_permission_denied, ec)){
        if(ec){
            continue;
        }
        if(!fileEntry.is_regular_file()){
            continue;
        }

        const std::filesystem::path& path = fileEntry.path();
        const bool isMaterialObject = MaterialAssetIO::IsMaterialObjectPath(path);
        const bool isMaterialAsset = MaterialAssetIO::IsMaterialAssetPath(path);
        if(!isMaterialObject && !isMaterialAsset){
            continue;
        }

        std::filesystem::path rel = path.lexically_relative(assetRoot);
        if(rel.empty() || StringUtils::BeginsWith(rel.generic_string(), "..")){
            continue;
        }

        if(isMaterialAsset){
            std::string lower = StringUtils::ToLowerCase(path.filename().string());
            std::string baseName = path.filename().string();
            if(StringUtils::EndsWith(lower, ".material.asset")){
                baseName = baseName.substr(0, baseName.size() - std::strlen(".material.asset"));
            }else if(StringUtils::EndsWith(lower, ".mat.asset")){
                baseName = baseName.substr(0, baseName.size() - std::strlen(".mat.asset"));
            }

            const std::filesystem::path siblingObject = path.parent_path() / (baseName + ".material");
            std::error_code siblingEc;
            if(std::filesystem::exists(siblingObject, siblingEc)){
                continue;
            }
        }else if(isMaterialObject){
            MaterialObjectData objectData;
            std::string objectError;
            if(!MaterialAssetIO::LoadMaterialObjectFromAbsolutePath(path, objectData, &objectError)){
                continue;
            }
            if(objectData.materialAssetRef.empty()){
                continue;
            }
        }

        MaterialRegistryEntry entry;
        entry.assetRef = std::string(ASSET_DELIMITER) + "/" + rel.generic_string();
        entry.id = (isMaterialObject ? "Material/" : "Asset/") + entry.assetRef;
        entry.displayName = (isMaterialObject ? "Material/" : "Asset/") + rel.generic_string();
        entry.isBuiltIn = false;
        entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const MaterialRegistryEntry& a, const MaterialRegistryEntry& b){
        if(a.isBuiltIn != b.isBuiltIn){
            return a.isBuiltIn > b.isBuiltIn;
        }
        return a.displayName < b.displayName;
    });
}

void MaterialRegistry::Refresh(bool force){
    auto now = std::chrono::steady_clock::now();
    if(!force && now < nextRefreshTime){
        return;
    }
    rebuildEntries();
    nextRefreshTime = now + std::chrono::seconds(1);
}

const std::vector<MaterialRegistryEntry>& MaterialRegistry::GetEntries() const{
    return entries;
}

std::shared_ptr<Material> MaterialRegistry::CreateById(const std::string& id, std::string* outError) const{
    const_cast<MaterialRegistry*>(this)->ensureBuiltIns();
    if(entries.empty()){
        const_cast<MaterialRegistry*>(this)->rebuildEntries();
    }

    for(const auto& builtIn : builtInFactories){
        if(builtIn.first == id){
            auto material = builtIn.second();
            if(!material && outError){
                *outError = "Failed to create built-in material: " + id;
            }
            return material;
        }
    }

    for(const auto& entry : entries){
        if(entry.id != id){
            continue;
        }
        if(entry.assetRef.empty()){
            break;
        }
        std::string error;
        auto material = MaterialAssetIO::InstantiateMaterialFromRef(entry.assetRef, nullptr, &error);
        if(!material && outError){
            *outError = error;
        }
        return material;
    }

    if(outError){
        *outError = "Material entry not found: " + id;
    }
    return nullptr;
}
