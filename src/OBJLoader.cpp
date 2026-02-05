#include "OBJLoader.h"
#include "StringUtils.h"
#include "MaterialDefaults.h"
#include "LogBot.h"

#include <sstream>
#include <algorithm>
#include <map>
#include <tuple>
#include <glm/glm.hpp>

OBJLoader::OBJData OBJLoader::ParseOBJData(const std::string& content) {
    OBJData data;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            // Vertex position
            float x, y, z;
            iss >> x >> y >> z;
            data.positions.push_back(Math3D::Vec3(x, y, z));
        }
        else if (prefix == "vn") {
            // Vertex normal
            float x, y, z;
            iss >> x >> y >> z;
            data.normals.push_back(Math3D::Vec3(x, y, z));
        }
        else if (prefix == "vt") {
            // Texture coordinate
            float u, v;
            iss >> u >> v;
            data.texCoords.push_back(Math3D::Vec2(u, v));
        }
        else if (prefix == "f") {
            // Face definition
            std::vector<VertexIndex> face;
            std::string vertexStr;

            while (iss >> vertexStr) {
                // Parse vertex reference (e.g., "1/2/3" or "1//3" or "1")
                std::vector<std::string> parts = StringUtils::Split(vertexStr, "/");

                VertexIndex idx;
                if (!parts.empty() && !parts[0].empty()) {
                    idx.posIdx = std::stoi(parts[0]) - 1;  // OBJ uses 1-based indexing
                }
                if (parts.size() > 1 && !parts[1].empty()) {
                    idx.texIdx = std::stoi(parts[1]) - 1;
                }
                if (parts.size() > 2 && !parts[2].empty()) {
                    idx.normIdx = std::stoi(parts[2]) - 1;
                }
                
                if (idx.posIdx >= 0) {
                    face.push_back(idx);
                }
            }

            if (face.size() >= 3) {
                data.faces.push_back(face);
            }
        }
    }

    return data;
}

std::vector<Vertex> OBJLoader::ConstructVertices(const OBJData& data) {
    std::vector<Vertex> vertices;

    for (const auto& face : data.faces) {
        for (const auto& vidx : face) {
            if (vidx.posIdx < 0 || vidx.posIdx >= static_cast<int>(data.positions.size())) {
                continue;
            }

            Vertex vtx = Vertex::Build();
            vtx.Position = data.positions[vidx.posIdx];

            // Get normal from OBJ if available
            if (vidx.normIdx >= 0 && vidx.normIdx < static_cast<int>(data.normals.size())) {
                vtx.Normal = data.normals[vidx.normIdx];
            } else {
                vtx.Normal = Math3D::Vec3(0, 1, 0);
            }

            // Get texture coordinate if available
            if (vidx.texIdx >= 0 && vidx.texIdx < static_cast<int>(data.texCoords.size())) {
                vtx.TexCoords = data.texCoords[vidx.texIdx];
            } else {
                vtx.TexCoords = Math3D::Vec2(0, 0);
            }

            vtx.Color = Math3D::Vec4(1, 1, 1, 1);

            vertices.push_back(vtx);
        }
    }

    return vertices;
}

std::shared_ptr<Model> OBJLoader::LoadFromAsset(PAsset asset, PMaterial material) {

    if (!asset) {
        LogBot.Log(LOG_ERRO,"OBJLoader::LoadFromAsset - Asset is null");
        return nullptr;
    }

    //LogBot.LogBasic("Got Here 2");

    if (!asset->loaded()) {
        LogBot.Log(LOG_ERRO,"OBJLoader::LoadFromAsset - Asset not loaded");
        return nullptr;
    }

    //LogBot.LogBasic("Got Here 3");

    std::string objContent = asset->asString();

    //LogBot.LogBasic("Got Here 4");

    if (objContent.empty()) {
        LogBot.Log(LOG_ERRO,"OBJLoader::LoadFromAsset - Asset content is empty");
        return nullptr;
    }

    //LogBot.LogBasic("Got Here 5");

    OBJData data = ParseOBJData(objContent);

    //LogBot.LogBasic("Got Here 6");

    if (data.positions.empty()) {
        LogBot.Log(LOG_ERRO,"OBJLoader::LoadFromAsset - No vertices found in OBJ data");
        return nullptr;
    }

    //LogBot.LogBasic("Got Here 7");

    std::vector<Vertex> vertices = ConstructVertices(data);

    //LogBot.LogBasic("Got Here 8");

    if (vertices.empty()) {
        LogBot.Log(LOG_ERRO,"OBJLoader::LoadFromAsset - Failed to construct vertices");
        return nullptr;
    }

    //LogBot.LogBasic("Got Here 9");

    // Create model with single part
    auto factory = ModelPartFactory::Create(material);

    // Map from OBJ vertex indices to our vertex buffer indices
    std::map<std::tuple<int, int, int>, int> vertexMap;
    //int nextVertexIndex = 0;

    // Add all vertices and define faces using the OBJ face data
    for (const auto& face : data.faces) {
        std::vector<int> faceIndices;

        for (const auto& vidx : face) {
            // Create a unique key for this vertex combination
            auto key = std::make_tuple(vidx.posIdx, vidx.texIdx, vidx.normIdx);

            // Check if we've already added this vertex
            if (vertexMap.find(key) == vertexMap.end()) {
                // Create and add new vertex
                if (vidx.posIdx < 0 || vidx.posIdx >= static_cast<int>(data.positions.size())) {
                    continue;
                }

                Vertex vtx = Vertex::Build();
                vtx.Position = data.positions[vidx.posIdx];

                // Get normal from OBJ if available
                if (vidx.normIdx >= 0 && vidx.normIdx < static_cast<int>(data.normals.size())) {
                    vtx.Normal = data.normals[vidx.normIdx];
                } else {
                    vtx.Normal = Math3D::Vec3(0, 1, 0);
                }

                // Get texture coordinate if available
                if (vidx.texIdx >= 0 && vidx.texIdx < static_cast<int>(data.texCoords.size())) {
                    vtx.TexCoords = data.texCoords[vidx.texIdx];
                } else {
                    vtx.TexCoords = Math3D::Vec2(0, 0);
                }

                vtx.Color = Math3D::Vec4(1, 1, 1, 1);

                //int idx = nextVertexIndex;
                //factory.addVertex(vtx, &idx);
                //vertexMap[key] = nextVertexIndex;
                //nextVertexIndex++;

                int idx = -1;
                factory.addVertex(vtx, &idx);
                vertexMap[key] = idx;
            }

            faceIndices.push_back(vertexMap[key]);
        }

        // Define face based on number of vertices
        if (faceIndices.size() == 3) {
            factory.defineFace(faceIndices[0], faceIndices[1], faceIndices[2]);
        } else if (faceIndices.size() == 4) {
            factory.defineFace(faceIndices[0], faceIndices[1], faceIndices[2], faceIndices[3]);
        } else if (faceIndices.size() > 4) {
            // Triangulate polygons
            for (size_t i = 1; i < faceIndices.size() - 1; ++i) {
                factory.defineFace(faceIndices[0], faceIndices[i], faceIndices[i + 1]);
            }
        }
    }

    auto part = factory.assemble();
    auto model = Model::Create();
    model->addPart(part);

    LogBot.Log(LOG_INFO,"OBJLoader::LoadFromAsset - Successfully loaded OBJ with " + std::to_string(vertexMap.size()) + " vertices");

    return model;
}

