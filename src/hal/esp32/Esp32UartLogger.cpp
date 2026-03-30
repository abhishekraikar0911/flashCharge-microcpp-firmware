#include "hal/esp32/Esp32UartLogger.h"
#include <Arduino.h>
#include <stdarg.h>

Esp32UartLogger::Esp32UartLogger(Level defaultLevel) : currentLevel(defaultLevel) {
    _logMutex = xSemaphoreCreateMutex();
}

void Esp32UartLogger::init(uint32_t baud) {
    Serial.begin(baud);
    // Small delay to allow USB/UART bridging to stabilize
    delay(10);
}

void Esp32UartLogger::log(Level level, const char* tag, const char* message) {
    if (level < currentLevel || level == Level::NONE) return;

    if (xSemaphoreTake(_logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Serial.printf("[%s] [%s] %s\n", levelToString(level), tag, message);
        xSemaphoreGive(_logMutex);
    }
}

void Esp32UartLogger::logf(Level level, const char* tag, const char* fmt, ...) {
    if (level < currentLevel || level == Level::NONE) return;

    if (xSemaphoreTake(_logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Serial.printf("[%s] [%s] ", levelToString(level), tag);
        
        va_list args;
        va_start(args, fmt);
        char buf[256];
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        
        Serial.print(buf);
        Serial.println();
        xSemaphoreGive(_logMutex);
    }
}

void Esp32UartLogger::setLevel(Level level) {
    currentLevel = level;
}

ILogger::Level Esp32UartLogger::getLevel() {
    return currentLevel;
}

const char* Esp32UartLogger::levelToString(Level level) {
    switch (level) {
        case Level::DEBUG: return "DBG";
        case Level::INFO:  return "INF";
        case Level::WARN:  return "WRN";
        case Level::ERROR: return "ERR";
        default:           return "UNK";
    }
}
