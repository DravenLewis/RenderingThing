/**
 * @file src/Rendering/Geometry/Mesh.cpp
 * @brief Implementation for Mesh.
 */


#include "Rendering/Geometry/Mesh.h"

#include <stdexcept> 
#include <cfloat>
#include <cmath>
#include <utility>


void Mesh::upload(const std::vector<Vertex>& verts, const std::vector<uint32_t>& faces, GLenum usage){
    this->faces = faces;
    this->verticies = verts;
    computeTangents();
    computeLocalBounds();

    if(!_areBuffersBound()){
        _genBuffers(usage);
    }else{
        this->bind();

        std::vector<float> rawVerts = this->getRawVertexData();

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, rawVerts.size() * sizeof(float), rawVerts.data(), usage);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->faces.size() * sizeof(int), this->faces.data(), usage);

        if(tangentVBO == 0){
            glGenBuffers(1, &tangentVBO);
            glBindBuffer(GL_ARRAY_BUFFER, tangentVBO);
            glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Math3D::Vec4), (void*)0);
            glEnableVertexAttribArray(4);
        }else{
            glBindBuffer(GL_ARRAY_BUFFER, tangentVBO);
        }
        glBufferData(GL_ARRAY_BUFFER, tangents.size() * sizeof(Math3D::Vec4), tangents.data(), usage);

        Mesh::Unbind();
    }

    if(!_areBuffersBound()){
        throw std::runtime_error("Cannot Bind. Unable to get valid VAO,VBO, or EBO");
    }
}

void Mesh::upload(std::vector<Vertex>&& verts, std::vector<uint32_t>&& faces, GLenum usage){
    this->faces = std::move(faces);
    this->verticies = std::move(verts);
    computeTangents();
    computeLocalBounds();

    if(!_areBuffersBound()){
        _genBuffers(usage);
    }else{
        this->bind();

        std::vector<float> rawVerts = this->getRawVertexData();

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, rawVerts.size() * sizeof(float), rawVerts.data(), usage);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->faces.size() * sizeof(int), this->faces.data(), usage);

        if(tangentVBO == 0){
            glGenBuffers(1, &tangentVBO);
            glBindBuffer(GL_ARRAY_BUFFER, tangentVBO);
            glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Math3D::Vec4), (void*)0);
            glEnableVertexAttribArray(4);
        }else{
            glBindBuffer(GL_ARRAY_BUFFER, tangentVBO);
        }
        glBufferData(GL_ARRAY_BUFFER, tangents.size() * sizeof(Math3D::Vec4), tangents.data(), usage);

        Mesh::Unbind();
    }

    if(!_areBuffersBound()){
        throw std::runtime_error("Cannot Bind. Unable to get valid VAO,VBO, or EBO");
    }
}

void Mesh::reload(){
    _genBuffers(GL_STATIC_DRAW);
}

namespace {
    GLuint g_lastBoundVao = 0;

    glm::vec3 safeNormalizeVec3(const glm::vec3& value, const glm::vec3& fallback){
        float lenSq = glm::dot(value, value);
        if(lenSq <= 1e-12f){
            return fallback;
        }
        return value * glm::inversesqrt(lenSq);
    }

    glm::vec3 fallbackTangentForNormal(const glm::vec3& normal){
        glm::vec3 n = safeNormalizeVec3(normal, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 axis = (std::abs(n.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 tangent = glm::cross(axis, n);
        float tangentLenSq = glm::dot(tangent, tangent);
        if(tangentLenSq <= 1e-12f){
            tangent = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), n);
            tangentLenSq = glm::dot(tangent, tangent);
            if(tangentLenSq <= 1e-12f){
                return glm::vec3(1.0f, 0.0f, 0.0f);
            }
        }
        return tangent * glm::inversesqrt(tangentLenSq);
    }
}

void Mesh::bind(){
    if(this->VAO != 0 && g_lastBoundVao != this->VAO){
        glBindVertexArray(this->VAO);
        g_lastBoundVao = this->VAO;
    }
}

void Mesh::draw(const Math3D::Mat4& parent, const Math3D::Mat4& view, const Math3D::Mat4& projection){
    this->bind();
    glDrawElements(GL_TRIANGLES, this->faces.size(), GL_UNSIGNED_INT, 0);
}

Mesh::~Mesh(){
    dispose();
}

void Mesh::_genBuffers(GLenum usage){

    if(_areBuffersBound()){
        dispose(); // time to rebind.
    }

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &tangentVBO);
    glGenBuffers(1, &EBO);

