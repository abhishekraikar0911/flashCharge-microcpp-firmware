#pragma once

// Debug levels - only compile enabled logs
#define DEBUG_NONE     0
#define DEBUG_CRITICAL 1  // Only critical errors
#define DEBUG_NORMAL   2  // + Important events
#define DEBUG_VERBOSE  3  // + All debug info

// Set debug level here (change to DEBUG_CRITICAL for production)
#define DEBUG_LEVEL DEBUG_NORMAL

// Conditional logging macros
#if DEBUG_LEVEL >= DEBUG_CRITICAL
    #define LOG_CRIT(...) Serial.printf(__VA_ARGS__)
#else
    #define LOG_CRIT(...)
#endif

#if DEBUG_LEVEL >= DEBUG_NORMAL
    #define LOG_INFO(...) Serial.printf(__VA_ARGS__)
#else
    #define LOG_INFO(...)
#endif

#if DEBUG_LEVEL >= DEBUG_VERBOSE
    #define LOG_DBG(...) Serial.printf(__VA_ARGS__)
#else
    #define LOG_DBG(...)
#endif
