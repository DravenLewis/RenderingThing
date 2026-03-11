/**
 * @file src/Rendering/Materials/Material.h
 * @brief Declarations for Material.
 */

#ifndef MATERIAL_H
#define MATERIAL_H

#include <string>
#include <unordered_map>
#include <memory>

#include "Rendering/Shaders/ShaderProgram.h"

/// @brief Holds data for IMaterialProperty.
struct IMaterialProperty{
    /**
     * @brief Applies current settings.
     * @param shader Value for shader.
     * @param name Name used for name.
     * @param assumeBound Value for assume bound.
     */
    virtual void apply(ShaderProgram& shader, const std::string& name, bool assumeBound) = 0;
    /**
     * @brief Destroys this IMaterialProperty instance.
     */
    virtual ~IMaterialProperty() = default;
};

/// @brief Holds data for MaterialProperty.
template <typename T>
struct MaterialProperty : public IMaterialProperty{
    T value;
    /**
     * @brief Constructs a new MaterialProperty instance.
     * @param val Value for val.
     */
    MaterialProperty(T val) : value(val) {};

    void apply(ShaderProgram& shader, const std::string& name, bool assumeBound) override{
        Uniform<T> uni;
        uni.set(this->value);
        if(assumeBound){
            shader.setUniformFast(name,uni);
        }else{
            shader.setUniform(name,uni);
        }
    }
};

/// @brief Represents the Material type.
class Material{
    private:
        std::shared_ptr<ShaderProgram> programObjPtr;
        std::unordered_map<std::string, std::shared_ptr<IMaterialProperty>> properties;
        bool castsShadowsFlag = true;
        bool receivesShadowsFlag = true;
    public:
        /**
         * @brief Constructs a new Material instance.
         * @param program Value for program.
         */
        Material(std::shared_ptr<ShaderProgram> program) : programObjPtr(program) {
            if(program){
                if(program->getID() == 0){
                    if(program->compile() == 0){
                        LogBot.Log(LOG_ERRO, "Failed to Compile Shader / Shader Program: \n\n%s",program->getLog().c_str());
                    }
                }
            }

            set<int>("u_receiveShadows", receivesShadowsFlag ? 1 : 0);
        };
        /**
         * @brief Destroys this Material instance.
         */
        virtual ~Material(){};

        template<typename T>
        bool set(const std::string& name, T value){
            auto it = properties.find(name);
            if(it != properties.end()){
                auto existing = std::dynamic_pointer_cast<MaterialProperty<T>>(it->second);
                if(existing){
                    existing->value = value;
                    return true;
                }
            }

            properties[name] = std::make_shared<MaterialProperty<T>>(value);
            return true;
        }

        virtual void bind(){
            if(!programObjPtr || programObjPtr->getID() == 0){
                return;
            }
            programObjPtr->bind();

            for(auto const& [name, prop] : properties){
                prop->apply(*programObjPtr, name, true);
            }
        };

        void unbind(){
            if(programObjPtr){
                programObjPtr->unbind();
            }
        }

        std::shared_ptr<ShaderProgram> getShader(){
            return this->programObjPtr;
        }

        void setShader(std::shared_ptr<ShaderProgram> program){
            programObjPtr = program;
            if(programObjPtr && programObjPtr->getID() == 0){
                if(programObjPtr->compile() == 0){
                    LogBot.Log(LOG_ERRO, "Failed to Compile Shader / Shader Program: \n\n%s", programObjPtr->getLog().c_str());
                }
            }
        }

        void setCastsShadows(bool value){ castsShadowsFlag = value; }
        bool castsShadows() const { return castsShadowsFlag; }

        void setReceivesShadows(bool value){
            receivesShadowsFlag = value;
            set<int>("u_receiveShadows", receivesShadowsFlag ? 1 : 0);
        }
        bool receivesShadows() const { return receivesShadowsFlag; }

        static std::shared_ptr<Material> Copy(std::shared_ptr<Material> material){
            if(!material) return nullptr;
            auto n_material = std::make_shared<Material>(material->getShader());
            n_material->properties = material->properties;
            return n_material;
        }

        template<typename T>
        static std::shared_ptr<T> GetAs(std::shared_ptr<Material> matPtr){
            return std::dynamic_pointer_cast<T>(matPtr);
        }
};

typedef std::shared_ptr<Material> PMaterial;



#endif // MATERIAL_H
