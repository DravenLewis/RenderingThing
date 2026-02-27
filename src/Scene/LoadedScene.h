#ifndef SCENE_LOADED_SCENE_H
#define SCENE_LOADED_SCENE_H

#include <filesystem>
#include <string>

#include "Scene/Scene.h"

class LoadedScene : public Scene3D {
    public:
        explicit LoadedScene(RenderWindow* window = nullptr,
                             std::string sceneRefOrPath = std::string(),
                             std::filesystem::path baseDirectory = std::filesystem::path());

        void setSceneRefOrPath(const std::string& value) { sceneRefOrPath = value; }
        const std::string& getSceneRefOrPath() const { return sceneRefOrPath; }

        void setBaseDirectory(const std::filesystem::path& value) { baseDirectory = value; }
        const std::filesystem::path& getBaseDirectory() const { return baseDirectory; }

        bool didLoadSuccessfully() const { return sceneLoaded; }
        const std::string& getLastLoadError() const { return lastLoadError; }

        void init() override;
        void render() override;

    private:
        bool loadSceneDocument();
        void ensureCameraAfterLoad();

        std::string sceneRefOrPath;
        std::filesystem::path baseDirectory;
        bool sceneLoaded = false;
        std::string lastLoadError;
};

#endif // SCENE_LOADED_SCENE_H
