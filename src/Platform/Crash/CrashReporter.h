#ifndef CRASH_REPORTER_H
#define CRASH_REPORTER_H

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class CrashReporter {
    public:
        struct StackFrame {
            void* address = nullptr;
            std::string symbol;
        };

        struct Report {
            std::string reason;
            std::string threadName;
            unsigned long threadId = 0;
            std::vector<StackFrame> stack;
        };

        static void Install();
        static void ReportCrash(const std::string& reason);
        static bool IsCrashed();
        static Report GetReport();
        static void RenderImGui();
};

#endif // CRASH_REPORTER_H
