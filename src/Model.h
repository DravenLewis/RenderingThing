#ifndef MODEL_H
#define MODEL_H

#include "Types.h"
#include "Drawable.h"
#include "Math.h"
#include "ModelPart.h"
#include "Camera.h"
#include "ShadowRenderer.h"

#include <vector>
#include <glad/glad.h>

struct Model : public IDrawable{
    private:
        Math3D::Transform modelTransform;
        std::vector<std::shared_ptr<ModelPart>> modelParts;
        bool enableBackfaceCulling = true;
    public:

        Model() = default;

        Model& addPart(std::shared_ptr<ModelPart> part){
            modelParts.push_back(part);
            return *this;
        }

        const std::vector<std::shared_ptr<ModelPart>>& getParts() const {
            return modelParts;
        }

        Math3D::Transform& transform(){
            return modelTransform;
        }

        void setBackfaceCulling(bool enabled){
            enableBackfaceCulling = enabled;
        }

        bool isBackfaceCullingEnabled() const {
            return enableBackfaceCulling;
        }

        void draw(
            const Math3D::Mat4& parent = Math3D::Mat4(),
            const Math3D::Mat4& view = Math3D::Mat4(),
            const Math3D::Mat4& projection = Math3D::Mat4()
        ) override {

            Math3D::Mat4 global = parent * transform().toMat4();

            // Enable or disable backface culling for this model
            if(enableBackfaceCulling){
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }else{
                glDisable(GL_CULL_FACE);
            }

            for(auto part : modelParts){
                if(part){
                    part->draw(global, view, projection);
                }
            }

            // Restore default culling state
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        };

        void drawShadows(const Math3D::Mat4& parent = Math3D::Mat4()){
            Math3D::Mat4 global = parent * transform().toMat4();
            for(auto part : modelParts){
                if(part && part->mesh){
                    Math3D::Mat4 worldMatrix = global * part->localTransform.toMat4();
                    ShadowRenderer::RenderShadows(part->mesh, worldMatrix, part->material);
                }
            }
        }

        void draw(PCamera cam, Nullable<Math3D::Transform> parentTransform = nullptr){
            if(!cam) return;

            auto parentMatrix = parentTransform.hasValue() 
                ? parentTransform->toMat4()
                : Math3D::Mat4(1.0f);

            this->draw(
                parentMatrix,
                cam->getViewMatrix(),
                cam->getProjectionMatrix()
            );
        }

        static std::shared_ptr<Model> Create(){
            return std::make_shared<Model>();
        }
};

typedef std::shared_ptr<Model> PModel;

#endif // MODEL_H
