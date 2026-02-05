
#include "RenderWindow.h"

/* // 2. Set OpenGL Attributes (Version 4.1 Core)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
   
    window = SDL_CreateWindow("Modern OpenGL 4 + SDL3 Cube", 800, 600, SDL_WINDOW_OPENGL);
    glContext = SDL_GL_CreateContext(window);
    
    // NOTE: Initialize your extension loader here (e.g., gladLoadGLLoader)
    
    gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
    glEnable(GL_DEPTH_TEST);*/

RenderWindow::RenderWindow(std::string title, DisplayMode mode){
    this->windowName = title;
    this->displayMode = mode;
    this->initSDL();
}

RenderWindow::~RenderWindow(){
    this->dispose();
}

bool RenderWindow::initSDL(){

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, this->displayMode.versionInfo.glMajor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, this->displayMode.versionInfo.glMinor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  this->displayMode.versionInfo.profile);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, this->displayMode.bufferSize);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, this->displayMode.depthBitWidth);

    this->windowPtr = SDL_CreateWindow(this->windowName.c_str(), this->displayMode.windowResolutionX, this->displayMode.windowResolutionY, SDL_WINDOW_OPENGL);
    this->glContext = SDL_GL_CreateContext(this->windowPtr);

    this->windowWidth = this->displayMode.windowResolutionX;
    this->windowHeight = this->displayMode.windowResolutionY;

    
    this->setFullScreen(this->displayMode.fullScreen);
    this->setResizable(this->displayMode.resizable);

    gladLoadGLLoader((GLADloadproc) SDL_GL_GetProcAddress);
    
    glEnable(GL_DEPTH_TEST);

    return true;
}

bool RenderWindow::isCloseRequested(){
    return this->doClose;
}

void RenderWindow::close(){
    this->doClose = true;
};

void RenderWindow::dispose(){
    if(this->isDisposed) return;
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(this->windowPtr);
    SDL_Quit();
    this->isDisposed = true;
}

void RenderWindow::swap(){
    SDL_GL_SwapWindow(this->windowPtr);
    glFinish(); // Maybe a fix.
}

bool RenderWindow::setWindowSize(int width, int height){
    if(SDL_SetWindowSize(this->windowPtr,width,height)){
        this->windowWidth = width;
        this->windowHeight = height;
        return true;
    }
    return false;
}

int  RenderWindow::getWindowWidth(){
    return this->windowWidth;
}

int  RenderWindow::getWindowHeight(){
    return this->windowHeight;
}

bool RenderWindow::setFullScreen(bool fullscreen){
    // 1. Determine flags
    Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN : 0;

    // 2. Attempt 1: Try setting state with current settings
    if (SDL_SetWindowFullscreen(this->windowPtr, flags)) {
        this->displayMode.fullScreen = fullscreen;
        return true;
    }

    // If disabling fullscreen failed, there is no valid fallback.
    if (!fullscreen) {
        return false;
    }

    // 3. Attempt 2: Fallback Logic
    
    // FIX A: Get the specific display the window is currently on.
    // Don't assume Primary (which defaults to 0 or 1 depending on internal SDL logic).
    SDL_DisplayID currentDisplayID = SDL_GetDisplayForWindow(this->windowPtr);
    
    // Fetch modes for that specific display
    auto availableModes = DisplayMode::GetAvailableFullscreenDisplayModes(currentDisplayID);

    // FIX B: Check if the vector is empty to prevent a crash
    if (availableModes.empty()) {
        return false;
    }

    // Now it is safe to access the first element
    auto candidateMode = availableModes[DisplayMode::FIRST];
    SDL_DisplayMode sdlMode = DisplayMode::ToSDLDisplayMode(candidateMode);

    if (SDL_SetWindowFullscreenMode(this->windowPtr, &sdlMode) == 0) {
        // Try the toggle one last time with the new mode applied
        if (SDL_SetWindowFullscreen(this->windowPtr, flags) == 0) {
            // SUCCESS: Now safely update the member variables
            this->displayMode = candidateMode;
            this->displayMode.fullScreen = true;
            return true;
        }
    }

    // 4. Failure: No state was corrupted.
    return false;
}

bool RenderWindow::isFullScreen(){
    return this->displayMode.fullScreen;
}

void RenderWindow::setWindowName(std::string windowName){
    if(SDL_SetWindowTitle(this->windowPtr, windowName.c_str())){
        this->windowName = windowName;
    }
}

std::string RenderWindow::getWindowName(){
    return this->windowName;
}

DisplayMode& RenderWindow::getDisplayMode(){
    return this->displayMode;
}

void RenderWindow::toggleFullscreen(){
    this->setFullScreen(!this->isFullScreen());
}

bool RenderWindow::getResizable(){
    return this->displayMode.resizable;
}

void RenderWindow::setResizable(bool resize){
    if(SDL_SetWindowResizable(this->windowPtr, resize)){
        this->displayMode.resizable = resize;
    }
}
//std::function<void(SDL_Event&)> eventHandler

void RenderWindow::process(){
    while (SDL_PollEvent(&event)) {
        if(event.type == SDL_EVENT_WINDOW_RESIZED){
            int w = event.window.data1;
            int h = event.window.data2;
            this->windowWidth = w;
            this->windowHeight = h;
            glViewport(0,0,w,h);
        }

        for(auto eh : this->windowEventHandlers){
            eh(event);
        }
    }
}

SDL_Event& RenderWindow::getEvent(){
    return this->event;
}

void RenderWindow::addWindowEventHandler(std::function<void(SDL_Event&)> fn){
    this->windowEventHandlers.push_back(fn);
}

SDL_Window* RenderWindow::getWindowPtr(){
    return this->windowPtr;
}