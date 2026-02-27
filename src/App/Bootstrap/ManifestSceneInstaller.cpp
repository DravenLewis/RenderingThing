#include "App/Bootstrap/ManifestSceneInstaller.h"

#include <algorithm>

#include "Editor/Core/EditorScene.h"
#include "Foundation/IO/File.h"
#include "Foundation/Logging/Logbot.h"
#include "Foundation/Util/StringUtils.h"
#include "Scene/LoadedScene.h"
#include "Serialization/IO/ManifestIO.h"

namespace AppBootstrap {

ManifestSceneInstaller::ManifestSceneInstaller(GameEngine& engineRef)
    : engine(engineRef) {
}

bool ManifestSceneInstaller::hasFlag(int argc, char** argv, const char* flag) const{
    if(!flag){
        return false;
    }
    for(int i = 1; i < argc; ++i){
        if(argv[i] && std::string(argv[i]) == flag){
            return true;
        }
    }
    return false;
}

std::filesystem::path ManifestSceneInstaller::findManifestPath() const{
    const std::filesystem::path cwd(File::GetCWD());
    const std::filesystem::path preferred = cwd / "game.MANIFEST";
    std::error_code ec;
    if(std::filesystem::exists(preferred, ec) && !ec){
        return preferred;
    }

    std::filesystem::path firstMatch;
    for(const auto& entry : std::filesystem::directory_iterator(cwd, ec)){
        if(ec){
            break;
        }
        const bool isRegularFile = entry.is_regular_file(ec);
        if(ec || !isRegularFile){
            ec.clear();
            continue;
        }
        const std::string extension = StringUtils::ToLowerCase(entry.path().extension().string());
        if(extension != ".manifest"){
            continue;
        }
        if(firstMatch.empty() || entry.path().filename().string() < firstMatch.filename().string()){
            firstMatch = entry.path();
        }
    }
    return firstMatch;
}

EngineRenderStrategy ManifestSceneInstaller::parseRenderPipeline(const std::string& pipeline) const{
    const std::string normalized = StringUtils::ToLowerCase(StringUtils::Trim(pipeline));
    if(normalized == "deferred"){
        return EngineRenderStrategy::Deferred;
    }
    return EngineRenderStrategy::Forward;
}

std::string ManifestSceneInstaller::buildWindowTitleFromManifest(const JsonSchema::GameManifestSchema& manifest) const{
    if(!manifest.window.title.empty()){
        return manifest.window.title;
    }
    if(!manifest.game.name.empty()){
        return manifest.game.name;
    }
    return std::string();
}

void ManifestSceneInstaller::applyManifestWindowAndRenderSettings(const JsonSchema::GameManifestSchema& manifest) const{
    engine.setRenderStrategy(parseRenderPipeline(manifest.render.defaultPipeline));

    RenderWindow* window = engine.window();
    if(!window){
        return;
    }

    const int width = std::max(1, manifest.window.width);
    const int height = std::max(1, manifest.window.height);
    window->setWindowSize(width, height);
    window->setResizable(manifest.window.resizable);

    const std::string title = buildWindowTitleFromManifest(manifest);
    if(!title.empty()){
        window->setWindowName(title);
    }
}

void ManifestSceneInstaller::clearLastResult(){
    manifestUsed = false;
    loadedManifestPath.clear();
    loadedStartupSceneRef.clear();
    lastErrorMessage.clear();
}

bool ManifestSceneInstaller::tryInstallFromCommandLine(
    int argc,
    char** argv,
    int* outStateId)
{
    InstallOptions options;
    return tryInstallFromCommandLine(argc, argv, options, outStateId);
}

bool ManifestSceneInstaller::tryInstallFromCommandLine(
    int argc,
    char** argv,
    const InstallOptions& options,
    int* outStateId)
{
    const bool forceRuntime = hasFlag(argc, argv, "--runtime");
    const bool forceEditor = hasFlag(argc, argv, "--editor");
    return tryInstall(options, forceRuntime, forceEditor, outStateId);
}

bool ManifestSceneInstaller::tryInstall(
    bool forceRuntime,
    bool forceEditor,
    int* outStateId)
{
    InstallOptions options;
    return tryInstall(options, forceRuntime, forceEditor, outStateId);
}

bool ManifestSceneInstaller::tryInstall(
    const InstallOptions& options,
    bool forceRuntime,
    bool forceEditor,
    int* outStateId)
{
    clearLastResult();
    if(outStateId){
        *outStateId = -1;
    }

    const std::filesystem::path manifestPath = findManifestPath();
    if(manifestPath.empty()){
        lastErrorMessage = "No .MANIFEST file found.";
        return false;
    }

    JsonSchema::GameManifestSchema manifestSchema;
    if(!ManifestIO::LoadGameManifestFromAbsolutePath(manifestPath, manifestSchema, &lastErrorMessage)){
        return false;
    }

    std::string startupSceneRef;
    if(!ManifestIO::ResolveStartupSceneRef(manifestSchema, startupSceneRef, &lastErrorMessage)){
        return false;
    }

    applyManifestWindowAndRenderSettings(manifestSchema);

    const std::filesystem::path startupSceneBaseDirectory = manifestPath.parent_path();
    auto loadedSceneFactory = [startupSceneRef, startupSceneBaseDirectory](RenderWindow* window) -> PScene {
        return std::make_shared<LoadedScene>(window, startupSceneRef, startupSceneBaseDirectory);
    };

    const bool runRuntime = forceRuntime && !forceEditor;
    int stateId = -1;
    if(runRuntime){
        stateId = engine.addState(loadedSceneFactory(engine.window()));
    }else{
        std::function<PScene(RenderWindow*)> targetFactory = options.fallbackFactory;
        if(options.preferManifestSceneInEditor){
            targetFactory = loadedSceneFactory;
        }
        if(!targetFactory){
            lastErrorMessage = "Cannot install editor state: fallback factory is null.";
            return false;
        }
        PScene targetScene = targetFactory(engine.window());
        stateId = engine.addState(std::make_shared<EditorScene>(engine.window(), targetScene, targetFactory));
    }

    if(stateId < 0){
        lastErrorMessage = "Failed to add bootstrapped scene state.";
        return false;
    }
    if(!engine.enterState(stateId)){
        lastErrorMessage = "Failed to enter bootstrapped scene state.";
        return false;
    }

    if(outStateId){
        *outStateId = stateId;
    }

    manifestUsed = true;
    loadedManifestPath = manifestPath;
    loadedStartupSceneRef = startupSceneRef;
    return true;
}

} // namespace AppBootstrap
