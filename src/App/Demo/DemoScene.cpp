#include "App/Demo/DemoScene.h"

#include "Rendering/Materials/MaterialDefaults.h"
#include "Rendering/Geometry/ModelPrefabs.h"
#include "Rendering/Textures/Texture.h"
#include "Assets/Importers/OBJLoader.h"
#include "Rendering/Lighting/LightUtils.h"
#include "Rendering/PostFX/ScreenEffects.h"
#include "Rendering/Fonts/FreeTypeFont.h"
#include "Rendering/Core/Graphics2D.h"
#include "Foundation/Util/StringUtils.h"
#include "Rendering/Lighting/ShadowRenderer.h"
#include "Rendering/Materials/PBRMaterial.h"
#include "Rendering/Textures/CubeMap.h"
#include "Rendering/Materials/SkyboxMaterial.h"
#include "ECS/Core/ECSComponents.h"
#include "Foundation/Logging/Logbot.h"

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

                if(keyCode == SDL_SCANCODE_ESCAPE){
                    LogBot.Log(LOG_WARN, "DemoSceneInputHandler ESC -> requestClose()");
                    owner->requestClose();
                    return true;
                }

                if(keyCode == SDL_SCANCODE_F10){
                    owner->toggleDebugWidgets();
                    return true;
                }
                
                //if(keyCode == SDL_SCANCODE_F9){
                //    owner->toggleDebugShadows();
                //    return true;
                //}

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

    NeoECS::GameObject* spawnModelEntity(Scene* scene, const std::string& name, const PModel& model, NeoECS::GameObject* parent = nullptr) {
        if(!scene) return nullptr;
        return scene->createModelGameObject(name, model, parent);
    }
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
    renderBox = Model::Create();

    mainScreen = getMainScreen();
    uiScreen = getUIScreen();

    demoCameraObject = createECSGameObject("DemoCamera");
    if(demoCameraObject){
        demoCameraObject->addComponent<TransformComponent>();
        demoCameraObject->addComponent<CameraComponent>();
        demoCameraObject->addComponent<BoundsComponent>();
        demoCameraObject->addComponent<SSAOComponent>();
        demoCameraObject->addComponent<DepthOfFieldComponent>();
        demoCameraObject->addComponent<AntiAliasingComponent>();

        demoCameraTransform = demoCameraObject->getComponent<TransformComponent>();
        demoCameraComponent = demoCameraObject->getComponent<CameraComponent>();
        auto* bounds = demoCameraObject->getComponent<BoundsComponent>();
        auto* ssao = demoCameraObject->getComponent<SSAOComponent>();
        auto* dof = demoCameraObject->getComponent<DepthOfFieldComponent>();
        auto* aa = demoCameraObject->getComponent<AntiAliasingComponent>();

        if(demoCameraTransform){
            demoCameraTransform->local.setPosition(Math3D::Vec3(0.0f, 1.5f, 8.0f));
            demoCameraTransform->local.setRotation(Math3D::Vec3(0.0f, -180.0f, 0.0f));
        }

        if(demoCameraComponent){
            demoCameraComponent->camera = Camera::CreatePerspective(
                45.0f,
                Math3D::Vec2(window->getWindowWidth(), window->getWindowHeight()),
                0.1f,
                1000.0f
            );
            cam = demoCameraComponent->camera;
            if(demoCameraTransform && cam){
                cam->setTransform(demoCameraTransform->local);
            }
        }

        if(bounds){
            bounds->type = BoundsType::Sphere;
            bounds->radius = 0.4f;
        }
        if(ssao){
            ssao->enabled = true;
            ssao->radiusPx = 2.5f;
            ssao->depthRadius = 0.015f;
            ssao->bias = 0.0008f;
            ssao->intensity = 0.75f;
            ssao->giBoost = 0.05f;
            ssao->sampleCount = 6;
        }
        if(dof){
            dof->enabled = true;
            dof->focusDistance = 14.0f;
            dof->focusRange = 10.0f;
            dof->blurStrength = 0.35f;
            dof->maxBlurPx = 4.0f;
            dof->sampleCount = 5;
        }
        if(aa){
            aa->preset = AntiAliasingPreset::FXAA_High;
        }
    }

    if(cam){
        setPreferredCamera(cam);
    }else if(mainScreen){
        cam = Camera::CreatePerspective(
            45.0f,
            Math3D::Vec2(window->getWindowWidth(), window->getWindowHeight()),
            0.1f,
            1000.0f
        );
        setPreferredCamera(cam);
    }

    fpsController = std::make_shared<FirstPersonController>(cam);
    if(inputManager && fpsController){
        fpsController->init(inputManager);
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

    skybox = SkyBoxLoader::CreateSkyBox("@assets/images/skybox/default", "skybox_default");

    auto makePbrTextured = [&](const PTexture& tex, float metallic, float roughness, float envStrength) -> std::shared_ptr<PBRMaterial> {
        auto mat = PBRMaterial::Create(Color::WHITE);
        if(!mat){
            return nullptr;
        }
        mat->BaseColorTex = tex;
        mat->Metallic = metallic;
        mat->Roughness = roughness;
        mat->OcclusionStrength = 1.0f;
        if(skybox && skybox->getCubeMap()){
            mat->EnvMap = skybox->getCubeMap();
            mat->UseEnvMap = 1;
        }else{
            mat->UseEnvMap = 0;
        }
        mat->EnvStrength = envStrength;
        return mat;
    };

    auto groundPBR = makePbrTextured(textureImageTop, 0.0f, 0.95f, 0.08f);
    auto topPBR = makePbrTextured(textureImageTop, 0.0f, 0.90f, 0.10f);
    auto sidePBR = makePbrTextured(textureImageSides, 0.0f, 0.95f, 0.08f);
    auto bottomPBR = makePbrTextured(textureImageBottom, 0.0f, 0.98f, 0.04f);
    auto uvPBR = makePbrTextured(textureUVDefault, 0.05f, 0.45f, 0.55f);
    if(uvPBR){
        uvPBR->OcclusionStrength = 0.65f;
    }

    ground->addPart(ModelPartPrefabs::MakePlane(200,200,groundPBR));
    ground->transform().setPosition(Math3D::Vec3(0,-5,0));

    int segSize = 256;
    meshModel->addPart(ModelPartPrefabs::MakeCirclePlane(2,segSize,bottomPBR));
    auto part = ModelPartPrefabs::MakeCirclePlane(2,segSize,bottomPBR);
    part->localTransform.setPosition(Math3D::Vec3(0,-0.005f,0));
    part->localTransform.setRotation(180.0f,0.0f,0.0f);
    meshModel->addPart(part);
    meshModel->transform().setPosition(Math3D::Vec3(15,5,15));

    auto cubeMaterialInformation = ModelPrefabs::CubeMaterialDefinition::Create();
    cubeMaterialInformation->matTop = topPBR;
    cubeMaterialInformation->matBottom = bottomPBR;
    cubeMaterialInformation->matLeft = sidePBR;
    cubeMaterialInformation->matRight = sidePBR;
    cubeMaterialInformation->matFront = sidePBR;
    cubeMaterialInformation->matBack = sidePBR;

    cubeModel = ModelPrefabs::MakeCube(1.0f,cubeMaterialInformation);

    auto mat = PBRMaterial::Create(Color::WHITE);
    mat->BaseColor = Color::fromRGB24(0xD4AF37);
    mat->Metallic = 0.95f;
    mat->Roughness = 0.08f;
    mat->EmissiveStrength = 0;
    mat->OcclusionStrength = 1.0f;
    mat->NormalTex = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/golden_material/DirtyGold01_Normal.png"));
    mat->NormalScale = 0.05;
    if(skybox){
        mat->EnvMap = skybox->getCubeMap();
    }
    mat->EnvStrength = 0.55f;
    
    lucille = OBJLoader::LoadFromAsset(AssetManager::Instance.getOrLoad("@assets/models/lucille/lucille.obj"),mat);
    orb = OBJLoader::LoadFromAsset(AssetManager::Instance.getOrLoad("@assets/models/theorb/orb.obj"), uvPBR, true);

    PTexture matSWBase = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/Materials/StoneWall03/MAT_SW_ALBEDO.png"));
    PTexture matSWAo = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/Materials/StoneWall03/MAT_SW_AO.png"));
    PTexture matSWHeight = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/Materials/StoneWall03/MAT_SW_HEIGHT.png"));
    PTexture matSWNormal = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/Materials/StoneWall03/MAT_SW_NORMAL.png"));
    PTexture matSWRough = Texture::Load(AssetManager::Instance.getOrLoad("@assets/images/Materials/StoneWall03/MAT_SW_ROUGHNESS.png"));

    auto boxPbrMat = PBRMaterial::Create();
    boxPbrMat->BaseColorTex = matSWBase;
    boxPbrMat->OcclusionTex = matSWAo;
    boxPbrMat->HeightTex = matSWHeight;
    boxPbrMat->NormalTex = matSWNormal;
    boxPbrMat->NormalScale = 1.0f;
    boxPbrMat->RoughnessTex = matSWRough;

    auto modelBoxModelPart = ModelPartPrefabs::MakeBox(1.0f, 1.0f, 1.0f, boxPbrMat);
    renderBox->addPart(modelBoxModelPart);
   


    if(mainScreen && mainScreen->getEnvironment()){
        auto env = mainScreen->getEnvironment();
        env->setLightingEnabled(true);
        env->setSkyBox(skybox);
    }

    auto SunLight = Light::CreateDirectionalLight(Math3D::Vec3(-0.3f, -1.0f, -0.2f), Color::fromRGBA255(255, 208, 180, 255), 3.0f);
    SunLight.shadowRange = 90.0f;
    SunLight.shadowBias = 0.0035f;
    SunLight.shadowNormalBias = 0.015f;
    auto KeyPoint = Light::CreatePointLight(Math3D::Vec3(4.5f, 6.0f, 2.0f), Color::fromRGBA255(255, 230, 180, 255), 6.5f, 18.0f, 2.0f);
    auto FillPoint = Light::CreatePointLight(Math3D::Vec3(-6.0f, 3.0f, 6.0f), Color::fromRGBA255(120, 180, 255, 255), 3.0f, 20.0f, 2.0f);
    FillPoint.castsShadows = false;
    auto RimPoint = Light::CreatePointLight(Math3D::Vec3(0.0f, 7.0f, -8.0f), Color::fromRGBA255(255, 255, 255, 255), 4.0f, 20.0f, 2.0f);
    RimPoint.castsShadows = false;

    auto RedPoint = Light::CreatePointLight(Math3D::Vec3(-2.0f, 2.0f, -2.0f), Color::fromRGBA255(255, 60, 60, 255), 6.0f, 8.0f, 2.0f);
    auto GreenPoint = Light::CreatePointLight(Math3D::Vec3(2.0f, 2.0f, -2.0f), Color::fromRGBA255(60, 255, 80, 255), 6.0f, 8.0f, 2.0f);
    auto BluePoint = Light::CreatePointLight(Math3D::Vec3(-2.0f, 2.0f, 2.0f), Color::fromRGBA255(80, 140, 255, 255), 6.0f, 8.0f, 2.0f);
    auto YellowPoint = Light::CreatePointLight(Math3D::Vec3(2.0f, 2.0f, 2.0f), Color::fromRGBA255(255, 255, 80, 255), 6.0f, 8.0f, 2.0f);
    RedPoint.castsShadows = false;
    GreenPoint.castsShadows = false;
    BluePoint.castsShadows = false;
    YellowPoint.castsShadows = false;

    ShadowRenderer::SetDebugShadows(showDebugShadows);

    groundObject = spawnModelEntity(this, "Ground", ground);
    meshObject = spawnModelEntity(this, "MeshModel", meshModel);
    cubeObject = spawnModelEntity(this, "CubeModel", cubeModel);
    lucilleObject = spawnModelEntity(this, "Lucille", lucille);
    orbObject = spawnModelEntity(this, "Orb", orb);
    renderBoxGameObject = spawnModelEntity(this, "Render Cube", renderBox);

    if(renderBoxGameObject){
        if(auto* transform = renderBoxGameObject->getComponent<TransformComponent>()){
            transform->local.setPosition(Math3D::Vec3(-7, -2, 10));
        }
    }

    if(lucilleObject){
        if(auto* transform = lucilleObject->getComponent<TransformComponent>()){
            transform->local.setPosition(Math3D::Vec3(0, 0, -5.0f));
        }
    }
    if(cubeObject){
        if(auto* transform = cubeObject->getComponent<TransformComponent>()){
            transform->local.setPosition(Math3D::Vec3(-10.0f,0,-10.0f));
        }
    }
    if(orbObject){
        if(auto* transform = orbObject->getComponent<TransformComponent>()){
            transform->local.setPosition(Math3D::Vec3(-10.0f,0,30.0f));
        }
    }

    auto lSun = createLightGameObject("SunLight", SunLight, nullptr, false, false);
    if(lSun){
        auto sunTransform = lSun->getComponent<TransformComponent>();
        if(sunTransform) 
            sunTransform->local.rotateAxisAngle(Math3D::Vec3(1,0,0),90); // face down.
    }

    createLightGameObject("KeyLight", KeyPoint, nullptr, true, false);
    createLightGameObject("FillLight", FillPoint, nullptr, true, false);
    createLightGameObject("RimLight", RimPoint, nullptr, true, false);
    createLightGameObject("TestRedLight", RedPoint, nullptr, true, false);
    createLightGameObject("TestGreenLight", GreenPoint, nullptr, true, false);
    createLightGameObject("TestBlueLight", BluePoint, nullptr, true, false);
    createLightGameObject("TestYellowLight", YellowPoint, nullptr, true, false);
}

