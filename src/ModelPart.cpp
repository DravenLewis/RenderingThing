
#include "ModelPart.h"

#include "Logbot.h"
#include "ShadowRenderer.h"

void ModelPart::draw(const Math3D::Mat4& parent, const Math3D::Mat4& view, const Math3D::Mat4& proj){
    if(!mesh) return;

    Math3D::Mat4 worldMatrix = parent * localTransform.toMat4();

    if(material){
        material->set<Math3D::Mat4>("u_model", worldMatrix);
        // Can become a UBO eventually.
        material->set<Math3D::Mat4>("u_view", view);
        material->set<Math3D::Mat4>("u_projection",proj);

        material->bind();
    }

    mesh->draw();

    if(material){
        material->unbind();
    }
};

ModelPartFactory ModelPartFactory::Create(std::shared_ptr<Material> material){
    ModelPartFactory factory;
        factory.materialInstancePtr = material;
        factory.meshInstancePtr = std::make_shared<Mesh>();
    return factory;
}

ModelPartFactory& ModelPartFactory::addVertex(Vertex vtx, int* index){
    /*auto it = std::find(this->vertexCache.begin(), this->vertexCache.end(), vtx);
    if(it != this->vertexCache.end()){
        *(index) = std::distance(this->vertexCache.begin(), it);
    }*/

    this->vertexCache.push_back(vtx);
    if(index){
        *index = static_cast<int>(this->vertexCache.size() - 1);
    }

    return *this;
}

ModelPartFactory& ModelPartFactory::defineFace(int vtx1, int vtx2, int vtx3, int vtx4){
    if(vtx4 == -1){ // Build Face From Triangle Only.
        this->faceCache.push_back(vtx1);
        this->faceCache.push_back(vtx2);
        this->faceCache.push_back(vtx3);
    }else{ // Build as Quad
        this->faceCache.push_back(vtx1);
        this->faceCache.push_back(vtx2);
        this->faceCache.push_back(vtx3);
        this->faceCache.push_back(vtx3);
        this->faceCache.push_back(vtx4);
        this->faceCache.push_back(vtx1);
    }
    return *this;
}

std::shared_ptr<ModelPart> ModelPartFactory::assemble(){

    if(this->meshInstancePtr){
        this->meshInstancePtr->upload(this->vertexCache,this->faceCache);
    }

    auto part = std::make_shared<ModelPart>();
    part->material = this->materialInstancePtr;
    part->mesh = this->meshInstancePtr;
    return part;
}

