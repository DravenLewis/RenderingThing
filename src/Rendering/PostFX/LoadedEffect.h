/**
 * @file src/Rendering/PostFX/LoadedEffect.h
 * @brief Header-only generic post effect loaded from an effect asset descriptor.
 */

#ifndef LOADED_EFFECT_H
#define LOADED_EFFECT_H

#include <algorithm>
#include <memory>
#include <string>

#include "Assets/Core/Asset.h"
#include "Assets/Core/AssetDescriptorUtils.h"
#include "Assets/Descriptors/EffectAsset.h"
#include "Foundation/Logging/Logbot.h"
#include "Rendering/Core/Graphics.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Rendering/Textures/Texture.h"


/// @brief Represents the LoadedEffect type.
class LoadedEffect : public Graphics::PostProcessing::PostProcessingEffect {
    private:
        std::string sourceEffectRef;
        EffectAssetData data;
        std::shared_ptr<ShaderProgram> shader;
        bool compileAttempted = false;

        void invalidateShader(){
            shader.reset();
            compileAttempted = false;
        }

        bool ensureCompiled(){
            if(shader && shader->getID() != 0){
                return true;
            }
            if(compileAttempted){
                return false;
            }
            compileAttempted = true;

            if(!data.isComplete()){
                LogBot.Log(LOG_ERRO,
                           "Loaded effect '%s' is missing vertex or fragment shader refs.",
                           sourceEffectRef.c_str());
                return false;
            }

            std::string vertexCode;
            std::string fragmentCode;
            std::string error;
            if(!AssetDescriptorUtils::ReadTextRefOrPath(data.vertexAssetRef, vertexCode, &error)){
                LogBot.Log(LOG_ERRO,
                           "Failed to read loaded effect vertex shader '%s' (%s): %s",
                           data.vertexAssetRef.c_str(),
                           sourceEffectRef.c_str(),
                           error.c_str());
                return false;
            }
            if(!AssetDescriptorUtils::ReadTextRefOrPath(data.fragmentAssetRef, fragmentCode, &error)){
                LogBot.Log(LOG_ERRO,
                           "Failed to read loaded effect fragment shader '%s' (%s): %s",
                           data.fragmentAssetRef.c_str(),
                           sourceEffectRef.c_str(),
                           error.c_str());
                return false;
            }

            shader = std::make_shared<ShaderProgram>();
            if(!shader){
                return false;
            }
            shader->setVertexShader(vertexCode);
            shader->setFragmentShader(fragmentCode);
            if(shader->compile() == 0){
                LogBot.Log(LOG_ERRO,
                           "Failed to compile loaded effect shader '%s':\n%s",
                           sourceEffectRef.c_str(),
                           shader->getLog().c_str());
                return false;
            }
            return true;
        }

        void applyInputBinding(PTexture inputTex,
                               PTexture depthTex,
                               PFrameBuffer outFbo,
                               const EffectInputBindingData& input){
            if(!shader || input.uniformName.empty()){
                return;
            }

            switch(input.source){
                case EffectInputSource::ScreenColor:
                    shader->setUniformFast(
                        input.uniformName,
                        Uniform<GLUniformUpload::TextureSlot>(
                            GLUniformUpload::TextureSlot(inputTex, std::max(input.textureSlot, 0))
                        )
                    );
                    break;
                case EffectInputSource::Depth:
                    shader->setUniformFast(
                        input.uniformName,
                        Uniform<GLUniformUpload::TextureSlot>(
                            GLUniformUpload::TextureSlot(depthTex, std::max(input.textureSlot, 0))
                        )
                    );
                    break;
                case EffectInputSource::InputTexelSize:{
                    Math3D::Vec2 texelSize(0.0f, 0.0f);
                    if(inputTex && inputTex->getWidth() > 0 && inputTex->getHeight() > 0){
                        texelSize = Math3D::Vec2(
                            1.0f / static_cast<float>(inputTex->getWidth()),
                            1.0f / static_cast<float>(inputTex->getHeight())
                        );
                    }
                    shader->setUniformFast(input.uniformName, Uniform<Math3D::Vec2>(texelSize));
                    break;
                }
                case EffectInputSource::OutputTexelSize:{
                    Math3D::Vec2 texelSize(0.0f, 0.0f);
                    if(outFbo && outFbo->getWidth() > 0 && outFbo->getHeight() > 0){
                        texelSize = Math3D::Vec2(
                            1.0f / static_cast<float>(outFbo->getWidth()),
                            1.0f / static_cast<float>(outFbo->getHeight())
                        );
                    }
                    shader->setUniformFast(input.uniformName, Uniform<Math3D::Vec2>(texelSize));
                    break;
                }
                default:
                    break;
            }
        }

