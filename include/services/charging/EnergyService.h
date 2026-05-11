/**
 * @file EnergyService.h
 * @brief Double-precision energy accumulation (Wh) from charger telemetry
 * @layer Service
 *
 * Uses: g_app.charger->getTelemetry(), SystemState::addEnergyWh()
 */
#pragma once

namespace prod {

class EnergyService {
public:
    static EnergyService& instance() {
        static EnergyService inst;
        return inst;
    }

    void begin();
    void poll();

private:
    EnergyService() = default;

    unsigned long _lastEnergyTime      = 0;
    unsigned long _lastChargingStopTime = 0;
    bool          _lastChargingEnabled = false;
};

} // namespace prod
