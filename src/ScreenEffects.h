#ifndef SCREENEFFECTS_H
#define SCREENEFFECTS_H

#include "Graphics.h"

class GrayscaleEffect : public Graphics::PostProcessing::PostProcessingEffect{
    private:
        std::shared_ptr<ShaderProgram> shader;

        const std::string GRAYSCALE_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;
            uniform sampler2D screenTexture;
            
            void main() {
                vec4 col = texture(screenTexture, TexCoords);
                float avg = 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
                FragColor = vec4(avg, avg, avg, col.a);
            }
        )";

    public:

        GrayscaleEffect(){
            shader = std::make_shared<ShaderProgram>();
            shader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            shader->setFragmentShader(GRAYSCALE_SHADER);
            shader->compile();
        }

        void apply(PTexture tex, PTexture depthTex, PFrameBuffer outFbo, std::shared_ptr<ModelPart> quad) override {

            outFbo->bind();
            outFbo->clear();
            glDisable(GL_DEPTH_TEST);

            shader->bind();
            Uniform<PTexture> u_tex(tex);
            shader->setUniform("screenTexture", u_tex);

            quad->draw(Math3D::Mat4(),Math3D::Mat4(),Math3D::Mat4());

            outFbo->unbind();
        }

        static Graphics::PostProcessing::PPostProcessingEffect New(){
            return std::make_shared<GrayscaleEffect>();
        }
};

#endif //SCREENEFFECTS_H