    this->bind();

    std::vector<float> rawVerts = this->getRawVertexData();

    // Set Up VBO
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, rawVerts.size() * sizeof(float), rawVerts.data(), usage);

    // Set Up EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->faces.size() * sizeof(int), this->faces.data(), usage);

    // Set up tangent stream (location 4) for stable tangent-space normal mapping.
    glBindBuffer(GL_ARRAY_BUFFER, tangentVBO);
    glBufferData(GL_ARRAY_BUFFER, tangents.size() * sizeof(Math3D::Vec4), tangents.data(), usage);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Math3D::Vec4), (void*)0);
    glEnableVertexAttribArray(4);

    // Rebind primary vertex stream before configuring attributes 0..3.
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    this->setLocationAttributeOffset(0,3,0);  // Attribute 0, Width 3 (X,Y,Z), Offset 0
    this->setLocationAttributeOffset(1,4,3);  // Attribute 1, Width 4 (X,Y,Z), Offset 3
    this->setLocationAttributeOffset(2,3,7);  // Attribute 2, Width 5 (X,Y,Z), Offset 7
    this->setLocationAttributeOffset(3,2,10); // Attribute 3, Width 2 (X,Y,Z), Offset 10

    Mesh::Unbind();
    
}

bool Mesh::_areBuffersBound(){
    // Sketchy as FUK
    return (VBO != 0 && tangentVBO != 0 && VAO != 0 && EBO != 0);
}

void Mesh::computeLocalBounds(){
    if(verticies.empty()){
        hasLocalBounds = false;
        localBoundsMin = Math3D::Vec3(0.0f, 0.0f, 0.0f);
        localBoundsMax = Math3D::Vec3(0.0f, 0.0f, 0.0f);
        return;
    }

    float minX = FLT_MAX;
    float minY = FLT_MAX;
    float minZ = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxY = -FLT_MAX;
    float maxZ = -FLT_MAX;

    for(const auto& vtx : verticies){
        minX = std::min(minX, vtx.Position.x);
        minY = std::min(minY, vtx.Position.y);
        minZ = std::min(minZ, vtx.Position.z);
        maxX = std::max(maxX, vtx.Position.x);
        maxY = std::max(maxY, vtx.Position.y);
        maxZ = std::max(maxZ, vtx.Position.z);
    }

    localBoundsMin = Math3D::Vec3(minX, minY, minZ);
    localBoundsMax = Math3D::Vec3(maxX, maxY, maxZ);
    hasLocalBounds = true;
}

