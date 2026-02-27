#include "Assets/Importers/OBJLoader.h"

#include "Foundation/Logging/Logbot.h"
#include "Assets/Descriptors/MaterialAsset.h"
#include "Assets/Core/AssetDescriptorUtils.h"
#include "Rendering/Materials/MaterialDefaults.h"
#include "Assets/Importers/MtlMaterialImporter.h"
#include "Foundation/Util/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <sstream>
#include <tuple>
#include <unordered_map>

#include <glm/glm.hpp>

namespace {
    std::string trimCopy(const std::string& value){
        return StringUtils::Trim(value);
    }

    std::string toLowerCopy(const std::string& value){
        return StringUtils::ToLowerCase(value);
    }

    int parseObjIndex(const std::string& token, int count){
        if(token.empty()){
            return -1;
        }
        int raw = 0;
        try{
            raw = std::stoi(token);
        }catch(...){
            return -1;
        }

        if(raw > 0){
            return raw - 1;
        }
        if(raw < 0){
            return count + raw;
        }
        return -1;
    }

    std::vector<std::string> parseMtllibTokens(const std::string& raw){
        std::vector<std::string> out;
        std::string current;
        bool inQuotes = false;
        for(char c : raw){
            if(c == '"'){
                inQuotes = !inQuotes;
                continue;
            }
            if(!inQuotes && std::isspace(static_cast<unsigned char>(c)) != 0){
                if(!current.empty()){
                    out.push_back(current);
                    current.clear();
                }
                continue;
            }
            current.push_back(c);
        }
        if(!current.empty()){
            out.push_back(current);
        }
        return out;
    }

    std::filesystem::path normalizePath(const std::filesystem::path& path){
        std::error_code ec;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
        if(ec){
            normalized = path.lexically_normal();
        }
        return normalized;
    }

    std::filesystem::path getAssetAbsolutePath(const PAsset& asset){
        if(!asset){
            return {};
        }
        auto& fileHandle = asset->getFileHandle();
        if(!fileHandle){
            return {};
        }
        return normalizePath(std::filesystem::path(fileHandle->getPath()));
    }
}

OBJLoader::OBJData OBJLoader::ParseOBJData(const std::string& content){
    OBJData data;
    std::istringstream stream(content);
    std::string line;
    std::string currentMaterialName;

    while(std::getline(stream, line)){
        std::string trimmed = trimCopy(line);
        if(trimmed.empty() || trimmed[0] == '#'){
            continue;
        }

        std::istringstream iss(trimmed);
        std::string prefix;
        iss >> prefix;

        if(prefix == "v"){
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            iss >> x >> y >> z;
            data.positions.push_back(Math3D::Vec3(x, y, z));
        }else if(prefix == "vn"){
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            iss >> x >> y >> z;
            data.normals.push_back(Math3D::Vec3(x, y, z));
        }else if(prefix == "vt"){
            float u = 0.0f;
            float v = 0.0f;
            iss >> u >> v;
            data.texCoords.push_back(Math3D::Vec2(u, v));
        }else if(prefix == "usemtl"){
            std::string materialName;
            std::getline(iss, materialName);
            currentMaterialName = trimCopy(materialName);
        }else if(prefix == "mtllib"){
            std::string libRemainder;
            std::getline(iss, libRemainder);
            for(const std::string& token : parseMtllibTokens(libRemainder)){
                if(!token.empty()){
                    data.materialLibs.push_back(token);
                }
            }
        }else if(prefix == "f"){
            FaceDefinition face;
            face.materialName = currentMaterialName;

            std::string vertexToken;
            while(iss >> vertexToken){
                std::vector<std::string> parts = StringUtils::Split(vertexToken, "/");

                VertexIndex idx;
                if(!parts.empty() && !parts[0].empty()){
                    idx.posIdx = parseObjIndex(parts[0], static_cast<int>(data.positions.size()));
                }
                if(parts.size() > 1 && !parts[1].empty()){
                    idx.texIdx = parseObjIndex(parts[1], static_cast<int>(data.texCoords.size()));
                }
                if(parts.size() > 2 && !parts[2].empty()){
                    idx.normIdx = parseObjIndex(parts[2], static_cast<int>(data.normals.size()));
                }

                if(idx.posIdx >= 0 && idx.posIdx < static_cast<int>(data.positions.size())){
                    face.vertices.push_back(idx);
                }
            }

            if(face.vertices.size() >= 3){
                data.faces.push_back(std::move(face));
            }
        }
    }

    return data;
}

