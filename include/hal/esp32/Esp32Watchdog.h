/**
 * @file Esp32Watchdog.h
 * @brief ESP32 implementation for IWatchdog interface
 * @layer HAL
 *
 * Wraps the ESP-IDF Task Watchdog Timer (TWDT).
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "hal/interfaces/IWatchdog.h"

class Esp32Watchdog : public IWatchdog {
public:
    Esp32Watchdog();
    virtual ~Esp32Watchdog() = default;

    bool init(uint32_t timeoutMs) override;
    void kick() override;
    void enable() override;
    void disable() override;

private:
    bool initialized;
    bool enabled;
};
