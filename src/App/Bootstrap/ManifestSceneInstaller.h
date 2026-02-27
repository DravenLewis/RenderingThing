#ifndef APP_BOOTSTRAP_MANIFEST_SCENE_INSTALLER_H
#define APP_BOOTSTRAP_MANIFEST_SCENE_INSTALLER_H

#include <filesystem>
#include <functional>
#include <string>

#include "Engine/Core/GameEngine.h"
#include "Scene/Scene.h"
#include "Serialization/Schema/ManifestSchemas.h"

namespace AppBootstrap {

class ManifestSceneInstaller {
    public:
        struct InstallOptions {
            std::function<PScene(RenderWindow*)> fallbackFactory;
            bool preferManifestSceneInEditor = true;
        };

        explicit ManifestSceneInstaller(GameEngine& engineRef);

        bool tryInstallFromCommandLine(
            int argc,
            char** argv,
            int* outStateId = nullptr
        );

        bool tryInstallFromCommandLine(
            int argc,
            char** argv,
            const InstallOptions& options,
            int* outStateId = nullptr
        );

        bool tryInstall(
            bool forceRuntime = false,
            bool forceEditor = false,
            int* outStateId = nullptr
        );

        bool tryInstall(
            const InstallOptions& options,
            bool forceRuntime = false,
            bool forceEditor = false,
            int* outStateId = nullptr
        );

        const std::string& lastError() const { return lastErrorMessage; }
        bool usedManifest() const { return manifestUsed; }
        const std::filesystem::path& manifestPath() const { return loadedManifestPath; }
        const std::string& startupSceneRef() const { return loadedStartupSceneRef; }

    private:
        bool hasFlag(int argc, char** argv, const char* flag) const;
        std::filesystem::path findManifestPath() const;
        EngineRenderStrategy parseRenderPipeline(const std::string& pipeline) const;
        std::string buildWindowTitleFromManifest(const JsonSchema::GameManifestSchema& manifest) const;
        void applyManifestWindowAndRenderSettings(const JsonSchema::GameManifestSchema& manifest) const;
        void clearLastResult();

        GameEngine& engine;
        bool manifestUsed = false;
        std::filesystem::path loadedManifestPath;
        std::string loadedStartupSceneRef;
        std::string lastErrorMessage;
};

} // namespace AppBootstrap

#endif // APP_BOOTSTRAP_MANIFEST_SCENE_INSTALLER_H
