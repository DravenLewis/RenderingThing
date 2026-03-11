/**
 * @file src/Rendering/Core/Screen.h
 * @brief Declarations for Screen.
 */

#ifndef SCREEN_H
#define SCREEN_H

#include <memory>
#include <vector>
#include <atomic>
#include <cstdint>

#include "Rendering/Core/FrameBuffer.h"
#include "Rendering/Textures/Texture.h"
#include "Scene/Camera.h"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Rendering/Geometry/ModelPartPrefabs.h"
#include "Platform/Window/RenderWindow.h"
#include "Rendering/Core/Graphics.h"
#include "Foundation/Math/Color.h"
#include "Rendering/Lighting/Environment.h"

class View;
typedef std::shared_ptr<View> PView;

/// @brief Represents the Screen type.
class Screen{
    private:
        int width, height;

        std::unique_ptr<TrippleBuffer> buffer;
        std::shared_ptr<ModelPart> screenQuad;
        std::shared_ptr<ShaderProgram> screenShader;
        std::vector<Graphics::PostProcessing::PPostProcessingEffect> effects;

        PCamera camera;
        PEnvironment environment;
        PCamera uiCamera;
        int uiWidth = 0;
        int uiHeight = 0;
        bool bound = false;
        std::atomic<float> lastPostProcessMs{0.0f};
        std::atomic<int> lastPostProcessEffectCount{0};
        std::uint32_t presentFrameIndex = 0;
        float presentDebandStrength = 1.25f;

        /**
         * @brief Initializes screen geom.
         */
        void initScreenGeom();
        /**
         * @brief Initializes screen shader.
         */
        void initScreenShader();

        static PCamera CurrentCamera;
        static PEnvironment CurrentEnvironment;
        Color clearColor = Color::BLACK;
    public:
        /**
         * @brief Constructs a new Screen instance.
         * @param width Dimension value.
         * @param height Dimension value.
         */
        Screen(int width, int height);
        /**
         * @brief Destroys this Screen instance.
         */
        ~Screen();

        /**
         * @brief Processes the post-processing pipeline.
         */
        void processRenderPipeline();
        /**
         * @brief Draws to window.
         * @param window Value for window.
         * @param clearWindow Flag controlling clear window.
         */
        void drawToWindow(RenderWindow* window, bool clearWindow = true);
        /**
         * @brief Draws to view.
         * @param view Value for view.
         * @param clearWindow Flag controlling clear window.
         * @param x Spatial value used by this operation.
         * @param y Spatial value used by this operation.
         * @param width Dimension value.
         * @param height Dimension value.
         */
        void drawToView(PView view, bool clearWindow = true, float x = -1, float y = -1, float width = -1, float height = -1);

        /**
         * @brief Resizes internal resources.
         * @param w Value for w.
         * @param h Value for h.
         */
        void resize(int w, int h);
        /**
         * @brief Returns the width.
         * @return Computed numeric result.
         */
        int getWidth();
        /**
         * @brief Returns the height.
         * @return Computed numeric result.
         */
        int getHeight();

        /**
         * @brief Binds this resource.
         * @param clear Flag controlling clear.
         */
        void bind(bool clear = true);
        /**
         * @brief Unbinds this resource.
         */
        void unbind();

        /**
         * @brief Adds effect.
         * @param effect Value for effect.
         */
        void addEffect(Graphics::PostProcessing::PPostProcessingEffect effect);
        /**
         * @brief Clears effects.
         */
        void clearEffects();


        /**
         * @brief Returns the display texture.
         * @return Result of this operation.
         */
        PTexture getDisplayTexture();
        /**
         * @brief Returns the display buffer.
         * @return Result of this operation.
         */
        PFrameBuffer getDisplayBuffer() const;
        /**
         * @brief Returns the draw buffer.
         * @return Result of this operation.
         */
        PFrameBuffer getDrawBuffer() const;

        /**
         * @brief Returns the camera.
         * @return Result of this operation.
         */
        PCamera getCamera() const { return camera; }
        void setCamera(PCamera cam, bool makeCurrent = true);
        void makeCameraCurrent();

        PEnvironment getEnvironment() const { return environment; }
        void setEnvironment(PEnvironment env, bool makeCurrent = true);
        void makeEnvironmentCurrent();

        static void MakeCameraCurrent(PCamera camera);
        static PCamera GetCurrentCamera();
        static void MakeEnvironmentCurrent(PEnvironment env);
        static PEnvironment GetCurrentEnvironment();

        void setClearColor(Color color) { this->clearColor = color;} 
        Color& getClearColor() { return this->clearColor; }
        void clear(Color c = Color::BLACK);
        bool isBound() const { return bound; }
        float getLastPostProcessMs() const { return lastPostProcessMs.load(std::memory_order_relaxed); }
        int getLastPostProcessEffectCount() const { return lastPostProcessEffectCount.load(std::memory_order_relaxed); }
};

typedef std::shared_ptr<Screen> PScreen;

#endif //SCREEN_H
