#ifndef SHADER_PROGRAM_H
#define SHADER_PROGRAM_H

#include <string>
#include <vector>
#include <glad/glad.h>
#include <SDL3/SDL_opengl.h>
#include <memory>
#include <map>
#include <unordered_map>


#include "Math.h"
#include "Texture.h"
#include "CubeMap.h"


template <typename T>
class Uniform {
    private:
        T value;

    public:
        Uniform() = default;
        Uniform(const T& v) : value(v) {}

        void set(const T& v) { value = v; }
        const T& get() const { return value; }
};

namespace GLUniformUpload {
    struct TextureSlot {
        std::shared_ptr<Texture> texture;
        int slot = 0;

        TextureSlot() = default;
        TextureSlot(std::shared_ptr<Texture> tex, int s = 0) : texture(tex), slot(s) {}
    };

    struct CubeMapSlot {
        std::shared_ptr<CubeMap> cubemap;
        int slot = 0;

        CubeMapSlot() = default;
        CubeMapSlot(std::shared_ptr<CubeMap> map, int s = 0) : cubemap(map), slot(s) {}
    };

    inline void upload(GLint loc, int v) {
        glUniform1i(loc, v);
    }

    inline void upload(GLint loc, float v) {
        glUniform1f(loc, v);
    }

    inline void upload(GLint loc, const Math3D::Vec2& v) {
        glUniform2f(loc, v.x, v.y);
    }

    inline void upload(GLint loc, const Math3D::Vec3& v) {
        glUniform3f(loc, v.x, v.y, v.z);
    }

    inline void upload(GLint loc, const Math3D::Vec4& v) {
        glUniform4f(loc, v.x, v.y, v.z, v.w);
    }

    inline void upload(GLint loc, const Math3D::Quat& q) {
        glUniform4f(loc, q.x, q.y, q.z, q.w);
    }

    inline void upload(GLint loc, const Math3D::Mat4& m) {
        glUniformMatrix4fv(
            loc,
            1,
            GL_FALSE,
            glm::value_ptr(m.data)
        );
    }

