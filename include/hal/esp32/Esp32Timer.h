/**
 * @file Esp32Timer.h
 * @brief ESP32 implementation for ITimer interface
 * @layer HAL
 *
 * Wraps Arduino millis(), micros(), and FreeRTOS vTaskDelay().
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "hal/interfaces/ITimer.h"

class Esp32Timer : public ITimer {
public:
    Esp32Timer();
    virtual ~Esp32Timer() = default;

    uint32_t millis() override;
    void delayMs(uint32_t ms) override;
    uint64_t micros() override;
};
