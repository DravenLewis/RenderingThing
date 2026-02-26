#ifndef IMGUI_LAYER_H
#define IMGUI_LAYER_H

#include <SDL3/SDL.h>

class RenderWindow;

class ImGuiLayer {
    public:
        static void Init(RenderWindow* window);
        static void Shutdown();
        static void BeginFrame();
        static void EndFrame();
        static void ProcessEvent(const SDL_Event& event);
        static void SetInputEnabled(bool enabled);
        static bool IsInitialized();
};

#endif // IMGUI_LAYER_H
