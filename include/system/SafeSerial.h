#pragma once

/**
 * @file safe_serial.h
 * @brief Thread-safe Serial wrapper using FreeRTOS mutex
 * 
 * Prevents garbled output when multiple tasks print simultaneously.
 * Uses a recursive mutex so nested calls from the same task won't deadlock.
 */

#include <Arduino.h>
#include <freertos/semphr.h>
#include <stdarg.h>

namespace SafeSerial {

    // ── Provisioning silence flag ──────────────────────────────────────────
    // Set to true during the provisioning wizard so background tasks don't
    // flood the Serial monitor while the user is trying to type.
    bool& isSuppressed();
    void setSuppressed(bool val);
    // ─────────────────────────────────────────────────────────────────────

    SemaphoreHandle_t& getMutex();

    bool lock(uint32_t timeoutMs = 100);
    void unlock();

    // Thread-safe printf — locks mutex for the entire formatted line
    void printf(const char* format, ...) __attribute__((format(printf, 1, 2)));
    
    // Thread-safe println
    void println(const char* msg);

    // Thread-safe print (no newline)
    inline void print(const char* msg) {
        if (isSuppressed()) return;   // ← silence during provisioning wizard
        if (!lock()) return;
        Serial.print(msg);
        unlock();
    }

    class SerialWrapper {
    public:
        template <typename T>
        void print(T val) { if (!isSuppressed()) ::Serial.print(val); }
        template <typename T, typename U>
        void print(T val, U format) { if (!isSuppressed()) ::Serial.print(val, format); }
        
        template <typename T>
        void println(T val) { if (!isSuppressed()) ::Serial.println(val); }
        template <typename T, typename U>
        void println(T val, U format) { if (!isSuppressed()) ::Serial.println(val, format); }
        void println() { if (!isSuppressed()) ::Serial.println(); }
        
        template <typename... Args>
        void printf(const char* format, Args... args) { 
            if (!isSuppressed()) ::Serial.printf(format, args...); 
        }
    };
    
    extern SerialWrapper SafeSerialObj;

} // namespace SafeSerial

