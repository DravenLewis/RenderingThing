/**
 * @file src/Editor/Core/ImGuiLayer.h
 * @brief Declarations for ImGuiLayer.
 */

#ifndef IMGUI_LAYER_H
#define IMGUI_LAYER_H

#include <SDL3/SDL.h>

class RenderWindow;

/// @brief Represents the ImGuiLayer type.
class ImGuiLayer {
    public:
        /**
         * @brief Initializes this object.
         * @param window Value for window.
         */
        static void Init(RenderWindow* window);
        /**
         * @brief Shuts down this subsystem.
         */
        static void Shutdown();
        /**
         * @brief Begins frame.
         */
        static void BeginFrame();
        /**
         * @brief Ends the current frame.
         */
        static void EndFrame();
        /**
         * @brief Processes one SDL event.
         * @param event Value for event.
         */
        static void ProcessEvent(const SDL_Event& event);
        /**
         * @brief Sets the input enabled.
         * @param enabled Flag controlling enabled.
         */
        static void SetInputEnabled(bool enabled);
        /**
         * @brief Checks whether this object is initialized.
         * @return True when the condition is satisfied; otherwise false.
         */
        static bool IsInitialized();
};

#endif // IMGUI_LAYER_H