        void applyProperty(EffectPropertyData& property){
            if(!shader){
                return;
            }

            auto setIntMirror = [&](int value){
                if(!property.uniformName.empty()){
                    shader->setUniformFast(property.uniformName, Uniform<int>(value));
                }
                if(!property.mirrorUniformName.empty()){
                    shader->setUniformFast(property.mirrorUniformName, Uniform<int>(value));
                }
            };

            switch(property.type){
                case EffectPropertyType::Float:
                    if(!property.uniformName.empty()){
                        shader->setUniformFast(property.uniformName, Uniform<float>(property.floatValue));
                    }
                    if(!property.mirrorUniformName.empty()){
                        shader->setUniformFast(property.mirrorUniformName, Uniform<float>(property.floatValue));
                    }
                    break;
                case EffectPropertyType::Int:
                    setIntMirror(property.intValue);
                    break;
                case EffectPropertyType::Bool:
                    setIntMirror(property.boolValue ? 1 : 0);
                    break;
                case EffectPropertyType::Vec2:
                    if(!property.uniformName.empty()){
                        shader->setUniformFast(property.uniformName, Uniform<Math3D::Vec2>(property.vec2Value));
                    }
                    if(!property.mirrorUniformName.empty()){
                        shader->setUniformFast(property.mirrorUniformName, Uniform<Math3D::Vec2>(property.vec2Value));
                    }
                    break;
                case EffectPropertyType::Vec3:
                    if(!property.uniformName.empty()){
                        shader->setUniformFast(property.uniformName, Uniform<Math3D::Vec3>(property.vec3Value));
                    }
                    if(!property.mirrorUniformName.empty()){
                        shader->setUniformFast(property.mirrorUniformName, Uniform<Math3D::Vec3>(property.vec3Value));
                    }
                    break;
                case EffectPropertyType::Vec4:
                    if(!property.uniformName.empty()){
                        shader->setUniformFast(property.uniformName, Uniform<Math3D::Vec4>(property.vec4Value));
                    }
                    if(!property.mirrorUniformName.empty()){
                        shader->setUniformFast(property.mirrorUniformName, Uniform<Math3D::Vec4>(property.vec4Value));
                    }
                    break;
                case EffectPropertyType::Texture2D:{
                    const std::uint64_t revision = property.textureAssetRef.empty()
                        ? 0
                        : AssetManager::Instance.getRevision(property.textureAssetRef);
                    if(property.loadedTextureRef != property.textureAssetRef ||
                       property.loadedTextureRevision != revision){
                        property.texturePtr.reset();
                        if(!property.textureAssetRef.empty()){
                            auto textureAsset = AssetManager::Instance.getOrLoad(property.textureAssetRef);
                            if(textureAsset){
                                property.texturePtr = Texture::Load(textureAsset);
                            }
                        }
                        property.loadedTextureRef = property.textureAssetRef;
                        property.loadedTextureRevision = revision;
                    }

                    if(!property.uniformName.empty()){
                        shader->setUniformFast(
                            property.uniformName,
                            Uniform<GLUniformUpload::TextureSlot>(
                                GLUniformUpload::TextureSlot(property.texturePtr, std::max(property.textureSlot, 0))
                            )
                        );
                    }
                    if(!property.presenceUniformName.empty()){
                        shader->setUniformFast(
                            property.presenceUniformName,
                            Uniform<int>(property.texturePtr ? 1 : 0)
                        );
                    }
                    break;
                }
                default:
                    break;
            }
        }

    public:
        void setSourceEffectRef(const std::string& effectRef){
            sourceEffectRef = effectRef;
        }

        void setEffectData(const EffectAssetData& effectData){
            const bool shaderChanged =
                (data.vertexAssetRef != effectData.vertexAssetRef) ||
                (data.fragmentAssetRef != effectData.fragmentAssetRef);
            data = effectData;
            if(shaderChanged){
                invalidateShader();
            }
        }

        EffectAssetData& effectData(){
            return data;
        }

        const EffectAssetData& effectData() const{
            return data;
        }

        bool apply(PTexture inputTex,
                   PTexture depthTex,
                   PFrameBuffer frameBuffer,
                   std::shared_ptr<ModelPart> quad) override{
            if(!inputTex || !frameBuffer || !quad){
                return false;
            }
            if(!ensureCompiled()){
                return false;
            }

            frameBuffer->bind();
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);

            shader->bind();
            for(const auto& input : data.inputs){
                applyInputBinding(inputTex, depthTex, frameBuffer, input);
            }
            for(auto& property : data.properties){
                applyProperty(property);
            }

            static const Math3D::Mat4 IDENTITY;
            shader->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
            shader->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));
            quad->draw(IDENTITY, IDENTITY, IDENTITY);
            frameBuffer->unbind();
            return true;
        }
};

#endif // LOADED_EFFECT_H
