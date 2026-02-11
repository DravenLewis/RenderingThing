#include "DemoScene.h"

#include "MaterialDefaults.h"
#include "ModelPrefabs.h"
#include "Texture.h"
#include "OBJLoader.h"
#include "LightUtils.h"
#include "ScreenEffects.h"
#include "FreeTypeFont.h"
#include "Graphics2D.h"
#include "StringUtils.h"
#include "ShadowRenderer.h"
#include "PBRMaterial.h"
#include "CubeMap.h"
#include "SkyboxMaterial.h"

#include <glad/glad.h>
#include <SDL3/SDL.h>

namespace {
    class DemoSceneInputHandler : public IEventHandler {
        public:
            explicit DemoSceneInputHandler(DemoScene* owner) : owner(owner) {}

            bool onKeyUp(int keyCode, InputManager& manager) override {
                if(!owner) return false;
                auto window = owner->getWindow();

                if(keyCode == SDL_SCANCODE_F11 && window){
                    window->toggleFullscreen();
                    return true;
                }

                if(keyCode == SDL_SCANCODE_ESCAPE && window){
                    window->close();
                    return true;
                }

                if(keyCode == SDL_SCANCODE_F10){
                    owner->toggleDebugWidgets();
                    return true;
                }

                return false;
            }

            bool onKeyDown(int keyCode, InputManager& manager) override { return false; }
            bool onMousePressed(int button, InputManager& manager) override { return false; }
            bool onMouseReleased(int button, InputManager& manager) override { return false; }
            bool onMouseMoved(int x, int y, InputManager& manager) override { return false; }
            bool onMouseScroll(float dz, InputManager& manager) override { return false; }

        private:
            DemoScene* owner = nullptr;
    };
}

DemoScene::DemoScene(RenderWindow* window) : Scene3D(window) {}

void DemoScene::setInputManager(std::shared_ptr<InputManager> manager){
    Scene3D::setInputManager(manager);
    if(fpsController && inputManager){
        fpsController->init(inputManager);
    }

    if(inputManager && !sceneInputHandler){
        sceneInputHandler = std::make_shared<DemoSceneInputHandler>(this);
        inputManager->addEventHandler(sceneInputHandler);
    }
}

void DemoScene::init(){
    auto window = getWindow();
    if(!window) return;

    ground = Model::Create();
    meshModel = Model::Create();

    mainScreen = getMainScreen();
    uiScreen = getUIScreen();

    cam = Camera::CreatePerspective(
        45.0f, 
        Math3D::Vec2(window->getWindowWidth(), window->getWindowHeight()),
        0.1f,
        1000.0f
    );

    fpsController = std::make_shared<FirstPersonController>(cam);
    if(inputManager){
        fpsController->init(inputManager);
    }

    if(mainScreen){
        mainScreen->setCamera(cam);
    }

    if(uiScreen){
        auto uiCam = Camera::CreateOrthogonal(Math3D::Rect(0,0,window->getWindowWidth(), window->getWindowHeight()), -100, 100);
        uiScreen->setCamera(uiCam,false);
        uiScreen->setClearColor(Color::CLEAR);
        graphics2d = std::make_shared<Graphics2D>(uiScreen);
    }

    PTexture textureImageTop = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/grass_top_col.png"));
    PTexture textureImageSides = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/grass_side.png"));
    PTexture textureImageBottom = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/dirt.png"));
    PTexture textureUVDefault = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/uv.png"));

    PMaterial TopMaterial = MaterialDefaults::FlatImageMaterial::Create(textureImageTop);
    PMaterial SideMaterial = MaterialDefaults::FlatImageMaterial::Create(textureImageSides);
    PMaterial BottomMaterial = MaterialDefaults::FlatImageMaterial::Create(textureImageBottom);
    
    skybox = SkyBoxLoader::CreateSkyBox("@assets/images/skybox/default", "skybox_default");

    ground->addPart(ModelPartPrefabs::MakePlane(200,200,TopMaterial));
    ground->transform().setPosition(Math3D::Vec3(0,-5,0));

    int segSize = 256;
    meshModel->addPart(ModelPartPrefabs::MakeCirclePlane(2,segSize,BottomMaterial));
    auto part = ModelPartPrefabs::MakeCirclePlane(2,segSize,BottomMaterial);
    part->localTransform.setPosition(Math3D::Vec3(0,-0.005f,0));
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

    cubeModel = ModelPrefabs::MakeCube(1.0f,cubeMaterialInformation);

    auto mat = PBRMaterial::Create(Color::WHITE);
    mat->BaseColor = Color(1.0f,1.00f,0.34f);
    mat->Metallic = 1.0f;
    mat->Roughness = 0.01f;
    mat->EmissiveStrength = 0;
    mat->OcclusionStrength = 1.0f;
    mat->NormalTex = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/golden_material/DirtyGold01_Normal.png"));
    mat->NormalScale = 0.05;
    if(skybox){
        mat->EnvMap = skybox->getCubeMap();
    }
    mat->EnvStrength = 0.7f;
    
    lucille = OBJLoader::LoadFromAsset(AssetManager::Instance.getOrLoad("@assets/models/lucille/lucille.obj"),mat);
    orb = OBJLoader::LoadFromAsset(AssetManager::Instance.getOrLoad("@assets/models/theorb/orb.obj"), MaterialDefaults::FlatImageMaterial::Create(textureUVDefault));

    if(mainScreen && mainScreen->getEnvironment()){
        auto env = mainScreen->getEnvironment();
        env->setLightingEnabled(true);
        env->setSkyBox(skybox);
        env->getLightManager().clearLights();

        auto SunLight = Light::CreateDirectionalLight(Math3D::Vec3(-0.3f, -1.0f, -0.2f), Color::fromRGBA255(255, 244, 214, 255), 1.2f);
        auto KeyPoint = Light::CreatePointLight(Math3D::Vec3(4.5f, 6.0f, 2.0f), Color::fromRGBA255(255, 230, 180, 255), 6.5f, 18.0f, 2.0f);
        auto FillPoint = Light::CreatePointLight(Math3D::Vec3(-6.0f, 3.0f, 6.0f), Color::fromRGBA255(120, 180, 255, 255), 3.0f, 20.0f, 2.0f);
        auto RimPoint = Light::CreatePointLight(Math3D::Vec3(0.0f, 7.0f, -8.0f), Color::fromRGBA255(255, 255, 255, 255), 4.0f, 20.0f, 2.0f);

        env->getLightManager().addLight(SunLight);
        env->getLightManager().addLight(KeyPoint);
        env->getLightManager().addLight(FillPoint);
        env->getLightManager().addLight(RimPoint);
    }

    if(lucille) lucille->transform().setPosition(Math3D::Vec3(0, 0, -5.0f));
    if(cubeModel) cubeModel->transform().setPosition(Math3D::Vec3(-10.0f,0,-10.0f));
    if(orb) orb->transform().setPosition(Math3D::Vec3(-10.0f,0,30.0f));

    addModel(ground);
    addModel(meshModel);
    addModel(cubeModel);
    addModel(lucille);
    addModel(orb);
}

