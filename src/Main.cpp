
#include "windows.h"

#include "Logbot.h"
#include "RenderWindow.h"
#include "Math.h"

#include "MaterialDefaults.h"
#include "ModelPrefabs.h"
#include "Texture.h"
#include "OBJLoader.h"
#include "LightUtils.h"
#include "Camera.h"
#include "InputManager.h"

#include "FirstPersonController.h"

#include <chrono>
#include <string>

#include "Screen.h"
#include "ScreenEffects.h"

#include "FreeTypeFont.h"
#include "Graphics2D.h"

#include "debug_helpers/FrameTimeGraph.h"
#include "ShadowRenderer.h"

PModel cubeModel;
PModel lucille;
PModel meshModel;

RenderWindow* window;

float totalTime = 0.0f;
Uint64 lastFrameTime = 0;

PCamera cam;

std::shared_ptr<InputManager> manager;
std::shared_ptr<FirstPersonController> fpsC;
std::shared_ptr<Graphics2D> g;

PModel ground;

PScreen uiScreen;
PScreen mainScreen;

PTexture p;

float frameTimeSpike= 0;
bool showDepthMap = false;

#define DEBUG_COLORS 0

FrameTimeGraph frameTimeGraph;


void init();
void run();

int main(){
    try{
        DisplayMode mode = DisplayMode::New(1280,720);
        mode.resizable = true;
        window = new RenderWindow("Modern OpenGL 4 + SDL3 Cube", mode);

        manager = std::make_shared<InputManager>(std::shared_ptr<RenderWindow>(window));

        window->addWindowEventHandler([&](SDL_Event& event){
            if (event.type == SDL_EVENT_QUIT) window->close();

            if (event.type == SDL_EVENT_KEY_UP){
                if(event.key.key == SDLK_F11){
                    window->toggleFullscreen();
                }

                if(event.key.key == SDLK_F11){
                    showDepthMap = !showDepthMap;
                }

                if(event.key.key == SDLK_ESCAPE){
                    window->close();
                }


            } 

            if (event.type == SDL_EVENT_WINDOW_RESIZED){
                if(mainScreen) mainScreen->resize(window->getWindowWidth(), window->getWindowHeight());
                if(uiScreen) uiScreen->resize(window->getWindowWidth(), window->getWindowHeight());
            }
        });

        init();

        while(!window->isCloseRequested()){
            window->process();
            run();
            window->swap();
        }

        window->dispose();
        delete window;
    }catch(const std::exception& e){
        LogBot.Log(LOG_ERRO, "Exception caught in main: %s", e.what());
        if(window){
            window->dispose();
            delete window;
        }
        return 1;
    }catch(...){
        LogBot.Log(LOG_FATL, "Unknown exception caught in main");
        if(window){
            window->dispose();
            delete window;
        }
        return 1;
    }

    return 0;
}

void init(){
   
    //font = TrueTypeFont::Create(AssetManager::Instance.getOrLoad("@assets/fonts/arial.ttf"), 24.0f);
    //font = Font::Create<FreeTypeFont>(AssetManager::Instance.getOrLoad("@assets/fonts/arial.ttf"), 24.0f);

    ground = Model::Create();
    meshModel = Model::Create();

    cam = Camera::CreatePerspective(
        45.0f, 
        Math3D::Vec2(window->getWindowWidth(), window->getWindowHeight()),
        0.1f,
        1000.0f
    );

    fpsC = std::make_shared<FirstPersonController>(cam);
    fpsC->init(manager);

    mainScreen = std::make_unique<Screen>(window->getWindowWidth(), window->getWindowHeight());
    mainScreen->setCamera(cam);
    //mainScreen->addEffect(GrayscaleEffect::New()); //WORKS PERFECTLY.

    uiScreen = std::make_unique<Screen>(window->getWindowWidth(), window->getWindowHeight());
    auto uiCam = Camera::CreateOrthogonal(Math3D::Rect(0,0,window->getWindowWidth(), window->getWindowHeight()), -100, 100);
    uiScreen->setCamera(uiCam,false);
    uiScreen->setClearColor(Color::CLEAR); // Render Through Screen

    g = std::make_shared<Graphics2D>(uiScreen);

    PTexture textureImageTop = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/grass_top_col.png"));
    PTexture textureImageSides = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/grass_side.png"));
    PTexture textureImageBottom = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/dirt.png"));

    PMaterial TopMaterial = MaterialDefaults::LitImageMaterial::Create(textureImageTop);
    PMaterial SideMaterial = MaterialDefaults::LitImageMaterial::Create(textureImageSides);
    PMaterial BottomMaterial = MaterialDefaults::LitImageMaterial::Create(textureImageBottom);
    
    p = textureImageSides;

    ground->addPart(ModelPartPrefabs::MakePlane(200,200,TopMaterial));
    ground->transform().setPosition(Math3D::Vec3(0,-5,0));

    int segSize = 256;
    meshModel->addPart(ModelPartPrefabs::MakeCirclePlane(2,segSize,BottomMaterial));
    auto part = ModelPartPrefabs::MakeCirclePlane(2,segSize,BottomMaterial);
    part->localTransform.setPosition(Math3D::Vec3(0,-0.005f,0)); // try and stop Z fighting.
    part->localTransform.setRotation(180.0f,0.0f,0.0f);
    meshModel->addPart(part);
    meshModel->transform().setPosition(Math3D::Vec3(15,5,15));
    


    auto cubeMaterialInformation = ModelPrefabs::CubeMaterialDefinition::Create();
    cubeMaterialInformation->matTop = TopMaterial;
    cubeMaterialInformation->matBottom = BottomMaterial;
    cubeMaterialInformation->matLeft = SideMaterial;
    cubeMaterialInformation->matRight = SideMaterial;
    cubeMaterialInformation->matFront = SideMaterial;
    cubeMaterialInformation->matBack = SideMaterial;

    #if DEBUG_COLORS == 1
        cubeMaterialInformation->matTop = Material::Copy(cubeMaterialInformation->matTop);
        cubeMaterialInformation->matTop->set<Color>("u_color", Color::RED);

        cubeMaterialInformation->matBottom = Material::Copy(cubeMaterialInformation->matBottom);
        cubeMaterialInformation->matBottom->set<Color>("u_color", Color::GREEN);

        cubeMaterialInformation->matFront = Material::Copy(cubeMaterialInformation->matFront);
        cubeMaterialInformation->matFront->set<Color>("u_color", Color::BLUE);

        cubeMaterialInformation->matBack = Material::Copy(cubeMaterialInformation->matBack);
        cubeMaterialInformation->matBack->set<Color>("u_color", Color::CYAN);

        cubeMaterialInformation->matLeft = Material::Copy(cubeMaterialInformation->matLeft);
        cubeMaterialInformation->matLeft->set<Color>("u_color", Color::MAGENTA);

        cubeMaterialInformation->matRight = Material::Copy(cubeMaterialInformation->matRight);
        cubeMaterialInformation->matRight->set<Color>("u_color", Color::YELLOW);
    #endif

    cubeModel = ModelPrefabs::MakeCube(1.0f,cubeMaterialInformation);
    lucille = OBJLoader::LoadFromAsset(AssetManager::Instance.getOrLoad("@assets/models/lucille/lucille.obj"), MaterialDefaults::FlatImageMaterial::Create(textureImageBottom));

    auto PointLight = Light::CreatePointLight(Math3D::Vec3(0,0,0), Color::fromRGBA32(0x00FF00FF), 1.0f, 5.0f);
    auto SunLight = Light::CreateDirectionalLight(-Math3D::Vec3::up(), Color::WHITE, 1.0f);


    LightManager::GlobalLightManager.addLight(PointLight);
    LightManager::GlobalLightManager.addLight(SunLight);

    if(lucille){
        lucille->transform().setPosition(Math3D::Vec3(0, 0, -5.0f));
    }

    if(cubeModel){
        cubeModel->transform().setPosition(Math3D::Vec3(-10.0f,0,-10.0f));
    }
}

