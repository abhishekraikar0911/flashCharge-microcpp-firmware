/**
 * @file ILogger.h
 * @brief HAL interface for system logging output
 * @layer HAL — MCU peripheral abstraction
 *
 * Implementations: Esp32UartLogger (wraps Serial, Serial1, etc.)
 * Replaces direct Serial.print / printf calls throughout the application.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include <stdint.h>
// Note: LOG_ convenience macros are in "hal/log_macros.h" — include that for macro access.

class ILogger {
public:
    enum class Level : uint8_t {
        DEBUG = 0,
        INFO,
        WARN,
        ERROR,
        NONE
    };

    virtual ~ILogger() = default;

    /** Initialize the logging peripheral (e.g. Serial.begin) */
    virtual void init(uint32_t baud) = 0;

    /** Log a string message */
    virtual void log(Level level, const char* tag, const char* message) = 0;

    /** Log a formatted message (printf style) */
    virtual void logf(Level level, const char* tag, const char* fmt, ...) = 0;

    /** Set the minimum log level to output */
    virtual void setLevel(Level level) = 0;

    /** Get the current minimum log level */
    virtual Level getLevel() = 0;
};
