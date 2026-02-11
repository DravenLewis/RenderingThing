#ifndef DEFAULTSTATE_H
#define DEFAULTSTATE_H

#include "Scene.h"

#include <memory>

#include "Graphics2D.h"
#include "Screen.h"

class DefaultState : public Scene2D {
    public:
        explicit DefaultState(RenderWindow* window);
        ~DefaultState() override = default;

        void init() override;
        void render() override;
        void dispose() override;

    private:
        PScreen uiScreen;
        std::shared_ptr<Graphics2D> graphics2d;
};

#endif // DEFAULTSTATE_H
