#ifndef VIEW_H
#define VIEW_H

#include <memory>
#include <vector>

#include "Screen.h"
#include "RenderWindow.h"

class View {
    private:
        int width = 0;
        int height = 0;
        std::vector<PScreen> screens;

    public:
        View(int width, int height);

        void resize(int w, int h);

        PScreen getScreen(size_t index) const;
        PScreen getMainScreen() const; // index 0
        PScreen getUIScreen() const;   // index 1

        void addScreen(PScreen screen);
        size_t screenCount() const { return screens.size(); }

        void drawToWindow(RenderWindow* window, bool clearWindow = true);
        void drawToView(RenderWindow* window, float x, float y, float w, float h, bool clearWindow = true);
};

typedef std::shared_ptr<View> PView;

#endif // VIEW_H
