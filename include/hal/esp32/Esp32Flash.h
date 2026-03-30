/**
 * @file Esp32Flash.h
 * @brief ESP32 implementation for IFlash interface using NVS
 * @layer HAL
 *
 * Wraps Arduino Preferences.h to provide non-volatile storage.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "hal/interfaces/IFlash.h"
#include <Preferences.h>

class Esp32Flash : public IFlash {
public:
    Esp32Flash();
    virtual ~Esp32Flash() = default;

    bool open(const char* ns) override;
    void close() override;
    bool putString(const char* key, const char* value) override;
    bool getString(const char* key, char* buf, size_t len) override;
    bool putInt(const char* key, int value) override;
    int  getInt(const char* key, int defaultVal = 0) override;
    bool putBool(const char* key, bool value) override;
    bool getBool(const char* key, bool defaultVal = false) override;
    bool putFloat(const char* key, float value) override;
    float getFloat(const char* key, float defaultVal = 0.0f) override;
    bool clear() override;

private:
    Preferences prefs;
    bool isOpen;
};
