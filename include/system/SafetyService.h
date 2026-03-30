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

    unsigned long _lastSafetyCheck   = 0;
    unsigned long _lastBmsSafetyCheck = 0;

    bool _estopActive              = false;
    bool _pendingEStopNotification = false;
    bool _tempWarningActive        = false;
    bool _tempCriticalActive       = false;
    bool _voltageAlertActive       = false;
    bool _lastBmsSafe              = true;
};

} // namespace prod