std::vector<Math3D::Vec3> OBJLoader::BuildSmoothNormals(const OBJData& data){
    std::vector<Math3D::Vec3> smoothNormals(data.positions.size(), Math3D::Vec3(0.0f, 0.0f, 0.0f));
    if(data.positions.empty()){
        return smoothNormals;
    }

    auto accumulateTriangleNormal = [&](int i0, int i1, int i2){
        if(i0 < 0 || i1 < 0 || i2 < 0){
            return;
        }
        if(i0 >= static_cast<int>(data.positions.size()) ||
           i1 >= static_cast<int>(data.positions.size()) ||
           i2 >= static_cast<int>(data.positions.size())){
            return;
        }

        const glm::vec3 p0 = static_cast<glm::vec3>(data.positions[i0]);
        const glm::vec3 p1 = static_cast<glm::vec3>(data.positions[i1]);
        const glm::vec3 p2 = static_cast<glm::vec3>(data.positions[i2]);
        const glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);
        if(glm::dot(faceNormal, faceNormal) <= 1e-12f){
            return;
        }

        const Math3D::Vec3 n(faceNormal);
        smoothNormals[i0] += n;
        smoothNormals[i1] += n;
        smoothNormals[i2] += n;
    };

    for(const auto& face : data.faces){
        if(face.vertices.size() < 3){
            continue;
        }

        const int base = face.vertices[0].posIdx;
        for(size_t i = 1; i + 1 < face.vertices.size(); ++i){
            accumulateTriangleNormal(base, face.vertices[i].posIdx, face.vertices[i + 1].posIdx);
        }
    }

    for(auto& n : smoothNormals){
        if(n.length() > 1e-6f){
            n = n.normalize();
        }else{
            n = Math3D::Vec3(0.0f, 1.0f, 0.0f);
        }
    }

    return smoothNormals;
}

