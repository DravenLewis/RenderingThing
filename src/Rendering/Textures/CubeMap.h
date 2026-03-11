/**
 * @file src/Rendering/Textures/CubeMap.h
 * @brief Declarations for CubeMap.
 */

#ifndef CUBEMAP_H
#define CUBEMAP_H

#include <glad/glad.h>
#include <memory>

#include "Assets/Core/Asset.h"

/// @brief Represents the CubeMap type.
class CubeMap {
    private:
        GLuint textureID = 0;
        int size = 0;
        bool ownsTexture = true;

    public:
        /**
         * @brief Constructs a new CubeMap instance.
         */
        CubeMap() = default;
        /**
         * @brief Destroys this CubeMap instance.
         */
        ~CubeMap();
        /**
         * @brief Constructs a new CubeMap instance.
         */
        CubeMap(const CubeMap&) = delete;
        /**
         * @brief Assigns from another instance.
         */
        CubeMap& operator=(const CubeMap&) = delete;
        /**
         * @brief Constructs a new CubeMap instance.
         * @param other Value for other.
         */
        CubeMap(CubeMap&& other) noexcept;
        /**
         * @brief Assigns from another instance.
         * @param other Value for other.
         */
        CubeMap& operator=(CubeMap&& other) noexcept;

        /**
         * @brief Binds this resource.
         * @param slot Value for slot.
         */
        void bind(unsigned int slot = 0) const;
        /**
         * @brief Unbinds this resource.
         */
        void unbind() const;
        /**
         * @brief Disposes this object.
         */
        void dispose();

        /**
         * @brief Returns the size.
         * @return Computed numeric result.
         */
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
