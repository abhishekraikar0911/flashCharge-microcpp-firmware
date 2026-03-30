/**
 * @file Esp32Gpio.h
 * @brief ESP32 implementation for IGpio interface
 * @layer HAL
 *
 * Wraps Arduino pinMode, digitalWrite, digitalRead, and analogRead.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "hal/interfaces/IGpio.h"

class Esp32Gpio : public IGpio {
public:
    Esp32Gpio();
    virtual ~Esp32Gpio() = default;

    void setMode(int pin, Mode mode) override;
    void write(int pin, bool high) override;
    bool read(int pin) override;
    int  analogRead(int pin) override;
};
