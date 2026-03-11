/**
 * @file src/Rendering/Core/View.h
 * @brief Declarations for View.
 */

#ifndef VIEW_H
#define VIEW_H

#include <memory>
#include <vector>

#include "Rendering/Core/Screen.h"
#include "Platform/Window/RenderWindow.h"

/// @brief Represents the View type.
class View : public std::enable_shared_from_this<View> {
    private:
        int width = 0;
        int height = 0;
        RenderWindow* window = nullptr;
        std::vector<PScreen> screens;

    public:
        /**
         * @brief Constructs a new View instance.
         */
        View();
        /**
         * @brief Constructs a new View instance.
         * @param window Value for window.
         */
        View(RenderWindow* window);
        /**
         * @brief Destroys this View instance.
         */
        virtual ~View() = default;

        /**
         * @brief Attaches a render window to this view.
         * @param window Value for window.
         */
        void attachWindow(RenderWindow* window);
        /**
         * @brief Checks whether window.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool hasWindow() const { return window != nullptr; }

        void resize(int w, int h);

        PScreen getScreen(size_t index) const;
        PScreen getMainScreen() const; // index 0
        PScreen getUIScreen() const;   // index 1
        RenderWindow* getWindow() const { return window; }
        int getWidth() const { return width; }
        int getHeight() const { return height; }

        void addScreen(PScreen screen);
        size_t screenCount() const { return screens.size(); }

        virtual void drawToWindow(bool clearWindow = true, float x = -1, float y = -1, float w = -1, float h = -1);

        Math3D::Vec3 screenToWorld(PCamera cam, float x, float y, float depth = 0.0f,
                                   float viewportX = 0.0f, float viewportY = 0.0f,
                                   float viewportW = -1.0f, float viewportH = -1.0f) const;
        Math3D::Vec3 worldToScreen(PCamera cam, const Math3D::Vec3& world,
                                   float viewportX = 0.0f, float viewportY = 0.0f,
                                   float viewportW = -1.0f, float viewportH = -1.0f) const;
};

typedef std::shared_ptr<View> PView;

#endif // VIEW_H
