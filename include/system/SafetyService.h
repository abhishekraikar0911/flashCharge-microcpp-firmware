/**
 * @file SafetyService.h
 * @brief Emergency stop, temperature, voltage/current fault handling, and fault lock timer
 * @layer Service
 *
 * Uses: g_app.gpio (BTN_ESTOP), g_app.charger->stopCharging(), SystemState, ocpp::
 * Does NOT access hardware pins directly except through g_app.gpio.
 */
#pragma once
#include "system/SystemState.h"

namespace prod {

class SafetyService {
public:
    static SafetyService& instance() {
        static SafetyService inst;
        return inst;
    }

    void begin();
    void poll();

    bool isEstopActive() const { return _estopActive; }

private:
    SafetyService() = default;

    void pollEStop();
    void pollSafetyLimits(const StateSnapshot& snap);
    void pollFaultLock(const StateSnapshot& snap);
    void pollButtons();  // Physical START / STOP buttons

    unsigned long _lastSafetyCheck   = 0;
    unsigned long _lastBmsSafetyCheck = 0;

    bool _estopActive              = false;
    bool _pendingEStopNotification = false;
    bool _tempWarningActive        = false;
    bool _tempCriticalActive       = false;
    bool _voltageAlertActive       = false;
    bool _lastBmsSafe              = true;
    bool _bmsTimeoutHandled        = false; // Prevents repeated endTransactionSafe on timeout

    // E-Stop debounce: prevent contact bounce from mis-triggering
    unsigned long _estopRisingTime  = 0; // When pin first went HIGH (pressed)
    unsigned long _estopFallingTime = 0; // When pin first went LOW (released)

    // Start button debounce
    unsigned long _startBtnRisingTime  = 0;
    bool          _startBtnActive      = false;

    // Stop button debounce
    unsigned long _stopBtnRisingTime   = 0;
    bool          _stopBtnActive       = false;

    // Contact Welding Detection
    bool          _lastChargingState    = false;
    unsigned long _weldCheckStartTime   = 0;
    bool          _weldCheckActive      = false;
    int           _weldCheckStep        = 0;
    float         _decayVoltages[4]     = {0.0f};
    const float   MIN_DROP_THRESHOLD    = 3.0f; // 3.0V drop required over 15 seconds

    void pollContactWelding(const StateSnapshot& snap);
};

} // namespace prod
