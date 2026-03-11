/**
 * @file src/App/Demo/DefaultState.h
 * @brief Declarations for DefaultState.
 */

#ifndef DEFAULTSTATE_H
#define DEFAULTSTATE_H

#include "Scene/Scene.h"

#include <memory>

#include "Rendering/Core/Graphics2D.h"
#include "Rendering/Core/Screen.h"

/// @brief Represents the DefaultState type.
class DefaultState : public Scene2D {
    public:
        /**
         * @brief Constructs a new DefaultState instance.
         * @param window Value for window.
          * @return Result of this operation.
         */
        explicit DefaultState(RenderWindow* window);
        /**
         * @brief Destroys this DefaultState instance.
         */
        ~DefaultState() override = default;

        /**
         * @brief Initializes this object.
         */
        void init() override;
        /**
         * @brief Renders this object.
         */
        void render() override;
        /**
         * @brief Disposes this object.
         */
        void dispose() override;

    private:
        PScreen uiScreen;
        std::shared_ptr<Graphics2D> graphics2d;
};

#endif // DEFAULTSTATE_H