void Mesh::computeTangents(){
    tangents.assign(verticies.size(), Math3D::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    if(verticies.empty() || faces.empty()){
        return;
    }

    std::vector<glm::vec3> tangentAccum(verticies.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangentAccum(verticies.size(), glm::vec3(0.0f));

    for(size_t i = 0; i + 2 < faces.size(); i += 3){
        uint32_t i0 = faces[i];
        uint32_t i1 = faces[i + 1];
        uint32_t i2 = faces[i + 2];
        if(i0 >= verticies.size() || i1 >= verticies.size() || i2 >= verticies.size()){
            continue;
        }

        const Vertex& v0 = verticies[i0];
        const Vertex& v1 = verticies[i1];
        const Vertex& v2 = verticies[i2];

        glm::vec3 p0 = (glm::vec3)v0.Position;
        glm::vec3 p1 = (glm::vec3)v1.Position;
        glm::vec3 p2 = (glm::vec3)v2.Position;

        glm::vec2 uv0 = (glm::vec2)v0.TexCoords;
        glm::vec2 uv1 = (glm::vec2)v1.TexCoords;
        glm::vec2 uv2 = (glm::vec2)v2.TexCoords;

        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        glm::vec2 dUV1 = uv1 - uv0;
        glm::vec2 dUV2 = uv2 - uv0;

        float det = (dUV1.x * dUV2.y) - (dUV1.y * dUV2.x);
        if(std::abs(det) <= 1e-12f){
            continue;
        }

        float invDet = 1.0f / det;
        glm::vec3 tangent = (edge1 * dUV2.y - edge2 * dUV1.y) * invDet;
        glm::vec3 bitangent = (edge2 * dUV1.x - edge1 * dUV2.x) * invDet;

        tangentAccum[i0] += tangent;
        tangentAccum[i1] += tangent;
        tangentAccum[i2] += tangent;

        bitangentAccum[i0] += bitangent;
        bitangentAccum[i1] += bitangent;
        bitangentAccum[i2] += bitangent;
    }

    for(size_t i = 0; i < verticies.size(); ++i){
        glm::vec3 normal = safeNormalizeVec3((glm::vec3)verticies[i].Normal, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 tangent = tangentAccum[i];
        tangent = tangent - normal * glm::dot(normal, tangent);
        tangent = safeNormalizeVec3(tangent, fallbackTangentForNormal(normal));

        float handedness = 1.0f;
        glm::vec3 bitangent = bitangentAccum[i];
        if(glm::dot(bitangent, bitangent) > 1e-12f){
            handedness = (glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f) ? -1.0f : 1.0f;
        }

        tangents[i] = Math3D::Vec4(Math3D::Vec3(tangent), handedness);
    }
}

std::vector<Math3D::Vec3> Mesh::getVerticiesVectorArray(){
    std::vector<Math3D::Vec3> list;
    for(int i = 0; i < this->verticies.size(); i++){
        Vertex v = this->verticies[i];
        list.push_back(v.Position);
    }
    return list;
}

std::vector<float> Mesh::getVerticiesAsRawFloatArray(){
    std::vector<float> floatArr;
    std::vector<Math3D::Vec3> vec3s = this->getVerticiesVectorArray();
    for(int i = 0; i < vec3s.size(); i++){
        floatArr.push_back(vec3s[i].x);
        floatArr.push_back(vec3s[i].y);
        floatArr.push_back(vec3s[i].z);
    }
    return floatArr;
}

std::vector<float> Mesh::getRawVertexData(){
   std::vector<float> fullData;
   for(int i = 0; i < this->verticies.size(); i++){
        Vertex vtx = this->verticies[i];
        std::vector<float> dVtx = Vertex::Decompose(vtx);
        for(int j = 0; j < dVtx.size(); j++){
            fullData.push_back(dVtx[j]);
        }
   } 
   return fullData;    
}

void Mesh::setLocationAttributeOffset(int attribute, int dataSize, int dataOffset){
    glVertexAttribPointer(attribute, dataSize, GL_FLOAT, GL_FALSE, Vertex::VERTEX_DATA_WIDTH * sizeof(float), (void*) (dataOffset * sizeof(float)));
    glEnableVertexAttribArray(attribute);
}

bool Mesh::getLocalBounds(Math3D::Vec3& outMin, Math3D::Vec3& outMax) const{
    if(!hasLocalBounds){
        return false;
    }
    outMin = localBoundsMin;
    outMax = localBoundsMax;
    return true;
}

void Mesh::dispose(){
    if(g_lastBoundVao == this->VAO){
        g_lastBoundVao = 0;
    }
    glDeleteVertexArrays(1,&(this->VAO));
    glDeleteBuffers(1,&(this->VBO));
    glDeleteBuffers(1,&(this->tangentVBO));
    glDeleteBuffers(1,&(this->EBO));
}

void Mesh::Unbind(){
    glBindVertexArray(0);
    g_lastBoundVao = 0;
}
