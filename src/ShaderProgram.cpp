
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

    auto bundles = _getShaderBundles();
    for(auto& bundle : bundles){
        if(bundle.valid){
            bundle.shaderHandle = this->_createShader(bundle.shader_code, bundle.type);
            this->shaderLog += this->_generateShaderLog(bundle.shaderHandle);
            this->shaderLog += "\n";

            if(bundle.shaderHandle == 0) bundle.valid = false;

            if(bundle.valid){
                glAttachShader(this->programHandle, bundle.shaderHandle);
            }
        }
    }

    glLinkProgram(this->programHandle);

    this->shaderLog += this->_generateProgramLog(this->programHandle);
    this->shaderLog += "\n";
    this->uniformLocationCache.clear();

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
        return it->second;
    }

    GLint loc = glGetUniformLocation(getID(), name.c_str());
    uniformLocationCache.emplace(name, loc);
    return loc;
}

void ShaderProgram::unbind(){
    glUseProgram(0);
}

