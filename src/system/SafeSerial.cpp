#include "system/SafeSerial.h"

namespace SafeSerial {

    static bool _suppressed = false;
    static SemaphoreHandle_t _mutex = nullptr;

    bool& isSuppressed() {
        return _suppressed;
    }

    void setSuppressed(bool val) {
        _suppressed = val;
    }

    SemaphoreHandle_t& getMutex() {
        if (_mutex == nullptr) {
            _mutex = xSemaphoreCreateRecursiveMutex();
        }
        return _mutex;
    }

    bool lock(uint32_t timeoutMs) {
        return xSemaphoreTakeRecursive(getMutex(), pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }

    void unlock() {
        xSemaphoreGiveRecursive(getMutex());
    }

    void printf(const char* format, ...) {
        if (isSuppressed()) return;
        if (!lock()) return;
        char buf[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        Serial.print(buf);
        unlock();
    }

    void println(const char* msg) {
        if (isSuppressed()) return;
        if (!lock()) return;
        Serial.println(msg);
        unlock();
    }

    SerialWrapper SafeSerialObj;

} // namespace SafeSerial

