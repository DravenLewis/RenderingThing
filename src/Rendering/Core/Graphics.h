/**
 * @file src/Rendering/Core/Graphics.h
 * @brief Declarations for Graphics.
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <vector>
#include <cstdint>
#include <memory>
#include <stdexcept>

#include "Foundation/Math/Color.h"
#include "Rendering/Textures/Texture.h"
#include "Rendering/Core/FrameBuffer.h"
#include "Rendering/Geometry/ModelPart.h"



namespace Graphics{

    namespace Image{

        /// @brief Holds data for Image.
        struct Image{
            public:
                std::vector<uint32_t> pixelData;
                int width, height;
                /**
                 * @brief Constructs a new Image instance.
                 */
                Image() = delete;
                /**
                 * @brief Constructs a new Image instance.
                 * @param width Dimension value.
                 * @param height Dimension value.
                 */
                Image(int width, int height){
                    if(width <= 0 || height <= 0) throw std::runtime_error("Cannot have 0 or Negative Dimension.");
                    this->width = width;
                    this->height = height;
                    pixelData.resize(static_cast<size_t>(width) * height);
                }
                /**
                 * @brief Constructs a new Image instance.
                 * @param src Value for src.
                 */
                Image(const Image& src) : width(src.width), height(src.height){
                    pixelData.resize(static_cast<size_t>(width) * height);
                    this->pixelData = src.pixelData;
                }

                /**
                 * @brief Creates a new object.
                 * @param width Dimension value.
                 * @param height Dimension value.
                 * @param src Value for src.
                 * @return Pointer to the resulting object.
                 */
                static std::shared_ptr<Image> Create(int width, int height, const std::vector<uint32_t>& src = {}){

                    std::vector<uint32_t> pixDat(width * height, 0x000000FF);

                    if(!src.empty() && (src.size() == pixDat.size())){
                        pixDat = src;
                    }

                    auto image = std::make_shared<Image>(width,height);
                    image->pixelData = pixDat;

                    return image;
                }

                /**
                 * @brief Returns the width.
                 * @return Computed numeric result.
                 */
                int getWidth() const { return width;}
                int getHeight() const {return height;}
                std::vector<uint32_t> getPixelData() const {return pixelData;}
        };

        /// @brief Holds data for BufferedImage.
        struct BufferedImage : public Image{
                /**
                 * @brief Constructs a new BufferedImage instance.
                 * @param width Dimension value.
                 * @param height Dimension value.
                 */
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

        /// @brief Represents the PostProcessingEffect type.
        class PostProcessingEffect{
            public:
                /**
                 * @brief Destroys this PostProcessingEffect instance.
                 */
                virtual ~PostProcessingEffect() = default;

                /**
                 * @brief Applies current settings.
                 * @param inputTex Value for put tex.
                 * @param depthTex Value for depth tex.
                 * @param frameBuffer Value for frame buffer.
                 * @param quad Value for quad.
                 * @return True when the operation succeeds; otherwise false.
                 */
                virtual bool apply(
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

            /**
             * @brief Returns the shader layout declaration.
             */
            layout (location = 0) in vec3 aPos;
            /**
             * @brief Returns the shader layout declaration.
             */
            layout (location = 1) in vec4 aColor;     // Mesh class has color at loc 1
            /**
             * @brief Returns the shader layout declaration.
             */
            layout (location = 2) in vec3 aNormal;    // Mesh class has normal at loc 2
            /**
             * @brief Returns the shader layout declaration.
             */
            layout (location = 3) in vec2 aTexCoords; // Mesh class has UV at loc 3

            out vec2 TexCoords;

            uniform mat4 u_model;
            uniform mat4 u_view;
            uniform mat4 u_projection;

            /**
             * @brief Executes the main shader pass.
             */
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
            uniform int u_applyDeband;
            uniform int u_frameIndex;
            uniform float u_debandStrength;

            /**
             * @brief Computes luminance.
             * @param c Value for c.
             * @return Computed numeric result.
             */
            float luma(vec3 c){
                return dot(c, vec3(0.299, 0.587, 0.114));
            }

            /**
             * @brief Generates interleaved gradient noise.
             * @param p Value for p.
             * @param frame Value for frame.
             * @return Computed numeric result.
             */
            float interleavedGradientNoise(vec2 p, float frame){
                vec2 jitter = vec2(frame * 0.75487765, frame * 0.56984026);
                return fract(52.9829189 * fract(dot(p + jitter, vec2(0.06711056, 0.00583715))));
            }

            /**
             * @brief Generates triangular-distribution noise.
             * @param p Value for p.
             * @param frame Value for frame.
             * @return Computed numeric result.
             */
            float triangularNoise(vec2 p, float frame){
                float a = interleavedGradientNoise(p + vec2(0.5, 0.25), frame);
                float b = interleavedGradientNoise(p + vec2(1.25, 2.75), frame + 17.0);
                return a - b;
            }

            /**
             * @brief Executes the main shader pass.
             */
            void main() {
                vec4 color = texture(screenTexture, TexCoords);

                if(u_applyDeband != 0){
                    vec2 texelSize = 1.0 / vec2(textureSize(screenTexture, 0));
                    vec3 rightColor = texture(screenTexture, clamp(TexCoords + vec2(texelSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb;
                    vec3 upColor = texture(screenTexture, clamp(TexCoords + vec2(0.0, texelSize.y), vec2(0.0), vec2(1.0))).rgb;

                    float centerLuma = luma(color.rgb);
                    float gradient = abs(centerLuma - luma(rightColor)) + abs(centerLuma - luma(upColor));
                    float smoothRegion = 1.0 - smoothstep(0.002, 0.030, gradient);

                    float frame = mod(float(u_frameIndex), 1024.0);
                    float noise = triangularNoise(gl_FragCoord.xy, frame);
                    float strength = (1.0 / 255.0) * max(u_debandStrength, 0.0) * mix(0.35, 1.65, smoothRegion);
                    color.rgb += vec3(noise * strength);
                }

                FragColor = vec4(max(color.rgb, vec3(0.0)), color.a);
            }
        )";

    }
}

#endif // GRAPHICS_H
