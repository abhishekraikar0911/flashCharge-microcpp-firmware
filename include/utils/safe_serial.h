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

    inline SemaphoreHandle_t& getMutex() {
        static SemaphoreHandle_t _mutex = xSemaphoreCreateRecursiveMutex();
        return _mutex;
    }

    inline bool lock(uint32_t timeoutMs = 100) {
        return xSemaphoreTakeRecursive(getMutex(), pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }

    inline void unlock() {
        xSemaphoreGiveRecursive(getMutex());
    }

    // Thread-safe printf — locks mutex for the entire formatted line
    inline void printf(const char* format, ...) __attribute__((format(printf, 1, 2)));
    inline void printf(const char* format, ...) {
        if (!lock()) return;
        char buf[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        Serial.print(buf);
        unlock();
    }

    // Thread-safe println
    inline void println(const char* msg) {
        if (!lock()) return;
        Serial.println(msg);
        unlock();
    }

    // Thread-safe print (no newline)
    inline void print(const char* msg) {
        if (!lock()) return;
        Serial.print(msg);
        unlock();
    }

} // namespace SafeSerial