void DemoScene::update(float deltaTime){
    lastDeltaTime = deltaTime;
    totalTime += deltaTime;

    if(deltaTime > frameTimeSpike) frameTimeSpike = deltaTime;
    if(showDebugWidgets) frameTimeGraph.push(deltaTime);

    if(fpsController && inputManager){
        fpsController->update(deltaTime, inputManager);
    }

    if(lucille){
        lucille->transform().rotateAxisAngle(Math3D::Vec3(1.0f, 1.0f, 1.0f), 50 * deltaTime);
    }

    if(cubeModel){
        cubeModel->transform().rotateAxisAngle(Math3D::Vec3(1,1,1), 50 * deltaTime);
    }

    if(orb){
        orb->transform().rotateAxisAngle(Math3D::Vec3(1,1,1), 50 * deltaTime);
    }
}

void DemoScene::render(){
    auto window = getWindow();
    if(!window) return;

    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if(mainScreen){
        mainScreen->bind();

        if(cam){
            auto env = mainScreen->getEnvironment();
            if(env && env->getSkyBox()){
                env->getSkyBox()->draw(cam);
            }
        }

        if(cam){
            for(const auto& model : models){
                if(model){
                    model->draw(cam);
                }
            }
        }

        mainScreen->unbind();
    }

    if(graphics2d && uiScreen && cam){
        graphics2d->begin();

        float currentFPS = (lastDeltaTime > 0.0f) ? (1.0f / lastDeltaTime) : 0.0f;

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

        Color c0 = Color::fromRGBA255(10,10,32,128);
        Graphics2D::SetBackgroundColor(*graphics2d, c0);
        Graphics2D::FillRect(*graphics2d, 0, 0, 600, 140);

        Color c1 = Color::WHITE;
        Graphics2D::SetForegroundColor(*graphics2d, c1);
        Graphics2D::DrawString(*graphics2d,info,30,30);

        if(showDebugWidgets){
            Color c2 = Color::fromRGBA255(10,10,32,128);
            Graphics2D::SetBackgroundColor(*graphics2d, c2);
            Graphics2D::FillRect(*graphics2d, 0, uiScreen->getHeight() - 100, uiScreen->getWidth(), 100);
            Graphics2D::SetBackgroundColor(*graphics2d, Color::WHITE);
            frameTimeGraph.draw(*graphics2d, 0, uiScreen->getHeight() - 70, uiScreen->getWidth(), 60);
        }
        
        auto shadowTex = ShadowRenderer::GetDepthBuffer();
        if(shadowTex && showDebugWidgets){
            Graphics2D::DrawImage(*graphics2d, shadowTex, uiScreen->getWidth() - (uiScreen->getWidth() / 4.0f) - 30, 30,uiScreen->getWidth() / 4.0f, uiScreen->getHeight() / 4.0f);
        }

        graphics2d->end();
    }

    drawToWindow();
    //drawToWindow(false, 30, window->getWindowHeight() - 300, 600, 300);
}

void DemoScene::dispose(){
    graphics2d.reset();
    fpsController.reset();
    models.clear();
    sceneInputHandler.reset();
}

void DemoScene::toggleDebugWidgets(){
    showDebugWidgets = !showDebugWidgets;
}
