#ifndef LOGBOT_H
#define LOGBOT_H

#include <string>
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <thread>

#include "StringUtils.h"

struct LogType {
private:
    std::string value;
public:
    LogType(std::string value) : value(value) {}
    std::string asText() { return value; }
};

// Global Log Type Instances
inline LogType LOG_INFO("Info"), LOG_WARN("Warning"), LOG_ERRO("ERROR"), LOG_FATL("FATAL ERROR"), LOG_UNKN("Unknown");

class Logbot {
private:
    static thread_local int activeDepth;
    static std::mutex logMutex;
    std::string lastFormattedValue;
    std::string loggingName;

    // RAII Guard to handle depth logic automatically
    struct DepthGuard {
        DepthGuard() { ++activeDepth; }
        ~DepthGuard() { --activeDepth; }
    };

    bool isTopLevelCall() {
        return activeDepth == 1;
    }

    std::string getCurrentTimeString() {
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        
        std::tm time_struct;
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&time_struct, &t);
#else
        localtime_r(&t, &time_struct);
#endif

        std::ostringstream oss;
        oss << std::put_time(&time_struct, "%m-%d-%Y %H:%M:%S");
        oss << ":" << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    void internalPrint(const std::string& msg ,bool override) {
        if (isTopLevelCall() || override) {
            std::lock_guard<std::mutex> lock(logMutex);
            std::cout << msg << std::endl;
        }
    }

    void internalPrint(const std::string& msg){
        internalPrint(msg, false);
    }

public:
    Logbot(std::string name) : loggingName(name) {}

    template<typename... Args>
    std::string LogBasic(std::string message, Args... args) {
        DepthGuard guard;
        // Double format logic kept from original: inner adds name, outer handles variadic args
        lastFormattedValue = StringUtils::Format(StringUtils::Format("[Log: %s] %s", loggingName.c_str(), message.c_str()), args...);
        
        internalPrint(lastFormattedValue);
        return lastFormattedValue;
    }

    template<typename... Args>
    std::string Log(LogType type, std::string message, Args... args) {
        DepthGuard guard;
        std::string header = StringUtils::Format("[%s] %s", type.asText().c_str(), message.c_str());
        
        // This will increment depth to 2, so LogBasic's internal print will be skipped
        lastFormattedValue = LogBasic(header, args...);
        
        internalPrint(lastFormattedValue);
        return lastFormattedValue;
    }

    template<typename... Args>
    std::string LogVerbose(LogType type, std::string message, Args... args) {
        DepthGuard guard;
        std::string timedMessage = StringUtils::Format("[%s] %s", getCurrentTimeString().c_str(), message.c_str());
        
        // This will increment depth to 2+, skipping lower-level prints
        lastFormattedValue = Log(type, timedMessage, args...);
        
        internalPrint(lastFormattedValue);
        return lastFormattedValue;
    }

    void Break(){
        internalPrint("", true);
    }

    void Repeat(std::string c, int length){
        std::string lineString;
        for(int i = 0; i < length; i++){
            lineString.append(c);
        }
        internalPrint(lineString, true);
    }

    static Logbot CreateInstance(std::string loggerName) {
        return Logbot(loggerName);
    }
};

// Definitions for static members
inline thread_local int Logbot::activeDepth = 0;
inline std::mutex Logbot::logMutex;
inline Logbot LogBot("DefaultLogger");

#endif // LOGBOT_H