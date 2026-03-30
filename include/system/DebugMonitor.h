#pragma once

#include <Arduino.h>

// ANSI Color Codes for Serial Monitor
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_GRAY    "\033[90m"

// Log Levels
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_CRITICAL = 4
};

class DebugMonitor {
private:
    static LogLevel currentLevel;
    static bool enableColors;
    static bool enableTimestamp;
    static unsigned long startTime;

    static const char* getLevelColor(LogLevel level) {
        if (!enableColors) return "";
        switch (level) {
            case LOG_DEBUG: return COLOR_GRAY;
            case LOG_INFO: return COLOR_CYAN;
            case LOG_WARN: return COLOR_YELLOW;
            case LOG_ERROR: return COLOR_RED;
            case LOG_CRITICAL: return COLOR_MAGENTA;
            default: return COLOR_WHITE;
        }
    }

    static const char* getLevelName(LogLevel level) {
        switch (level) {
            case LOG_DEBUG: return "DEBUG";
            case LOG_INFO: return "INFO ";
            case LOG_WARN: return "WARN ";
            case LOG_ERROR: return "ERROR";
            case LOG_CRITICAL: return "CRIT ";
            default: return "?????";
        }
    }

public:
    static void init(LogLevel level = LOG_INFO, bool colors = true, bool timestamp = true) {
        currentLevel = level;
        enableColors = colors;
        enableTimestamp = timestamp;
        startTime = millis();
    }

    static void setLevel(LogLevel level) { currentLevel = level; }

    template<typename... Args>
    static void log(LogLevel level, const char* tag, const char* format, Args... args) {
        if (level < currentLevel) return;

        Serial.print(getLevelColor(level));
        
        if (enableTimestamp) {
            unsigned long elapsed = millis() - startTime;
            Serial.printf("[%7lu.%03lu] ", elapsed / 1000, elapsed % 1000);
        }
        
        Serial.printf("[%s] [%-8s] ", getLevelName(level), tag);
        Serial.printf(format, args...);
        Serial.print(COLOR_RESET);
        Serial.println();
    }

    static void separator(char c = '=', int length = 80) {
        for (int i = 0; i < length; i++) Serial.print(c);
        Serial.println();
    }

    static void header(const char* title) {
        separator();
        Serial.printf("  %s\n", title);
        separator();
    }
};

// Global instance
extern DebugMonitor Debug;

// Convenience macros
#define LOG_D(tag, ...) Debug.log(LOG_DEBUG, tag, __VA_ARGS__)
#define LOG_I(tag, ...) Debug.log(LOG_INFO, tag, __VA_ARGS__)
#define LOG_W(tag, ...) Debug.log(LOG_WARN, tag, __VA_ARGS__)
#define LOG_E(tag, ...) Debug.log(LOG_ERROR, tag, __VA_ARGS__)
#define LOG_C(tag, ...) Debug.log(LOG_CRITICAL, tag, __VA_ARGS__)
