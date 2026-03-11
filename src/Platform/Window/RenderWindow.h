/**
 * @file src/Platform/Window/RenderWindow.h
 * @brief Declarations for RenderWindow.
 */

#ifndef RENDERWINDOW_H
#define RENDERWINDOW_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <glad/glad.h>
#include <string>
#include <vector>
#include <functional>

/// @brief Enumerates values for GLProfile.
enum GLProfile{
    CORE = SDL_GL_CONTEXT_PROFILE_CORE,
    GLES = SDL_GL_CONTEXT_PROFILE_ES,
    LEGACY = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY
};

/// @brief Enumerates values for VSyncMode.
enum class VSyncMode{
    Off = 0,
    On = 1,
    Adaptive = -1
};

/// @brief Holds data for GLVersionInfo.
struct GLVersionInfo{
    int glMajor;
    int glMinor;
    GLProfile profile;

    /**
     * @brief Constructs a new GLVersionInfo instance.
     * @param glMajorVersion Value for gl major version.
     * @param glMinorVerion Value for gl minor verion.
     * @param profile Filesystem path for profile.
     */
    GLVersionInfo(int glMajorVersion = 4, int glMinorVerion = 0, GLProfile profile = GLProfile::CORE){
        this->glMajor = glMajorVersion;
        this->glMinor = glMinorVerion;
        this->profile = profile;
    }
};

/// @brief Holds data for Display.
struct Display{
    /**
     * @brief Returns the display count.
     * @return Computed numeric result.
     */
    static int getDisplayCount(){
        int count;
        SDL_GetDisplays(&count);
        return count;
    }

    /**
     * @brief Returns a display id by index.
     * @param id Identifier or index value.
     * @return SDL display id for the requested index.
     */
    static SDL_DisplayID getDisplay(int id){
        if(id >= 0 && id < getDisplayCount()){
            SDL_DisplayID* displays = SDL_GetDisplays(NULL);
            SDL_free(displays);
            return displays[id];
        }
    }

    /**
     * @brief Returns the primary display id.
     * @return SDL display id for the primary display.
     */
    static SDL_DisplayID getPrimary(){
        return getDisplay(0);
    }
};

#define DISPLAY_PRIMARY Display::getPrimary()

/// @brief Holds data for DisplayMode.
struct DisplayMode{
    int windowResolutionX;
    int windowResolutionY;
    bool fullScreen;
    bool resizable;
    VSyncMode vSyncMode;
    int bufferSize;
    int depthBitWidth;

    SDL_DisplayID __compatDisplayID = 0;
    SDL_PixelFormat __compatPixelFormat = SDL_PIXELFORMAT_RGBA8888;
    SDL_DisplayModeData* __compatInternal = nullptr;
    float __compatPixelDensity = 0;
    float __compatRefreshRate = 0;
    int  __refreshRateDenom = 0;
    int __refreshRateNumer = 0;

    GLVersionInfo versionInfo;
    typedef std::vector<DisplayMode> DisplayModes;

    static const int FIRST = 0;

    /**
     * @brief Creates a display mode configuration object.
     * @param width Initial window width.
     * @param height Initial window height.
     * @param glInfo OpenGL context configuration.
     * @param fs True to start in fullscreen mode.
     * @param rs True to allow window resizing.
     * @param bs Buffer count hint.
     * @param dbw Depth buffer bit width.
     * @param vSync Initial VSync mode.
     * @return Configured display mode value.
     */
    static DisplayMode New(int width = 640,
                           int height = 480,
                           GLVersionInfo glInfo = GLVersionInfo(),
                           bool fs = false,
                           bool rs = false,
                           int bs = 1,
                           int dbw = 24,
                           VSyncMode vSync = VSyncMode::Off){
        DisplayMode mode;
        mode.windowResolutionX = width;
        mode.windowResolutionY = height;
        mode.versionInfo = glInfo;
        mode.fullScreen = fs;
        mode.resizable = rs;
        mode.vSyncMode = vSync;
        mode.bufferSize = bs;
        mode.depthBitWidth = dbw;
        return mode;
    }

    /**
     * @brief Enumerates available fullscreen display modes for a display.
     * @param display Target display id.
     * @return Available display mode list.
     */
    static DisplayMode::DisplayModes GetAvailableFullscreenDisplayModes(SDL_DisplayID display = DISPLAY_PRIMARY){
        DisplayModes availableModes;
        int displayCount;

        SDL_DisplayMode** modeList = SDL_GetFullscreenDisplayModes(display,&displayCount);

        if(modeList){
            availableModes.reserve(displayCount);

            for(int i = 0; i < displayCount; i++){
                const SDL_DisplayMode* rawMode = modeList[i];

                DisplayMode dMode = DisplayMode::New(rawMode->w, rawMode->h);

                dMode.__compatDisplayID = rawMode->displayID;
                dMode.__compatPixelFormat = rawMode->format;
                dMode.__compatInternal = rawMode->internal;
                dMode.__compatPixelDensity = rawMode->pixel_density;
                dMode.__compatRefreshRate = rawMode->refresh_rate;
                dMode.__refreshRateDenom = rawMode->refresh_rate_denominator;
                dMode.__refreshRateNumer = rawMode->refresh_rate_numerator;
                availableModes.push_back(dMode);
            }

            SDL_free((void*) modeList);
        }

        return availableModes;
    }

