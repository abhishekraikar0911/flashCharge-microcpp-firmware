/**
 * @file include/utils/charger_safety_policy.h
 * @brief Pure, header-only safety decision logic extracted from HardwareService.
 *
 * These functions have ZERO platform dependencies (no Arduino, no FreeRTOS).
 * They only operate on a StateSnapshot so they are trivially unit-testable.
 */
#pragma once

#ifndef NATIVE_TEST_BUILD
#  include <Arduino.h>
#endif

// StateSnapshot is defined in system_state.h. We only need it as a parameter type.
struct StateSnapshot;  // Forward declared if included without full system_state.h

#include "system/state/SystemState.h"

namespace ChargerSafetyPolicy {

    /// Maximum safe charger module temperature in degrees Celsius.
    static constexpr float MAX_SAFE_TEMP = 70.0f;

    /// BMS heartbeat timeout in milliseconds before vehicle considered disconnected.
    static constexpr unsigned long BMS_TIMEOUT_MS = 3000UL;

    /**
     * @brief Returns true when the charger temperature is within safe limits.
     *
     * Mirrors the logic in HardwareService::pollSafetyMonitor.
     */
    inline bool isTemperatureSafe(const StateSnapshot& snap) {
        return snap.chargerTemp <= MAX_SAFE_TEMP;
    }

    /**
     * @brief Returns true when the BMS heartbeat has timed out.
     *
     * Mirrors the logic in HardwareService::pollPlugDetection.
     * @param now  Current value of millis() — injected so tests can control time.
     */
    inline bool isBMSTimedOut(const StateSnapshot& snap, unsigned long now) {
        if (!snap.gunPhysicallyConnected && !snap.batteryConnected)
            return false;
        return (now - snap.lastBMS) > BMS_TIMEOUT_MS;
    }

    /**
     * @brief Returns true when it is completely safe to continue/begin charging.
     *
     * Aggregates all safety conditions:
     *  1. Temperature must be within range.
     *  2. BMS must not have timed out.
     *  3. No fault lock must be active.
     */
    inline bool isSafeToCharge(const StateSnapshot& snap, unsigned long now) {
        if (!isTemperatureSafe(snap))    return false;
        if (isBMSTimedOut(snap, now))    return false;
        if (snap.faultLockActive)        return false;
        return true;
    }

} // namespace ChargerSafetyPolicy