std::shared_ptr<Model> OBJLoader::LoadFromAsset(PAsset asset, PMaterial material, bool forceSmoothNormals){
    if(!asset){
        LogBot.Log(LOG_ERRO, "OBJLoader::LoadFromAsset - Asset is null");
        return nullptr;
    }

    if(!asset->loaded()){
        LogBot.Log(LOG_ERRO, "OBJLoader::LoadFromAsset - Asset not loaded");
        return nullptr;
    }

    const std::string objContent = asset->asString();
    if(objContent.empty()){
        LogBot.Log(LOG_ERRO, "OBJLoader::LoadFromAsset - Asset content is empty");
        return nullptr;
    }

    OBJData data = ParseOBJData(objContent);
    if(data.positions.empty()){
        LogBot.Log(LOG_ERRO, "OBJLoader::LoadFromAsset - No vertices found in OBJ data");
        return nullptr;
    }
    if(data.faces.empty()){
        LogBot.Log(LOG_ERRO, "OBJLoader::LoadFromAsset - No faces found in OBJ data");
        return nullptr;
    }

    std::vector<Math3D::Vec3> smoothNormals;
    if(forceSmoothNormals){
        smoothNormals = BuildSmoothNormals(data);
    }

    std::shared_ptr<Material> fallbackMaterial = material;
    if(!fallbackMaterial){
        fallbackMaterial = MaterialDefaults::LitColorMaterial::Create(Color::WHITE);
    }

    const std::filesystem::path objPath = getAssetAbsolutePath(asset);
    auto loadObjMaterials = [&]() -> std::unordered_map<std::string, std::shared_ptr<Material>> {
        std::unordered_map<std::string, std::shared_ptr<Material>> materials;
        if(data.materialLibs.empty()){
            return materials;
        }

        const std::filesystem::path objDir = objPath.parent_path();
        for(const std::string& libToken : data.materialLibs){
            std::filesystem::path mtlPath(libToken);
            if(!mtlPath.is_absolute()){
                mtlPath = objDir / mtlPath;
            }
            mtlPath = normalizePath(mtlPath);

            std::vector<MtlMaterialDefinition> defs;
            std::string error;
            if(!MtlMaterialImporter::LoadFromAbsolutePath(mtlPath, defs, &error)){
                if(error.empty()){
                    error = "unknown error";
                }
                LogBot.Log(LOG_WARN,
                           "OBJLoader::LoadFromAsset - Failed to load material library '%s': %s",
                           mtlPath.string().c_str(),
                           error.c_str());
                continue;
            }

            for(const auto& def : defs){
                MaterialAssetData importedData;
                std::string convertError;
                if(!MtlMaterialImporter::BuildMaterialAssetData(def, importedData, &convertError)){
                    continue;
                }
                importedData.name = def.name;

                std::string instantiateError;
                auto importedMaterial = MaterialAssetIO::InstantiateMaterial(importedData, &instantiateError);
                if(!importedMaterial){
                    continue;
                }

                const std::string key = toLowerCopy(trimCopy(def.name));
                if(key.empty()){
                    continue;
                }
                materials[key] = importedMaterial;
            }
        }

        return materials;
    };

    const std::unordered_map<std::string, std::shared_ptr<Material>> importedMaterials = loadObjMaterials();

    struct PartBuildState {
        ModelPartFactory factory;
        std::map<std::tuple<int, int, int>, int> vertexMap;
        bool initialized = false;
        size_t faceCount = 0;
    };

    std::map<std::string, PartBuildState> partStates;

    auto resolvePartMaterial = [&](const std::string& rawMaterialName) -> std::pair<std::string, std::shared_ptr<Material>> {
        const std::string materialName = trimCopy(rawMaterialName);
        if(materialName.empty()){
            return {"__default__", fallbackMaterial};
        }

        const std::string lookupKey = toLowerCopy(materialName);
        auto importedIt = importedMaterials.find(lookupKey);
        if(importedIt != importedMaterials.end() && importedIt->second){
            return {std::string("mat:") + lookupKey, importedIt->second};
        }

        // Keep unknown material slots separated so part-level splits still match usemtl groups.
        return {std::string("missing:") + lookupKey, fallbackMaterial};
    };

    for(const auto& face : data.faces){
        if(face.vertices.size() < 3){
            continue;
        }

        auto materialInfo = resolvePartMaterial(face.materialName);
        auto& partState = partStates[materialInfo.first];
        if(!partState.initialized){
            partState.factory = ModelPartFactory::Create(materialInfo.second);
            partState.initialized = true;
        }

        std::vector<int> faceIndices;
        faceIndices.reserve(face.vertices.size());
        for(const auto& vidx : face.vertices){
            if(vidx.posIdx < 0 || vidx.posIdx >= static_cast<int>(data.positions.size())){
                continue;
            }

            const auto key = std::make_tuple(vidx.posIdx, vidx.texIdx, vidx.normIdx);
            auto existing = partState.vertexMap.find(key);
            if(existing == partState.vertexMap.end()){
                Vertex vtx = Vertex::Build();
                vtx.Position = data.positions[vidx.posIdx];

                if(forceSmoothNormals && vidx.posIdx >= 0 && vidx.posIdx < static_cast<int>(smoothNormals.size())){
                    vtx.Normal = smoothNormals[vidx.posIdx];
                }else if(vidx.normIdx >= 0 && vidx.normIdx < static_cast<int>(data.normals.size())){
                    vtx.Normal = data.normals[vidx.normIdx];
                }else{
                    vtx.Normal = Math3D::Vec3(0.0f, 1.0f, 0.0f);
                }

                if(vidx.texIdx >= 0 && vidx.texIdx < static_cast<int>(data.texCoords.size())){
                    vtx.TexCoords = data.texCoords[vidx.texIdx];
                }else{
                    vtx.TexCoords = Math3D::Vec2(0.0f, 0.0f);
                }

                vtx.Color = Math3D::Vec4(1.0f, 1.0f, 1.0f, 1.0f);

                int index = -1;
                partState.factory.addVertex(vtx, &index);
                partState.vertexMap[key] = index;
                faceIndices.push_back(index);
            }else{
                faceIndices.push_back(existing->second);
            }
        }

        if(faceIndices.size() < 3){
            continue;
        }

        if(faceIndices.size() == 3){
            partState.factory.defineFace(faceIndices[0], faceIndices[1], faceIndices[2]);
        }else if(faceIndices.size() == 4){
            partState.factory.defineFace(faceIndices[0], faceIndices[1], faceIndices[2], faceIndices[3]);
        }else{
            for(size_t i = 1; i + 1 < faceIndices.size(); ++i){
                partState.factory.defineFace(faceIndices[0], faceIndices[i], faceIndices[i + 1]);
            }
        }

        partState.faceCount++;
    }

    auto model = Model::Create();
    size_t totalVertexCount = 0;
    for(auto& kv : partStates){
        PartBuildState& state = kv.second;
        if(!state.initialized || state.faceCount == 0){
            continue;
        }

        totalVertexCount += state.vertexMap.size();
        auto part = state.factory.assemble();
        if(part){
            model->addPart(part);
        }
    }

    if(model->getParts().empty()){
        LogBot.Log(LOG_ERRO, "OBJLoader::LoadFromAsset - Failed to construct model parts");
        return nullptr;
    }

    if(!objPath.empty()){
        model->setSourceAssetRef(AssetDescriptorUtils::AbsolutePathToAssetRef(objPath));
    }
    model->setSourceForceSmoothNormals(forceSmoothNormals);

    LogBot.Log(
        LOG_INFO,
        "OBJLoader::LoadFromAsset - Successfully loaded OBJ with " +
        std::to_string(totalVertexCount) +
        " vertices across " +
        std::to_string(model->getParts().size()) +
        " parts"
    );

    return model;
}
