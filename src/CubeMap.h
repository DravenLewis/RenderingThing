#ifndef CUBEMAP_H
#define CUBEMAP_H

#include <glad/glad.h>
#include <memory>

#include "Asset.h"

class CubeMap {
    private:
        GLuint textureID = 0;
        int size = 0;
        bool ownsTexture = true;

    public:
        CubeMap() = default;
        ~CubeMap();
        CubeMap(const CubeMap&) = delete;
        CubeMap& operator=(const CubeMap&) = delete;
        CubeMap(CubeMap&& other) noexcept;
        CubeMap& operator=(CubeMap&& other) noexcept;

        void bind(unsigned int slot = 0) const;
        void unbind() const;
        void dispose();

        int getSize() const { return size; }
        GLuint getID() const { return textureID; }

        static std::shared_ptr<CubeMap> Load(
            PAsset posX,
            PAsset negX,
            PAsset posY,
            PAsset negY,
            PAsset posZ,
            PAsset negZ
        );
};

typedef std::shared_ptr<CubeMap> PCubeMap;

#endif // CUBEMAP_H