void DemoScene::update(float deltaTime){
    lastDeltaTime = deltaTime;
    totalTime += deltaTime;

    if(deltaTime > frameTimeSpike) frameTimeSpike = deltaTime;
    if(showDebugWidgets) frameTimeGraph.push(deltaTime);

    if(fpsController && inputManager){
        fpsController->update(deltaTime, inputManager);
    }

    if(demoCameraTransform && cam){
        demoCameraTransform->local = cam->transform();
    }

    if(lucilleObject){
        if(auto* transform = lucilleObject->getComponent<TransformComponent>()){
            transform->local.rotateAxisAngle(Math3D::Vec3(1.0f, 1.0f, 1.0f), 50 * deltaTime);
        }
    }

    if(cubeObject){
        if(auto* transform = cubeObject->getComponent<TransformComponent>()){
            transform->local.rotateAxisAngle(Math3D::Vec3(1,1,1), 50 * deltaTime);
        }
    }

    if(orbObject){
        if(auto* transform = orbObject->getComponent<TransformComponent>()){
            transform->local.rotateAxisAngle(Math3D::Vec3(1,1,1), 50 * deltaTime);
        }
    }
}

void DemoScene::render(){
    auto window = getWindow();
    if(!window) return;

    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    render3DPass();

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

        Color c0 = Color::fromRGBA255(10,10,32,200);
        Graphics2D::SetBackgroundColor(*graphics2d, c0);
        Graphics2D::FillRect(*graphics2d, 0, 0, 600, 140);

        Color c1 = Color::WHITE;
        Graphics2D::SetForegroundColor(*graphics2d, c1);
        Graphics2D::DrawString(*graphics2d,info,30,30);

        if(showDebugWidgets){
            Color c2 = Color::fromRGBA255(10,10,32,200);
            Graphics2D::SetBackgroundColor(*graphics2d, c2);
            Graphics2D::FillRect(*graphics2d, 0, uiScreen->getHeight() - 130, uiScreen->getWidth(), 130);
            Graphics2D::SetBackgroundColor(*graphics2d, Color::WHITE);
            auto& stats = getDebugStats();
            frameTimeGraph.setEcsInfo(
                stats.snapshotMs.load(std::memory_order_relaxed),
                stats.shadowMs.load(std::memory_order_relaxed),
                stats.drawMs.load(std::memory_order_relaxed),
                stats.drawCount.load(std::memory_order_relaxed),
                stats.lightCount.load(std::memory_order_relaxed)
            );
            frameTimeGraph.draw(*graphics2d, 0, uiScreen->getHeight() - 70, uiScreen->getWidth(), 60);
        }
        
        auto shadowTex = ShadowRenderer::GetDepthBuffer();
        if(shadowTex && showDebugWidgets){
            Graphics2D::DrawImage(*graphics2d, shadowTex, uiScreen->getWidth() - (uiScreen->getWidth() / 4.0f) - 30, 30,uiScreen->getWidth() / 4.0f, uiScreen->getHeight() / 4.0f);
        }

        graphics2d->end();
    }

    //drawToWindow();
    //drawToWindow(false, 30, window->getWindowHeight() - 300, 600, 300);
}

void DemoScene::dispose(){
    demoCameraObject = nullptr;
    demoCameraComponent = nullptr;
    demoCameraTransform = nullptr;
    graphics2d.reset();
    fpsController.reset();
    sceneInputHandler.reset();
    Scene3D::dispose();
}

void DemoScene::toggleDebugWidgets(){
    showDebugWidgets = !showDebugWidgets;
}

void DemoScene::toggleDebugShadows(){
    ShadowRenderer::CycleDebugShadows();
    showDebugShadows = ShadowRenderer::GetDebugShadows();
}