void run(){

    // Calculate delta time
    Uint64 currentFrameTime = SDL_GetTicks();
    float deltaTime = lastFrameTime > 0 ? (currentFrameTime - lastFrameTime) / 1000.0f : 0.0f;
    lastFrameTime = currentFrameTime;
    totalTime += deltaTime;

    if(deltaTime > frameTimeSpike) frameTimeSpike = deltaTime;
    frameTimeGraph.push(deltaTime);

    fpsC->update(deltaTime, manager);

    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    mainScreen->bind();

    if(lucille && cam){
        lucille->transform().rotateAxisAngle(Math3D::Vec3(1.0f, 1.0f, 1.0f), 50 * deltaTime);
        lucille->draw(cam);
    }

    cubeModel->transform().rotateAxisAngle(Math3D::Vec3(1,1,1), 50 * deltaTime);
    cubeModel->draw(cam);

    if(ground){
        ground->draw(cam);
    }

    if(meshModel){
        //meshModel->transform().rotateAxisAngle(Math3D::Vec3(1,1,1), 50 * deltaTime);
        meshModel->draw(cam);
    }

    mainScreen->unbind();


    g->begin();

    float currentFPS = (deltaTime > 0.0f) ? (1.0f / deltaTime) : 0.0f;

    std::string info = StringUtils::Format(
        "Current Player Pos: (X:%f, Y:%f, Z:%f)\nCurrent Camera View: (P:%f, Y:%f, R:%f)\nFPS: %f\nPress F11 for Fullscreen\nPress ESC to close.\nGRAVE(`) to Unlock Mouse.",
        cam->transform().position.x,
        cam->transform().position.y,
        cam->transform().position.z,
        cam->transform().rotation.x,
        cam->transform().rotation.y,
        cam->transform().rotation.z,
        currentFPS
    );

    //font->drawText(info,Math3D::Vec2(30,30),uiScreen->getCamera(),Color::WHITE, false);

    Color c1 = Color::WHITE;
    Graphics2D::SetForegroundColor(*g, c1);
    Graphics2D::DrawString(*g,info,30,30);

    //Graphics2D::SetForegroundColor(*g, Color::RED);
    //Graphics2D::DrawLine(*g,30,200,100,200);
    //Graphics2D::FillRect(*g,30,400,100,100);
    //Graphics2D::DrawImage(*g,p,150,400,100,100);
    Color c2 = Color::fromRGBA255(10,10,32,128);
    Graphics2D::SetBackgroundColor(*g, c2);
    Graphics2D::FillRect(*g, 0, uiScreen->getHeight() - 100, uiScreen->getWidth(), 100);
    Graphics2D::SetBackgroundColor(*g, Color::WHITE);
    frameTimeGraph.draw(*g, 0, uiScreen->getHeight() - 70, uiScreen->getWidth(), 60);
    
    auto shadowTex = ShadowRenderer::GetDepthBuffer();
    if(shadowTex && showDepthMap){
        Graphics2D::DrawImage(*g, shadowTex, uiScreen->getWidth() - (uiScreen->getWidth() / 4.0f) - 30, 30,uiScreen->getWidth() / 4.0f, uiScreen->getHeight() / 4.0f);
    }

    g->end();

    mainScreen->drawToWindow(window); 
    uiScreen->drawToWindow(window, false);
}

