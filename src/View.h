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

        void drawToWindow(bool clearWindow = true, float x = -1, float y = -1, float w = -1, float h = -1);
};

typedef std::shared_ptr<View> PView;

#endif // VIEW_H
