/**
 * @file src/Rendering/Shaders/ShaderProgram.h
 * @brief Declarations for ShaderProgram.
 */

#ifndef SHADER_PROGRAM_H
#define SHADER_PROGRAM_H

#include <string>
#include <vector>
#include <glad/glad.h>
#include <SDL3/SDL_opengl.h>
#include <memory>
#include <map>
#include <unordered_map>


#include "Foundation/Math/Math3D.h"
#include "Rendering/Textures/Texture.h"
#include "Rendering/Textures/CubeMap.h"


/// @brief Represents the Uniform type.
template <typename T>
class Uniform {
    private:
        T value;

    public:
        /**
         * @brief Constructs a new Uniform instance.
         */
        Uniform() = default;
        /**
         * @brief Constructs a new Uniform instance.
         * @param v Value for v.
         */
        Uniform(const T& v) : value(v) {}

        void set(const T& v) { value = v; }
        const T& get() const { return value; }
};

namespace GLUniformUpload {
    /// @brief Holds data for TextureSlot.
    struct TextureSlot {
        std::shared_ptr<Texture> texture;
        int slot = 0;

        /**
         * @brief Constructs a new TextureSlot instance.
         */
        TextureSlot() = default;
        /**
         * @brief Constructs a new TextureSlot instance.
         * @param tex Value for tex.
         * @param s Value for s.
         */
        TextureSlot(std::shared_ptr<Texture> tex, int s = 0) : texture(tex), slot(s) {}
    };

    /// @brief Holds data for CubeMapSlot.
    struct CubeMapSlot {
        std::shared_ptr<CubeMap> cubemap;
        int slot = 0;

        /**
         * @brief Constructs a new CubeMapSlot instance.
         */
        CubeMapSlot() = default;
        /**
         * @brief Constructs a new CubeMapSlot instance.
         * @param map Value for map.
         * @param s Value for s.
         */
        CubeMapSlot(std::shared_ptr<CubeMap> map, int s = 0) : cubemap(map), slot(s) {}
    };

    /**
     * @brief Uploads an integer uniform value.
     * @param loc Uniform location.
     * @param v Integer value.
     */
    inline void upload(GLint loc, int v) {
        glUniform1i(loc, v);
    }

    /**
     * @brief Uploads a float uniform value.
     * @param loc Uniform location.
     * @param v Float value.
     */
    inline void upload(GLint loc, float v) {
        glUniform1f(loc, v);
    }

    /**
     * @brief Uploads a `vec2` uniform value.
     * @param loc Uniform location.
     * @param v Vector value.
     */
    inline void upload(GLint loc, const Math3D::Vec2& v) {
        glUniform2f(loc, v.x, v.y);
    }

    /**
     * @brief Uploads a `vec3` uniform value.
     * @param loc Uniform location.
     * @param v Vector value.
     */
    inline void upload(GLint loc, const Math3D::Vec3& v) {
        glUniform3f(loc, v.x, v.y, v.z);
    }

    /**
     * @brief Uploads a `vec4` uniform value.
     * @param loc Uniform location.
     * @param v Vector value.
     */
    inline void upload(GLint loc, const Math3D::Vec4& v) {
        glUniform4f(loc, v.x, v.y, v.z, v.w);
    }

    /**
     * @brief Uploads a quaternion uniform value.
     * @param loc Uniform location.
     * @param q Quaternion value.
     */
    inline void upload(GLint loc, const Math3D::Quat& q) {
        glUniform4f(loc, q.x, q.y, q.z, q.w);
    }

    /**
     * @brief Uploads a 4x4 matrix uniform value.
     * @param loc Uniform location.
     * @param m Matrix value.
     */
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

        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(loc, slot);
        return;
    }

    /**
     * @brief Uploads a texture slot uniform.
     * @param loc Uniform location.
     * @param texSlot Texture and slot binding information.
     */
    inline void upload(GLint loc, const TextureSlot& texSlot){
        if(texSlot.texture){
            texSlot.texture->bind(texSlot.slot);
            glUniform1i(loc, texSlot.slot);
            return;
        }

        glActiveTexture(GL_TEXTURE0 + texSlot.slot);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(loc, texSlot.slot);
        return;
    }

    /**
     * @brief Uploads a cube-map slot uniform.
     * @param loc Uniform location.
     * @param mapSlot Cube-map and slot binding information.
     */
    inline void upload(GLint loc, const CubeMapSlot& mapSlot){
        if(mapSlot.cubemap){
            mapSlot.cubemap->bind(mapSlot.slot);
            glUniform1i(loc, mapSlot.slot);
            return;
        }

        glActiveTexture(GL_TEXTURE0 + mapSlot.slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glUniform1i(loc, mapSlot.slot);
        return;
    }
}


typedef unsigned int Shader, Program, Buffer, Array;

/// @brief Enumerates values for ShaderType.
enum ShaderType{
    FRAGMENT = 0,
    VERTEX,
    GEOMETRY,
    TESSELATION,
    COMPUTE,
    TASK,
    RAYTRACE
};

/// @brief Holds data for ShaderBundle.
struct ShaderBundle{

    Shader shaderHandle = 0;
    std::string shader_code;
    ShaderType type;
    bool valid = false;

    /**
     * @brief Creates a new object.
     * @param glsl Shader source code.
     * @return Initialized shader bundle.
     */
    static ShaderBundle Create(std::string& glsl){
        ShaderBundle bundle;
        bundle.shader_code = glsl;
        return bundle;
    }

