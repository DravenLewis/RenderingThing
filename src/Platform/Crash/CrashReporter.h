/**
 * @file src/Platform/Crash/CrashReporter.h
 * @brief Declarations for CrashReporter.
 */

#ifndef CRASH_REPORTER_H
#define CRASH_REPORTER_H

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

/// @brief Represents the CrashReporter type.
class CrashReporter {
    public:
        /// @brief Holds data for StackFrame.
        struct StackFrame {
            void* address = nullptr;
            std::string symbol;
        };

        /// @brief Holds data for Report.
        struct Report {
            std::string reason;
            std::string threadName;
            unsigned long threadId = 0;
            std::vector<StackFrame> stack;
        };

        /**
         * @brief Installs crash handlers.
         */
        static void Install();
        /**
         * @brief Builds and writes a crash report.
         * @param reason Value for reason.
         */
        static void ReportCrash(const std::string& reason);
        /**
         * @brief Checks whether crashed.
         * @return True when the condition is satisfied; otherwise false.
         */
        static bool IsCrashed();
        /**
         * @brief Returns the report.
         * @return Result of this operation.
         */
        static Report GetReport();
        /**
         * @brief Renders im gui.
         */
        static void RenderImGui();
};

#endif // CRASH_REPORTER_H
