#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <vector>
#include <cstdint>
#include <memory>
#include <stdexcept>

#include "Color.h"
#include "Texture.h"
#include "FrameBuffer.h"
#include "ModelPart.h"



namespace Graphics{

    namespace Image{

        struct Image{
            public:
                std::vector<uint32_t> pixelData;
                int width, height;
                Image() = delete;
                Image(int width, int height){
                    if(width <= 0 || height <= 0) throw std::runtime_error("Cannot have 0 or Negative Dimension.");
                    this->width = width;
                    this->height = height;
                    pixelData.resize(static_cast<size_t>(width) * height);
                }
                Image(const Image& src) : width(src.width), height(src.height){
                    pixelData.resize(static_cast<size_t>(width) * height);
                    this->pixelData = src.pixelData;
                }

                static std::shared_ptr<Image> Create(int width, int height, const std::vector<uint32_t>& src = {}){

                    std::vector<uint32_t> pixDat(width * height, 0x000000FF);

                    if(!src.empty() && (src.size() == pixDat.size())){
                        pixDat = src;
                    }

                    auto image = std::make_shared<Image>(width,height);
                    image->pixelData = pixDat;

                    return image;
                }

                int getWidth() const { return width;}
                int getHeight() const {return height;}
                std::vector<uint32_t> getPixelData() const {return pixelData;}
        };

        struct BufferedImage : public Image{
                BufferedImage(int width, int height) : Image(width, height){}
                BufferedImage(const Image& src) : Image(src) {}
                BufferedImage() = delete;

                uint32_t getValueAt(int x, int y) {
                    if(x < 0 || y < 0 || x >= width || y >= height) return 0;
                    int index = x + y * width;
                    return pixelData[index];
                }

                void setValueAt(int x, int y, uint32_t value){
                    if(x < 0 || y < 0 || x >= width || y >= height || value < 0 || value > 0xFFFFFFFF /** MAX RGBA value */) return;
                    int index = x + y * width;
                    pixelData[index] = value;
                }

                Color getPixelAt(int x, int y){
                    return Color::fromRGBA32(getValueAt(x,y));
                }

                void setPixelAt(int x, int y, Color col){
                    setValueAt(x,y,col.toRGBA32());
                }
        };
    }

    namespace PostProcessing{

        class PostProcessingEffect{
            public:
                virtual ~PostProcessingEffect() = default;

                virtual void apply(
                    PTexture inputTex,
                    PTexture depthTex,
                    PFrameBuffer frameBuffer,
                    std::shared_ptr<ModelPart> quad
                ) = 0;
        };

        typedef std::shared_ptr<PostProcessingEffect> PPostProcessingEffect;


    }

    namespace ShaderDefaults{
        
        const std::string SCREEN_VERT_SRC = R"(
            #version 330 core

            layout (location = 0) in vec3 aPos;
            layout (location = 1) in vec4 aColor;     // Mesh class has color at loc 1
            layout (location = 2) in vec3 aNormal;    // Mesh class has normal at loc 2
            layout (location = 3) in vec2 aTexCoords; // Mesh class has UV at loc 3

            out vec2 TexCoords;

            uniform mat4 u_model;
            uniform mat4 u_view;
            uniform mat4 u_projection;

            void main() {
                TexCoords = aTexCoords;
                gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
            }
        )";

        const std::string SCREEN_FRAG_SRC = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D screenTexture;

            void main() {
                FragColor = texture(screenTexture, TexCoords);
                //FragColor = vec4(1.0,0.0,0.0,1.0);
            }
        )";

    }
}

#endif // GRAPHICS_H
