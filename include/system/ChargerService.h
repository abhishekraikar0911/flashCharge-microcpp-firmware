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

    // Resets dynamic limit tracking — must be called at each transaction start
    void resetDynamicLimits();

private:
    ChargerService() = default;

    void pollPlugDetection(const StateSnapshot& snap);
    void pollChargerHealth();
    void pollVehicleInfo(const StateSnapshot& snap);

    unsigned long _lastPlugCheck           = 0;
    unsigned long _lastChargerHealthCheck  = 0;
    unsigned long _lastVehicleInfoCheck    = 0;
    unsigned long _lastVehicleInfoSent     = 0;
    unsigned long _lastVoltageTime         = 0;
    unsigned long _lastChargingStopTime    = 0;

    float _lastVoltageCheck  = 0.0f;
    float _lastSentImax      = -1.0f; // Tracks last sent Imax to detect changes; -1 forces update on first call
    float _lastSentVmax      = -1.0f; // Tracks last sent Vmax; -1 forces update on first call
    float _lastSentSoc       = -1.0f;
    float _lastSentMaxCurrent= -1.0f;
    bool  _lastPlugState     = false;
    bool  _chargerHealthy    = true;
    bool  _lastChargingEnabled = false;
};

} // namespace prod
