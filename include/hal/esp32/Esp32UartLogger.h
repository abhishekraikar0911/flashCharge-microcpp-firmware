/**
 * @file Esp32UartLogger.h
 * @brief ESP32 implementation for ILogger interface using hardware UART
 * @layer HAL
 *
 * Wraps Arduino Serial prints.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "hal/interfaces/ILogger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Esp32UartLogger : public ILogger {
public:
    /**
     * @param defaultLevel  The minimum log level to output at boot
     */
    Esp32UartLogger(Level defaultLevel = Level::INFO);
    virtual ~Esp32UartLogger() = default;

    void init(uint32_t baud) override;
    void log(Level level, const char* tag, const char* message) override;
    void logf(Level level, const char* tag, const char* fmt, ...) override;
    
    void  setLevel(Level level) override;
    Level getLevel() override;

private:
    Level currentLevel;
    SemaphoreHandle_t _logMutex;
    const char* levelToString(Level level);
};
