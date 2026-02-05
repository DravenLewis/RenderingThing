#ifndef DRAWABLE_H
#define DRAWABLE_H

#include <Math.h>

struct IDrawable{
    virtual void draw(
        const Math3D::Mat4& parent = Math3D::Mat4(),
        const Math3D::Mat4& viewMtx = Math3D::Mat4(),
        const Math3D::Mat4& projectionMtx = Math3D::Mat4()
    ) = 0;
    
    ~IDrawable() = default;
};

#endif //DRAWABLE_H