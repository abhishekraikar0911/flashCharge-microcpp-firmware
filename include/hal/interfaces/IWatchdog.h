/**
 * @file IWatchdog.h
 * @brief HAL interface for hardware watchdog timer
 * @layer HAL — MCU peripheral abstraction
 *
 * Implementations: Esp32Watchdog (wraps esp_task_wdt_*)
 * The BSP registers each critical task on init.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include <stdint.h>

class IWatchdog {
public:
    virtual ~IWatchdog() = default;

    /**
     * Initialize the watchdog with a timeout.
     * After this duration without a kick(), the system will reset.
     * @param timeoutMs  Maximum interval between kicks (milliseconds)
     * @return true on success
     */
    virtual bool init(uint32_t timeoutMs) = 0;

    /**
     * Reset the watchdog counter (confirm the system is alive).
     * Must be called from each registered task within timeoutMs.
     */
    virtual void kick() = 0;

    /** Enable watchdog resets (call after init). */
    virtual void enable() = 0;

    /** Temporarily disable watchdog (use only in controlled test scenarios). */
    virtual void disable() = 0;
};