    // TODO WE NEED A TEXTURE UNIFORM AS WELL.
    inline void upload(GLint loc,std::shared_ptr<Texture> tex, int slot = 0){
        if(tex){
            tex->bind(slot);
            glUniform1i(loc, slot);
            return;
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(loc, 0);
        return;
    }

    inline void upload(GLint loc, const TextureSlot& texSlot){
        if(texSlot.texture){
            texSlot.texture->bind(texSlot.slot);
            glUniform1i(loc, texSlot.slot);
            return;
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(loc, texSlot.slot);
        return;
    }

    inline void upload(GLint loc, const CubeMapSlot& mapSlot){
        if(mapSlot.cubemap){
            mapSlot.cubemap->bind(mapSlot.slot);
            glUniform1i(loc, mapSlot.slot);
            return;
        }

        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glUniform1i(loc, mapSlot.slot);
        return;
    }
}


typedef unsigned int Shader, Program, Buffer, Array;

enum ShaderType{
    FRAGMENT,
    VERTEX,
    GEOMETRY,
    TESSELATION,
    COMPUTE,
    TASK,
    RAYTRACE
};

struct ShaderBundle{

    Shader shaderHandle;
    std::string shader_code;
    ShaderType type;
    bool valid = false;

    static ShaderBundle Create(std::string& glsl){
        ShaderBundle bundle;
        bundle.shader_code = glsl;
        return bundle;
    }

    ~ShaderBundle(){
        glDeleteShader(shaderHandle);
    }
};

class ShaderProgram{
    private:
        ShaderBundle sh_vert;
        ShaderBundle sh_frag;
        ShaderBundle sh_geom;
        ShaderBundle sh_tess;
        ShaderBundle sh_comp;
        ShaderBundle sh_task;
        ShaderBundle sh_srtx;
    
        Shader programHandle = 0;
        std::string shaderLog;
        std::unordered_map<std::string, GLint> uniformLocationCache;

        GLint getUniformLocationCached(const std::string& name);

        std::string _generateShaderLog(Shader shader){
            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if(!success){
                char infoLog[1024] = {0};
                glGetShaderInfoLog(shader, 512, 0, infoLog);
                return std::string(infoLog);
            }
            return std::string("Successful Compilation.");
        }

        std::string _generateProgramLog(Program shader){
            GLint success;
            glGetProgramiv(shader, GL_LINK_STATUS, &success);
            if (!success) {
                char infoLog[1024] = {0};
                glGetProgramInfoLog(shader, 1024, NULL, infoLog);
                return "Linker Error: " + std::string(infoLog);
            }
            return "Program Linked Successfully.";
        }

        Shader _createShader(std::string glsl, ShaderType type){
            Shader value = 0;

            const char * str = glsl.c_str();

            switch (type){
                case ShaderType::VERTEX:
                    value = glCreateShader(GL_VERTEX_SHADER);
                    break;
                case ShaderType::FRAGMENT:
                    value = glCreateShader(GL_FRAGMENT_SHADER);
                    break;
                case ShaderType::GEOMETRY:
                    value = glCreateShader(GL_GEOMETRY_SHADER);
                    break;
                case ShaderType::TESSELATION:
                    value = glCreateShader(GL_TESS_CONTROL_SHADER);
                    break;
                case ShaderType::COMPUTE:
                    value = glCreateShader(GL_COMPUTE_SHADER);
                    break;
                case ShaderType::TASK:
                    value = glCreateShader(GL_TESS_EVALUATION_SHADER);
                    break;
                case ShaderType::RAYTRACE:
                default:
                    value = 0;
                    break;
            }

            if(value != 0){
                glShaderSource(value,1,&str, NULL);
                glCompileShader(value);
            }

            return value;
        }

        std::vector<ShaderBundle> _getShaderBundles(){
            std::vector<ShaderBundle> BUNDLES = {
                sh_vert,
                sh_frag,
                sh_geom,
                sh_tess,
                sh_comp,
                sh_task,
                sh_srtx
            };

            return BUNDLES;
        }
    public:
        void setVertexShader(std::string glsl);
        void setFragmentShader(std::string glsl);
        void setGeometryShader(std::string glsl);
        void setTesselationShader(std::string glsl);
        void setComputeShader(std::string glsl);
        void setTaskShader(std::string glsl);
        void setRTXShader(std::string glsl);
       
        ShaderBundle& getVertexShader();
        ShaderBundle& getFragmentShader();
        ShaderBundle& getGeometryShader();
        ShaderBundle& getTesselationShader();
        ShaderBundle& getComputeShader();
        ShaderBundle& getTaskShader();
        ShaderBundle& getRTXShader();

        std::string getLog();
        Shader compile();
        void bind();
        void unbind();
        
        template<typename T>
        void setUniform(const std::string& name, const Uniform<T>& uniform){
            bind();
            GLint loc = getUniformLocationCached(name);
            if(loc != -1){
                GLUniformUpload::upload(loc, uniform.get());
            }
            //unbind();
        }

        template<typename T>
        void setUniformFast(const std::string& name, const Uniform<T>& uniform){
            GLint loc = getUniformLocationCached(name);
            if(loc != -1){
                GLUniformUpload::upload(loc, uniform.get());
            }
        }

        Shader getID();

        static Shader GetCurrentShaderProgram();

        ~ShaderProgram(){
            glDeleteProgram(programHandle);
        }
};


struct ShaderCache{
    std::map<std::string, std::shared_ptr<ShaderProgram>> programCache;

    std::shared_ptr<ShaderProgram> getOrCompile(
        std::string name, 
        std::string vtx = "", 
        std::string frag = "",
        std::string geom = "",
        std::string tess = "",
        std::string compute = "",
        std::string task = "",
        std::string srtx = ""
    ){
        // Check if the file is in the cache;
        auto it = programCache.find(name);
        if(it != programCache.end()){
            return it->second;
        }

        auto program = std::make_shared<ShaderProgram>();
        if(vtx.size() != 0) program->setVertexShader(vtx);
        if(frag.size() != 0) program->setFragmentShader(frag);
        if(geom.size() != 0) program->setGeometryShader(geom);
        if(tess.size() != 0) program->setTesselationShader(tess);
        if(compute.size() != 0) program->setComputeShader(compute);
        if(task.size() != 0) program->setTaskShader(task);
        if(srtx.size() != 0) program->setRTXShader(srtx);


        program->compile();

        programCache[name] = program;

        return program;
    }

    void clear(){
        programCache.clear();
    }
};

namespace ShaderCacheManager{
    inline static ShaderCache INSTANCE = ShaderCache();
}


#endif // SHADER_PROGRAM_H
