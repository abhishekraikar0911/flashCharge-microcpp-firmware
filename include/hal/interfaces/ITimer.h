/**
 * @file ITimer.h
 * @brief HAL interface for system timing and delays
 * @layer HAL — MCU peripheral abstraction
 *
 * Implementations: Esp32Timer (wraps millis(), vTaskDelay())
 * Use this instead of calling millis()/delay() directly from drivers.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include <stdint.h>

class ITimer {
public:
    virtual ~ITimer() = default;

    /**
     * Return milliseconds since system boot.
     * Wraps at ~49.7 days (uint32 overflow) — same as Arduino millis().
     */
    virtual uint32_t millis() = 0;

    /**
     * Block the calling task for the given duration.
     * On FreeRTOS, this yields the scheduler (vTaskDelay).
     */
    virtual void delayMs(uint32_t ms) = 0;

    /**
     * Return microseconds since system boot.
     * Use only where millisecond resolution is insufficient.
     */
    virtual uint64_t micros() = 0;
};
