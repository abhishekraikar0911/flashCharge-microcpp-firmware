#include "hal/esp32/Esp32UartLogger.h"
#include "system/SafeSerial.h"
#include <Arduino.h>
#include <stdarg.h>

// NOTE: We intentionally use SafeSerial's recursive mutex here instead of a
// private mutex. This ensures the logger and every direct Serial.printf() call
// in the codebase contend on the SAME lock, eliminating all interleaving.

Esp32UartLogger::Esp32UartLogger(Level defaultLevel) : currentLevel(defaultLevel) {
    // _logMutex kept for ABI compatibility but unused — SafeSerial mutex is used instead
    _logMutex = nullptr;
}

void Esp32UartLogger::init(uint32_t baud) {
    Serial.begin(baud);
    delay(10);
}

void Esp32UartLogger::log(Level level, const char* tag, const char* message) {
    if (level < currentLevel || level == Level::NONE) return;
    if (SafeSerial::isSuppressed()) return;  // ← silent during provisioning wizard

    char buf[300];
    snprintf(buf, sizeof(buf), "[%s] [%s] %s\n", levelToString(level), tag, message);

    if (SafeSerial::lock(100)) {
        Serial.print(buf);
        SafeSerial::unlock();
    }
}

void Esp32UartLogger::logf(Level level, const char* tag, const char* fmt, ...) {
    if (level < currentLevel || level == Level::NONE) return;
    if (SafeSerial::isSuppressed()) return;  // ← silent during provisioning wizard

    char msgBuf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    char lineBuf[300];
    snprintf(lineBuf, sizeof(lineBuf), "[%s] [%s] %s\n", levelToString(level), tag, msgBuf);

    if (SafeSerial::lock(100)) {
        Serial.print(lineBuf);
        SafeSerial::unlock();
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
