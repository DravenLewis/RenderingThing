/**
 * @file src/Rendering/Geometry/Drawable.h
 * @brief Declarations for Drawable.
 */

#ifndef DRAWABLE_H
#define DRAWABLE_H

#include "Foundation/Math/Math3D.h"

/// @brief Holds data for IDrawable.
struct IDrawable{
    /**
     * @brief Draws this object.
     * @param parent Value for parent.
     * @param viewMtx Value for view mtx.
     * @param projectionMtx Value for projection mtx.
     */
    virtual void draw(
        const Math3D::Mat4& parent = Math3D::Mat4(),
        const Math3D::Mat4& viewMtx = Math3D::Mat4(),
        const Math3D::Mat4& projectionMtx = Math3D::Mat4()
    ) = 0;
    
    /**
     * @brief Destroys this IDrawable instance.
     */
    ~IDrawable() = default;
};

#endif //DRAWABLE_H