    /**
     * @brief Destroys this ShaderBundle instance.
     */
    ~ShaderBundle(){
        if(shaderHandle != 0){
            glDeleteShader(shaderHandle);
            shaderHandle = 0;
        }
    }
};

/// @brief Represents the ShaderProgram type.
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

        /**
         * @brief Returns a cached uniform location, querying OpenGL if needed.
         * @param name Uniform name.
         * @return Uniform location, or `-1` when missing.
         */
        GLint getUniformLocationCached(const std::string& name);

        /**
         * @brief Builds a compile log string for a shader object.
         * @param shader Shader handle.
         * @return Shader compile log text.
         */
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

        /**
         * @brief Builds a link log string for a program object.
         * @param shader Program handle.
         * @return Program link log text.
         */
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

        /**
         * @brief Creates shader.
         * @param glsl GLSL source code.
         * @param type Shader stage type.
         * @return Shader handle, or `0` on failure.
         */
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

        /**
         * @brief Returns all stage bundles as a flat list.
         * @return Shader bundle list.
         */
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
        /**
         * @brief Sets the vertex shader.
         * @param glsl GLSL source code.
         */
        void setVertexShader(std::string glsl);
        /**
         * @brief Sets the fragment shader.
         * @param glsl GLSL source code.
         */
        void setFragmentShader(std::string glsl);
        /**
         * @brief Sets the geometry shader.
         * @param glsl GLSL source code.
         */
        void setGeometryShader(std::string glsl);
        /**
         * @brief Sets the tesselation shader.
         * @param glsl GLSL source code.
         */
        void setTesselationShader(std::string glsl);
        /**
         * @brief Sets the compute shader.
         * @param glsl GLSL source code.
         */
        void setComputeShader(std::string glsl);
        /**
         * @brief Sets the task shader.
         * @param glsl GLSL source code.
         */
        void setTaskShader(std::string glsl);
        /**
         * @brief Sets the rtx shader.
         * @param glsl GLSL source code.
         */
        void setRTXShader(std::string glsl);
       
        /**
         * @brief Returns the vertex shader.
         * @return Reference to the resulting value.
         */
        ShaderBundle& getVertexShader();
        /**
         * @brief Returns the fragment shader.
         * @return Reference to the resulting value.
         */
        ShaderBundle& getFragmentShader();
        /**
         * @brief Returns the geometry shader.
         * @return Reference to the resulting value.
         */
        ShaderBundle& getGeometryShader();
        /**
         * @brief Returns the tesselation shader.
         * @return Reference to the resulting value.
         */
        ShaderBundle& getTesselationShader();
        /**
         * @brief Returns the compute shader.
         * @return Reference to the resulting value.
         */
        ShaderBundle& getComputeShader();
        /**
         * @brief Returns the task shader.
         * @return Reference to the resulting value.
         */
        ShaderBundle& getTaskShader();
        /**
         * @brief Returns the rtx shader.
         * @return Reference to the resulting value.
         */
        ShaderBundle& getRTXShader();

        /**
         * @brief Returns the last compile/link log message.
         * @return Log message text.
         */
        std::string getLog();
        /**
         * @brief Compiles and links all configured shader stages.
         * @return Program handle, or `0` on failure.
         */
        Shader compile();
        /**
         * @brief Binds this resource.
         */
        void bind();
        /**
         * @brief Unbinds this resource.
         */
        void unbind();
        
        /**
         * @brief Sets the uniform.
         * @param name Uniform name.
         * @param uniform Uniform wrapper containing the value.
         */
        template<typename T>
        void setUniform(const std::string& name, const Uniform<T>& uniform){
            bind();
            GLint loc = getUniformLocationCached(name);
            if(loc != -1){
                GLUniformUpload::upload(loc, uniform.get());
            }
            //unbind();
        }

        /**
         * @brief Sets the uniform fast.
         * @param name Uniform name.
         * @param uniform Uniform wrapper containing the value.
         */
        template<typename T>
        void setUniformFast(const std::string& name, const Uniform<T>& uniform){
            GLint loc = getUniformLocationCached(name);
            if(loc != -1){
                GLUniformUpload::upload(loc, uniform.get());
            }
        }

        /**
         * @brief Returns the OpenGL program id.
         * @return Program handle.
         */
        Shader getID();

        /**
         * @brief Returns the currently bound OpenGL program id.
         * @return Active program handle.
         */
        static Shader GetCurrentShaderProgram();

        /**
         * @brief Destroys this ShaderProgram instance.
         */
        ~ShaderProgram(){
            glDeleteProgram(programHandle);
        }
};


/// @brief Holds data for ShaderCache.
struct ShaderCache{
    std::map<std::string, std::shared_ptr<ShaderProgram>> programCache;

    /**
     * @brief Returns a cached shader program, compiling and caching if missing.
     * @param name Cache key for the shader program.
     * @param vtx Vertex shader source.
     * @param frag Fragment shader source.
     * @param geom Geometry shader source.
     * @param tess Tessellation shader source.
     * @param compute Compute shader source.
     * @param task Task shader source.
     * @param srtx Ray-trace shader source.
     * @return Shared pointer to a compiled shader program.
     */
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
            if(it->second && it->second->getID() != 0){
                return it->second;
            }
            // Recover from failed cached programs (ID==0) by recompiling.
            programCache.erase(it);
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

    /**
     * @brief Clears the current state.
     */
    void clear(){
        programCache.clear();
    }
};

namespace ShaderCacheManager{
    inline static ShaderCache INSTANCE = ShaderCache();
}



#endif // SHADER_PROGRAM_H
