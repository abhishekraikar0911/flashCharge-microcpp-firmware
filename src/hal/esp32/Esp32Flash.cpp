#include "hal/esp32/Esp32Flash.h"

Esp32Flash::Esp32Flash() : isOpen(false) {}

bool Esp32Flash::open(const char* ns) {
    if (isOpen) {
        prefs.end();
    }
    // false = read/write mode
    isOpen = prefs.begin(ns, false);
    return isOpen;
}

void Esp32Flash::close() {
    if (isOpen) {
        prefs.end();
        isOpen = false;
    }
}

bool Esp32Flash::putString(const char* key, const char* value) {
    if (!isOpen) return false;
    return prefs.putString(key, value) > 0;
}

bool Esp32Flash::getString(const char* key, char* buf, size_t len) {
    if (!isOpen) {
        if (len > 0) buf[0] = '\0';
        return false;
    }
    // Preferences returns length of written data (0 if missing/error)
    size_t result = prefs.getString(key, buf, len);
    if (result == 0 && len > 0) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool Esp32Flash::putInt(const char* key, int value) {
    if (!isOpen) return false;
    return prefs.putInt(key, value) > 0;
}

int Esp32Flash::getInt(const char* key, int defaultVal) {
    if (!isOpen) return defaultVal;
    // Returns defaultVal if key is not found
    return prefs.getInt(key, defaultVal);
}

bool Esp32Flash::putBool(const char* key, bool value) {
    if (!isOpen) return false;
    return prefs.putBool(key, value) > 0;
}

bool Esp32Flash::getBool(const char* key, bool defaultVal) {
    if (!isOpen) return defaultVal;
    return prefs.getBool(key, defaultVal);
}

bool Esp32Flash::putFloat(const char* key, float value) {
    if (!isOpen) return false;
    return prefs.putFloat(key, value) > 0;
}

float Esp32Flash::getFloat(const char* key, float defaultVal) {
    if (!isOpen) return defaultVal;
    return prefs.getFloat(key, defaultVal);
}

bool Esp32Flash::clear() {
    if (!isOpen) return false;
    return prefs.clear();
}
