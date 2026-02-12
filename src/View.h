#ifndef VIEW_H
#define VIEW_H

#include <memory>
#include <vector>

#include "Screen.h"
#include "RenderWindow.h"

class View : public std::enable_shared_from_this<View> {
    private:
        int width = 0;
        int height = 0;
        RenderWindow* window = nullptr;
        std::vector<PScreen> screens;

    public:
        View();
        View(RenderWindow* window);
        virtual ~View() = default;

        void attachWindow(RenderWindow* window);
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
