#include "ImGuiLayer.h"

#include "RenderWindow.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

namespace {
    bool g_initialized = false;
    bool g_inputEnabled = true;
    const char* g_glslVersion = "#version 130";
}

void ImGuiLayer::Init(RenderWindow* window){
    if(g_initialized || !window) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(window->getWindowPtr(), window->getGLContext());
    ImGui_ImplOpenGL3_Init(g_glslVersion);

    g_initialized = true;
}

void ImGuiLayer::Shutdown(){
    if(!g_initialized) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    g_initialized = false;
}

void ImGuiLayer::BeginFrame(){
    if(!g_initialized) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if(!g_inputEnabled){
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        io.MouseDown[0] = false;
        io.MouseDown[1] = false;
        io.MouseDown[2] = false;
    }
}

void ImGuiLayer::EndFrame(){
    if(!g_initialized) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiLayer::ProcessEvent(const SDL_Event& event){
    if(!g_initialized) return;
    if(!g_inputEnabled) return;
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiLayer::SetInputEnabled(bool enabled){
    g_inputEnabled = enabled;
}

bool ImGuiLayer::IsInitialized(){
    return g_initialized;
}
