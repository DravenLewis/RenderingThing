/**
 * @file src/Rendering/Geometry/Model.h
 * @brief Declarations for Model.
 */

#ifndef MODEL_H
#define MODEL_H

#include "Foundation/Util/Types.h"
#include "Rendering/Geometry/Drawable.h"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Geometry/ModelPart.h"
#include "Scene/Camera.h"
#include "Rendering/Lighting/ShadowRenderer.h"

#include <string>
#include <vector>
#include <glad/glad.h>

/// @brief Holds data for Model.
struct Model : public IDrawable{
    private:
        Math3D::Transform modelTransform;
        std::vector<std::shared_ptr<ModelPart>> modelParts;
        bool enableBackfaceCulling = true;
        std::string sourceAssetRef;
        bool sourceForceSmoothNormals = false;
    public:

        /**
         * @brief Constructs a new Model instance.
         */
        Model() = default;

        /**
         * @brief Adds part.
         * @param part Value for part.
         * @return Reference to the resulting value.
         */
        Model& addPart(std::shared_ptr<ModelPart> part){
            modelParts.push_back(part);
            return *this;
        }

        /**
         * @brief Returns the parts.
         * @return Reference to the resulting value.
         */
        const std::vector<std::shared_ptr<ModelPart>>& getParts() const {
            return modelParts;
        }

        /**
         * @brief Returns the transform value.
         * @return Reference to the resulting value.
         */
        Math3D::Transform& transform(){
            return modelTransform;
        }

        /**
         * @brief Sets the backface culling.
         * @param enabled Flag controlling enabled.
         */
        void setBackfaceCulling(bool enabled){
            enableBackfaceCulling = enabled;
        }

        /**
         * @brief Checks whether backface culling enabled.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool isBackfaceCullingEnabled() const {
            return enableBackfaceCulling;
        }

        /**
         * @brief Sets the source asset ref.
         * @param sourceRef Reference to source.
         */
        void setSourceAssetRef(const std::string& sourceRef){
            sourceAssetRef = sourceRef;
        }

        /**
         * @brief Returns the source asset ref.
         * @return Resulting string value.
         */
        const std::string& getSourceAssetRef() const{
            return sourceAssetRef;
        }

        /**
         * @brief Sets the source force smooth normals.
         * @param enabled Flag controlling enabled.
         */
        void setSourceForceSmoothNormals(bool enabled){
            sourceForceSmoothNormals = enabled;
        }

        /**
         * @brief Returns the source force smooth normals.
         * @return True when the operation succeeds; otherwise false.
         */
        bool getSourceForceSmoothNormals() const{
            return sourceForceSmoothNormals;
        }

        /**
         * @brief Draws this object.
         * @param parent Value for parent.
         * @param view Value for view.
         * @param projection Value for projection.
         */
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
                if(part && part->visible){
                    part->draw(global, view, projection);
                }
            }

            // Restore default culling state
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        };

        /**
         * @brief Draws shadows.
         * @param parent Value for parent.
         */
        void drawShadows(const Math3D::Mat4& parent = Math3D::Mat4()){
            Math3D::Mat4 global = parent * transform().toMat4();
            for(auto part : modelParts){
                if(part && part->visible && part->mesh){
                    Math3D::Mat4 worldMatrix = global * part->localTransform.toMat4();
                    ShadowRenderer::RenderShadows(part->mesh, worldMatrix, part->material);
                }
            }
        }

        /**
         * @brief Draws this object.
         * @param cam Value for cam.
         * @param parentTransform Spatial value used by this operation.
         */
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

        /**
         * @brief Creates a new object.
         * @return Pointer to the resulting object.
         */
        static std::shared_ptr<Model> Create(){
            return std::make_shared<Model>();
        }
};

typedef std::shared_ptr<Model> PModel;

#endif // MODEL_H
