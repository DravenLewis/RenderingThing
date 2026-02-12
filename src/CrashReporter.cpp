#include "CrashReporter.h"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif

#include <imgui.h>
#include <csignal>
#include <cstdlib>

namespace {
    std::atomic<bool> g_crashed{false};
    CrashReporter::Report g_report;
    std::mutex g_reportMutex;

    std::string ThreadNameFromId(unsigned long id){
        return "Thread_" + std::to_string(id);
    }

#ifdef _WIN32
    void CaptureStack(std::vector<CrashReporter::StackFrame>& outFrames){
        constexpr int kMaxFrames = 64;
        void* frames[kMaxFrames] = {};
        USHORT count = CaptureStackBackTrace(0, kMaxFrames, frames, nullptr);

        HANDLE process = GetCurrentProcess();
        SymInitialize(process, NULL, TRUE);

        for(USHORT i = 0; i < count; ++i){
            DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
            char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
            SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;

            CrashReporter::StackFrame frame;
            frame.address = frames[i];
            if(SymFromAddr(process, address, 0, symbol)){
                frame.symbol = symbol->Name;
            }else{
                frame.symbol = "Unknown";
            }
            outFrames.push_back(std::move(frame));
        }
    }

    LONG WINAPI CrashFilter(EXCEPTION_POINTERS*){
        CrashReporter::ReportCrash("Unhandled SEH exception");
        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif

    void SignalHandler(int){
        CrashReporter::ReportCrash("Unhandled signal");
    }
}

void CrashReporter::Install(){
#ifdef _WIN32
    SetUnhandledExceptionFilter(CrashFilter);
#endif
    std::signal(SIGABRT, SignalHandler);
    std::signal(SIGSEGV, SignalHandler);
}

void CrashReporter::ReportCrash(const std::string& reason){
    bool expected = false;
    if(!g_crashed.compare_exchange_strong(expected, true)){
        return;
    }

    std::lock_guard<std::mutex> lock(g_reportMutex);
    g_report = Report{};
    g_report.reason = reason;
#ifdef _WIN32
    g_report.threadId = GetCurrentThreadId();
    g_report.threadName = ThreadNameFromId(g_report.threadId);
    CaptureStack(g_report.stack);
#else
    g_report.threadId = 0;
    g_report.threadName = "UnknownThread";
#endif
}

bool CrashReporter::IsCrashed(){
    return g_crashed.load();
}

CrashReporter::Report CrashReporter::GetReport(){
    std::lock_guard<std::mutex> lock(g_reportMutex);
    return g_report;
}

void CrashReporter::RenderImGui(){
    if(!IsCrashed()) return;

    ImGui::OpenPopup("Crash Report");
    if(ImGui::BeginPopupModal("Crash Report", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        auto report = GetReport();
        ImGui::TextUnformatted("A crash was detected.");
        ImGui::Separator();
        ImGui::Text("Reason: %s", report.reason.c_str());
        ImGui::Text("Thread: %s (%lu)", report.threadName.c_str(), report.threadId);

        ImGui::Separator();
        ImGui::TextUnformatted("Stack Trace:");
        ImGui::BeginChild("CrashStack", ImVec2(500, 240), true);
        for(const auto& frame : report.stack){
            ImGui::Text("%p  %s", frame.address, frame.symbol.c_str());
        }
        ImGui::EndChild();

        if(ImGui::Button("Close Application")){
#ifdef _WIN32
            TerminateProcess(GetCurrentProcess(), 1);
#else
            std::exit(1);
#endif
        }
        ImGui::EndPopup();
    }
}
