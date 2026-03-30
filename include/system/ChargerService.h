/**
 * @file ChargerService.h
 * @brief Plug detection and charger module health monitoring
 * @layer Service
 *
 * Uses: g_app.charger, g_app.bms, SystemState, ocpp::
 * Does NOT access hardware pins directly.
 */
#pragma once
#include "system/SystemState.h"

namespace prod {

class ChargerService {
public:
    static ChargerService& instance() {
        static ChargerService inst;
        return inst;
    }

    void begin();
    void poll();

    bool isChargerHealthy() const { return _chargerHealthy; }
    unsigned long lastChargingStopTime() const { return _lastChargingStopTime; }

private:
    ChargerService() = default;

    void pollPlugDetection(const StateSnapshot& snap);
    void pollChargerHealth();

    unsigned long _lastPlugCheck           = 0;
    unsigned long _lastChargerHealthCheck  = 0;
    unsigned long _lastVoltageTime         = 0;
    unsigned long _lastChargingStopTime    = 0;

    float _lastVoltageCheck  = 0.0f;
    bool  _lastPlugState     = false;
    bool  _chargerHealthy    = true;
    bool  _lastChargingEnabled = false;
};

} // namespace prod
