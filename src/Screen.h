#ifndef SCREEN_H
#define SCREEN_H

#include <memory>
#include <vector>

#include "FrameBuffer.h"
#include "Texture.h"
#include "Camera.h"
#include "Math.h"
#include "ShaderProgram.h"
#include "ModelPartPrefabs.h"
#include "RenderWindow.h"
#include "Graphics.h"
#include "Color.h"
#include "Environment.h"

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
};

typedef std::shared_ptr<Screen> PScreen;

#endif //SCREEN_H