    /**
     * @brief Converts an engine `DisplayMode` to `SDL_DisplayMode`.
     * @param mode Engine display mode to convert.
     * @return Converted SDL display mode value.
     */
    static SDL_DisplayMode ToSDLDisplayMode(DisplayMode& mode){
        SDL_DisplayMode smode;
        smode.displayID = mode.__compatDisplayID;
        smode.format = mode.__compatPixelFormat;
        smode.w = mode.windowResolutionX;
        smode.h = mode.windowResolutionY;
        smode.internal = std::move(mode.__compatInternal);
        smode.pixel_density = mode.__compatPixelDensity;
        smode.refresh_rate = mode.__compatRefreshRate;
        smode.refresh_rate_denominator = mode.__refreshRateDenom;
        smode.refresh_rate_numerator = mode.__refreshRateNumer;

        return smode;
    }
    
};

/// @brief Represents the RenderWindow type.
class RenderWindow{
    private:
        std::string windowName;
        DisplayMode displayMode;
        int windowWidth;
        int windowHeight;
        bool doClose = false;
        /**
         * @brief Initializes SDL windowing and OpenGL context state.
         * @return True when the operation succeeds; otherwise false.
         */
        bool initSDL();
        bool isDisposed = false;
        SDL_Window* windowPtr;
        SDL_GLContext glContext;
        DisplayMode lastDisplayMode;
        SDL_Event event;
        /**
         * @brief Constructs a new RenderWindow instance.
         */
        RenderWindow() = delete;
        std::vector<std::function<void(SDL_Event&)>> windowEventHandlers;
    public:
        /**
         * @brief Constructs a new RenderWindow instance.
         * @param title Initial window title.
         * @param mode Window/display configuration.
         */
        RenderWindow(std::string title, DisplayMode mode = DisplayMode::New());
        /**
         * @brief Destroys this RenderWindow instance.
         */
        ~RenderWindow();
        /**
         * @brief Returns whether the window close flag is set.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool isCloseRequested();
        /**
         * @brief Marks the window for shutdown.
         */
        void close();
        /**
         * @brief Releases SDL/OpenGL resources owned by this window.
         */
        void dispose();
        /**
         * @brief Swaps front/back buffers.
         */
        void swap();
        /**
         * @brief Resizes the window.
         * @param width New window width.
         * @param height New window height.
         * @return True when the operation succeeds; otherwise false.
         */
        bool setWindowSize(int width, int height);
        /**
         * @brief Returns the current window width.
         * @return Computed numeric result.
         */
        int  getWindowWidth();
        /**
         * @brief Returns the current window height.
         * @return Computed numeric result.
         */
        int  getWindowHeight();
        /**
         * @brief Sets SDL swap interval mode.
         * @param mode Target VSync mode.
         * @return True when the operation succeeds; otherwise false.
         */
        bool setVSyncMode(VSyncMode mode);
        /**
         * @brief Returns the currently configured VSync mode.
         * @return Current VSync mode.
         */
        VSyncMode getVSyncMode() const;
        /**
         * @brief Enables or disables standard VSync mode.
         * @param enabled True to enable VSync; false to disable.
         * @return True when the operation succeeds; otherwise false.
         */
        bool setVSyncEnabled(bool enabled);
        /**
         * @brief Returns whether VSync is enabled.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool isVSyncEnabled() const;
        /**
         * @brief Enables or disables fullscreen mode.
         * @param fullscreen True for fullscreen mode.
         * @return True when the operation succeeds; otherwise false.
         */
        bool setFullScreen(bool fullscreen);
        /**
         * @brief Returns whether fullscreen mode is active.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool isFullScreen();
        /**
         * @brief Updates the window title.
         * @param windowName New window title.
         */
        void setWindowName(std::string windowName);
        /**
         * @brief Toggles fullscreen.
         */
        void toggleFullscreen();
        /**
         * @brief Enables or disables window resizing.
         * @param resize True to allow resize.
         */
        void setResizable(bool resize);
        /**
         * @brief Returns whether resizing is enabled.
         * @return True when the operation succeeds; otherwise false.
         */
        bool getResizable();
        /**
         * @brief Polls and dispatches SDL window/input events.
         */
        void process();
        /**
         * @brief Adds window event handler.
         * @param fn Callback invoked for each processed SDL event.
         */
        void addWindowEventHandler(std::function<void(SDL_Event&)> fn);
        /**
         * @brief Returns the window name.
         * @return Resulting string value.
         */
        std::string getWindowName();
        /**
         * @brief Returns mutable display mode settings.
         * @return Reference to the current display mode object.
         */
        DisplayMode& getDisplayMode();
        /**
         * @brief Returns the last processed SDL event.
         * @return Reference to the cached SDL event.
         */
        SDL_Event& getEvent();
        /**
         * @brief Returns the native SDL window handle.
         * @return SDL window pointer.
         */
        SDL_Window* getWindowPtr();
        /**
         * @brief Returns the OpenGL context handle.
         * @return SDL OpenGL context.
         */
        SDL_GLContext getGLContext();

};

#endif //RENDERWINDOW_H
