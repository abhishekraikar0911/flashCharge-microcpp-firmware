/**
 * @file LedService.h
 * @brief Status LED control (D13 charger, D15 network) — blink patterns per OCPP state
 * @layer Service
 *
 * Uses: g_app.gpio, getChargePointStatus(), g_networkManager
 * Does NOT access pins directly — uses g_app.gpio abstraction.
 */
#pragma once

namespace prod {

class LedService {
public:
    static LedService& instance() {
        static LedService inst;
        return inst;
    }

    void begin();
    void poll();

private:
    LedService() = default;

    unsigned long _lastBlinkTime   = 0;
    bool          _chargerLedState = false;
    bool          _networkLedState = false;
    bool          _faultLedState   = false;  // D13: blinks when faultLockActive
};

} // namespace prod
