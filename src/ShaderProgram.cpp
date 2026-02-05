
#include "ShaderProgram.h"

#include <iostream>

#include "Logbot.h"

void ShaderProgram::setVertexShader(std::string glsl){
    ShaderBundle bundle = ShaderBundle::Create(glsl);
    bundle.type = ShaderType::VERTEX;
    bundle.valid = true;
    this->sh_vert = bundle;
}
void ShaderProgram::setFragmentShader(std::string glsl){
    ShaderBundle bundle = ShaderBundle::Create(glsl);
    bundle.type = ShaderType::FRAGMENT;
    bundle.valid = true;
    this->sh_frag = bundle;
}
void ShaderProgram::setGeometryShader(std::string glsl){
    ShaderBundle bundle = ShaderBundle::Create(glsl);
    bundle.type = ShaderType::GEOMETRY;
    bundle.valid = true;
    this->sh_geom = bundle;
}
void ShaderProgram::setTesselationShader(std::string glsl){
    ShaderBundle bundle = ShaderBundle::Create(glsl);
    bundle.type = ShaderType::TESSELATION;
    bundle.valid = true;
    this->sh_tess = bundle;
}
void ShaderProgram::setComputeShader(std::string glsl){
    ShaderBundle bundle = ShaderBundle::Create(glsl);
    bundle.type = ShaderType::COMPUTE;
    bundle.valid = true;
    this->sh_comp = bundle;
}
void ShaderProgram::setTaskShader(std::string glsl){
    ShaderBundle bundle = ShaderBundle::Create(glsl);
    bundle.type = ShaderType::TASK;
    bundle.valid = true;
    this->sh_vert = bundle;
}
void ShaderProgram::setRTXShader(std::string glsl){
    ShaderBundle bundle = ShaderBundle::Create(glsl);
    bundle.type = ShaderType::RAYTRACE;
    bundle.valid = false; // RTX Not Supported.
    this->sh_vert = bundle;
}

ShaderBundle& ShaderProgram::getVertexShader(){
    return this->sh_vert;
}
ShaderBundle& ShaderProgram::getFragmentShader(){
    return this->sh_frag;
}
ShaderBundle& ShaderProgram::getGeometryShader(){
    return this->sh_geom;
}
ShaderBundle& ShaderProgram::getTesselationShader(){
    return this->sh_tess;
}
ShaderBundle& ShaderProgram::getComputeShader(){
    return this->sh_comp;
}
ShaderBundle& ShaderProgram::getTaskShader(){
    return this->sh_task;
}
ShaderBundle& ShaderProgram::getRTXShader(){
    return this->sh_srtx;
}

std::string ShaderProgram::getLog(){
    return this->shaderLog;
}

Shader ShaderProgram::compile(){

    this->programHandle = glCreateProgram();

    bool anyShaderFailed = false;

    auto bundles = _getShaderBundles();
    for(auto& bundle : bundles){
        if(bundle.valid){
            bundle.shaderHandle = this->_createShader(bundle.shader_code, bundle.type);
            this->shaderLog += this->_generateShaderLog(bundle.shaderHandle);
            this->shaderLog += "\n";

            if(bundle.shaderHandle == 0){
                bundle.valid = false;
                anyShaderFailed = true;
            }else{
                GLint success = 0;
                glGetShaderiv(bundle.shaderHandle, GL_COMPILE_STATUS, &success);
                if(!success){
                    bundle.valid = false;
                    anyShaderFailed = true;
                }
            }

            if(bundle.valid){
                glAttachShader(this->programHandle, bundle.shaderHandle);
            }
        }
    }

    glLinkProgram(this->programHandle);

    this->shaderLog += this->_generateProgramLog(this->programHandle);
    this->shaderLog += "\n";
    this->uniformLocationCache.clear();

    GLint linkSuccess = 0;
    glGetProgramiv(this->programHandle, GL_LINK_STATUS, &linkSuccess);
    if(!linkSuccess || anyShaderFailed){
        glDeleteProgram(this->programHandle);
        this->programHandle = 0;
    }

    return this->programHandle;
}

Shader ShaderProgram::GetCurrentShaderProgram(){
    GLint id;
    glGetIntegerv(GL_CURRENT_PROGRAM, &id);
    return id;
};

void ShaderProgram::bind(){
    if(this->programHandle == 0 || ShaderProgram::GetCurrentShaderProgram() == this->programHandle) return;
    glUseProgram(this->programHandle);
}
        
Shader ShaderProgram::getID(){
    return this->programHandle;
}

GLint ShaderProgram::getUniformLocationCached(const std::string& name){
    auto it = uniformLocationCache.find(name);
    if(it != uniformLocationCache.end()){
        if(it->second != -1){
            return it->second;
        }
        // Retry in case the location was cached before the program was fully linked.
        GLint retry = glGetUniformLocation(getID(), name.c_str());
        it->second = retry;
        return retry;
    }

    GLint loc = glGetUniformLocation(getID(), name.c_str());
    uniformLocationCache.emplace(name, loc);
    return loc;
}

void ShaderProgram::unbind(){
    glUseProgram(0);
}

