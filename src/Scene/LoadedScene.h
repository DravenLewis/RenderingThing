/**
 * @file src/Scene/LoadedScene.h
 * @brief Declarations for LoadedScene.
 */

#ifndef SCENE_LOADED_SCENE_H
#define SCENE_LOADED_SCENE_H

#include <filesystem>
#include <string>

#include "Assets/Core/Asset.h"
#include "Scene/Scene.h"

/// @brief Represents the LoadedScene type.
class LoadedScene : public Scene3D {
    public:
        /**
         * @brief Constructs a new LoadedScene instance.
         * @param window Value for window.
         * @param sceneRefOrPath Filesystem path for scene reference or path.
         * @param baseDirectory Filesystem path for base directory.
         */
        explicit LoadedScene(RenderWindow* window = nullptr,
                             std::string sceneRefOrPath = std::string(),
                             std::filesystem::path baseDirectory = std::filesystem::path());

        /**
         * @brief Sets the scene ref or path.
         * @param value Value for value.
         */
        void setSceneRefOrPath(const std::string& value) { sceneRefOrPath = value; }
        const std::string& getSceneRefOrPath() const { return sceneRefOrPath; }

        void setBaseDirectory(const std::filesystem::path& value) { baseDirectory = value; }
        const std::filesystem::path& getBaseDirectory() const { return baseDirectory; }

        void setSourceScenePath(const std::filesystem::path& value) {
            sourceScenePath = value.empty() ? std::filesystem::path() : value.lexically_normal();
        }
        const std::filesystem::path& getSourceScenePath() const { return sourceScenePath; }
        void setSourceSceneAsset(const PAsset& value) { sourceSceneAsset = value; }
        const PAsset& getSourceSceneAsset() const { return sourceSceneAsset; }

        bool didLoadSuccessfully() const { return sceneLoaded; }
        const std::string& getLastLoadError() const { return lastLoadError; }

        void init() override;
        void render() override;

    private:
        bool loadSceneDocument();
        void ensureCameraAfterLoad();

        std::string sceneRefOrPath;
        std::filesystem::path baseDirectory;
        std::filesystem::path sourceScenePath;
        PAsset sourceSceneAsset;
        bool sceneLoaded = false;
        std::string lastLoadError;
};

#endif // SCENE_LOADED_SCENE_H
