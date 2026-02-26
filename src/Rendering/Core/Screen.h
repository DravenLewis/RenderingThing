#ifndef SCREEN_H
#define SCREEN_H

#include <memory>
#include <vector>
#include <atomic>

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

        void initScreenGeom();
        void initScreenShader();

        static PCamera CurrentCamera;
        static PEnvironment CurrentEnvironment;
        Color clearColor = Color::BLACK;
    public:
        Screen(int width, int height);
        ~Screen();

        void processRenderPipeline();
        void drawToWindow(RenderWindow* window, bool clearWindow = true);
        void drawToView(PView view, bool clearWindow = true, float x = -1, float y = -1, float width = -1, float height = -1);

        void resize(int w, int h);
        int getWidth();
        int getHeight();

        void bind(bool clear = true);
        void unbind();

        void addEffect(Graphics::PostProcessing::PPostProcessingEffect effect);
        void clearEffects();


        PTexture getDisplayTexture();
        PFrameBuffer getDisplayBuffer() const;
        PFrameBuffer getDrawBuffer() const;

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
