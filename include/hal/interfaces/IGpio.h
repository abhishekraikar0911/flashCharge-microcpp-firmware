/**
 * @file IGpio.h
 * @brief HAL interface for GPIO and ADC peripheral
 * @layer HAL — MCU peripheral abstraction
 *
 * Implementations: Esp32Gpio
 * Replaces all direct digitalWrite/digitalRead/analogRead calls.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once

class IGpio {
public:
    enum Mode {
        GPIO_INPUT,
        GPIO_OUTPUT,
        GPIO_INPUT_PULLUP,
        GPIO_INPUT_PULLDOWN
    };

    virtual ~IGpio() = default;

    /** Configure pin direction. */
    virtual void setMode(int pin, Mode mode) = 0;

    /** Write digital HIGH (true) or LOW (false). */
    virtual void write(int pin, bool high) = 0;

    /** Read digital state. */
    virtual bool read(int pin) = 0;

    /**
     * Read raw ADC value.
     * @return raw ADC count (platform-dependent range, e.g. 0–4095 for ESP32)
     */
    virtual int analogRead(int pin) = 0;
};
