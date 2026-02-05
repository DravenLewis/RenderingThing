#ifndef RENDERWINDOW_H
#define RENDERWINDOW_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <glad/glad.h>
#include <string>
#include <vector>
#include <functional>

enum GLProfile{
    CORE = SDL_GL_CONTEXT_PROFILE_CORE,
    GLES = SDL_GL_CONTEXT_PROFILE_ES,
    LEGACY = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY
};

struct GLVersionInfo{
    int glMajor;
    int glMinor;
    GLProfile profile;

    GLVersionInfo(int glMajorVersion = 4, int glMinorVerion = 0, GLProfile profile = GLProfile::CORE){
        this->glMajor = glMajorVersion;
        this->glMinor = glMinorVerion;
        this->profile = profile;
    }
};

struct Display{
    static int getDisplayCount(){
        int count;
        SDL_GetDisplays(&count);
        return count;
    }

    static SDL_DisplayID getDisplay(int id){
        if(id >= 0 && id < getDisplayCount()){
            SDL_DisplayID* displays = SDL_GetDisplays(NULL);
            SDL_free(displays);
            return displays[id];
        }
    }

    static SDL_DisplayID getPrimary(){
        return getDisplay(0);
    }
};

#define DISPLAY_PRIMARY Display::getPrimary()

struct DisplayMode{
    int windowResolutionX;
    int windowResolutionY;
    bool fullScreen;
    bool resizable;
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

    static DisplayMode New(int width = 640, int height = 480, GLVersionInfo glInfo = GLVersionInfo(), bool fs = false, bool rs = false, int bs = 1, int dbw = 24){
        DisplayMode mode;
        mode.windowResolutionX = width;
        mode.windowResolutionY = height;
        mode.versionInfo = glInfo;
        mode.fullScreen = fs;
        mode.resizable = rs;
        mode.bufferSize = bs;
        mode.depthBitWidth = dbw;
        return mode;
    }

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

class RenderWindow{
    private:
        std::string windowName;
        DisplayMode displayMode;
        int windowWidth;
        int windowHeight;
        bool doClose = false;
        bool initSDL();
        bool isDisposed = false;
        SDL_Window* windowPtr;
        SDL_GLContext glContext;
        DisplayMode lastDisplayMode;
        SDL_Event event;
        RenderWindow() = delete;
        std::vector<std::function<void(SDL_Event&)>> windowEventHandlers;
    public:
        RenderWindow(std::string title, DisplayMode mode = DisplayMode::New());
        ~RenderWindow();
        bool isCloseRequested();
        void close();
        void dispose();
        void swap();
        bool setWindowSize(int width, int height);
        int  getWindowWidth();
        int  getWindowHeight();
        bool setFullScreen(bool fullscreen);
        bool isFullScreen();
        void setWindowName(std::string windowName);
        void toggleFullscreen();
        void setResizable(bool resize);
        bool getResizable();
        void process();
        void addWindowEventHandler(std::function<void(SDL_Event&)> fn);
        std::string getWindowName();
        DisplayMode& getDisplayMode();
        SDL_Event& getEvent();
        SDL_Window* getWindowPtr();

};

#endif //RENDERWINDOW_H