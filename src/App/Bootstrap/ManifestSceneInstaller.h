/**
 * @file src/App/Bootstrap/ManifestSceneInstaller.h
 * @brief Declarations for ManifestSceneInstaller.
 */

#ifndef APP_BOOTSTRAP_MANIFEST_SCENE_INSTALLER_H
#define APP_BOOTSTRAP_MANIFEST_SCENE_INSTALLER_H

#include <filesystem>
#include <functional>
#include <string>

#include "Engine/Core/GameEngine.h"
#include "Scene/Scene.h"
#include "Serialization/Schema/ManifestSchemas.h"

namespace AppBootstrap {

/// @brief Represents the ManifestSceneInstaller type.
class ManifestSceneInstaller {
    public:
        /// @brief Holds data for InstallOptions.
        struct InstallOptions {
            std::function<PScene(RenderWindow*)> fallbackFactory;
            bool preferManifestSceneInEditor = true;
        };

        /**
         * @brief Constructs a new ManifestSceneInstaller instance.
         * @param engineRef Reference to engine.
          * @return Result of this operation.
         */
        explicit ManifestSceneInstaller(GameEngine& engineRef);

        /**
         * @brief Checks whether try install from command line.
         * @param argc Value for argc.
         * @param argv Value for argv.
         * @param outStateId Identifier or index value.
         * @return True when the operation succeeds; otherwise false.
         */
        bool tryInstallFromCommandLine(
            int argc,
            char** argv,
            int* outStateId = nullptr
        );

        /**
         * @brief Checks whether try install from command line.
         * @param argc Value for argc.
         * @param argv Value for argv.
         * @param options Configuration options.
         * @param outStateId Identifier or index value.
         * @return True when the operation succeeds; otherwise false.
         */
        bool tryInstallFromCommandLine(
            int argc,
            char** argv,
            const InstallOptions& options,
            int* outStateId = nullptr
        );

        /**
         * @brief Checks whether try install.
         * @param forceRuntime Flag controlling force runtime.
         * @param forceEditor Flag controlling force editor.
         * @param outStateId Identifier or index value.
         * @return True when the operation succeeds; otherwise false.
         */
        bool tryInstall(
            bool forceRuntime = false,
            bool forceEditor = false,
            int* outStateId = nullptr
        );

        /**
         * @brief Checks whether try install.
         * @param options Configuration options.
         * @param forceRuntime Flag controlling force runtime.
         * @param forceEditor Flag controlling force editor.
         * @param outStateId Identifier or index value.
         * @return True when the operation succeeds; otherwise false.
         */
        bool tryInstall(
            const InstallOptions& options,
            bool forceRuntime = false,
            bool forceEditor = false,
            int* outStateId = nullptr
        );

        /**
         * @brief Returns the last installation error message.
         * @return Resulting string value.
         */
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
