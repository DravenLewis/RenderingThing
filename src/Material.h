#ifndef MATERIAL_H
#define MATERIAL_H

#include <string>
#include <unordered_map>
#include <memory>

#include "ShaderProgram.h"

struct IMaterialProperty{
    virtual void apply(ShaderProgram& shader, const std::string& name, bool assumeBound) = 0;
    virtual ~IMaterialProperty() = default;
};

template <typename T>
struct MaterialProperty : public IMaterialProperty{
    T value;
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

class Material{
    private:
        std::shared_ptr<ShaderProgram> programObjPtr;
        std::unordered_map<std::string, std::shared_ptr<IMaterialProperty>> properties;
    public:
        Material(std::shared_ptr<ShaderProgram> program) : programObjPtr(program) {
            if(program){
                if(program->getID() == 0){
                    if(program->compile() == 0){
                        LogBot.Log(LOG_ERRO, "Failed to Compile Shader / Shader Program: \n\n%s",program->getLog().c_str());
                    }
                }
            }
        };
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
            if(!programObjPtr) return;
            programObjPtr->bind();

            for(auto const& [name, prop] : properties){
                prop->apply(*programObjPtr, name, true);
            }
        };

        void unbind(){
            programObjPtr->unbind();
        }

        std::shared_ptr<ShaderProgram> getShader(){
            return this->programObjPtr;
        }

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
