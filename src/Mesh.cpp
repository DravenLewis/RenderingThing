
#include "Mesh.h"

#include <stdexcept> 


void Mesh::upload(std::vector<Vertex> verts, std::vector<uint32_t> faces){
    this->faces = faces;
    this->verticies = verts;

    if(!_areBuffersBound()){
        _genBuffers();
    }

    if(!_areBuffersBound()){
        throw std::runtime_error("Cannot Bind. Unable to get valid VAO,VBO, or EBO");
    }
}

void Mesh::reload(){
    _genBuffers();
}

void Mesh::bind(){
    int id = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&id);
    if(this->VAO != 0 && id != this->VAO){
        glBindVertexArray(this->VAO);
    }
}

void Mesh::draw(const Math3D::Mat4& parent, const Math3D::Mat4& view, const Math3D::Mat4& projection){
    this->bind();
    glDrawElements(GL_TRIANGLES, this->faces.size(), GL_UNSIGNED_INT, 0);
}

Mesh::~Mesh(){
    dispose();
}

void Mesh::_genBuffers(){

    if(_areBuffersBound()){
        dispose(); // time to rebind.
    }

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    this->bind();

    std::vector<float> rawVerts = this->getRawVertexData();

    // Set Up VBO
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, rawVerts.size() * sizeof(float), rawVerts.data(), GL_STATIC_DRAW);

    // Set Up EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->faces.size() * sizeof(int), this->faces.data(), GL_STATIC_DRAW);

    this->setLocationAttributeOffset(0,3,0);  // Attribute 0, Width 3 (X,Y,Z), Offset 0
    this->setLocationAttributeOffset(1,4,3);  // Attribute 1, Width 4 (X,Y,Z), Offset 3
    this->setLocationAttributeOffset(2,3,7);  // Attribute 2, Width 5 (X,Y,Z), Offset 7
    this->setLocationAttributeOffset(3,2,10); // Attribute 3, Width 2 (X,Y,Z), Offset 10

    Mesh::Unbind();
    
}

bool Mesh::_areBuffersBound(){
    // Sketchy as FUK
    return (VBO != 0 && VAO != 0 && EBO != 0);
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

void Mesh::dispose(){
    glDeleteVertexArrays(1,&(this->VAO));
    glDeleteBuffers(1,&(this->VBO));
    glDeleteBuffers(1,&(this->EBO));
}