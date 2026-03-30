#include "system/EnergyService.h"
#include "app/AppContext.h"
#include "services/OcppClient.h"
#include "system/SystemState.h"

namespace prod {

void EnergyService::begin() {
    _lastEnergyTime = g_app.timer ? g_app.timer->millis() : 0;
    if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "ENERGY_SVC", "Started");
}

void EnergyService::poll() {
    uint32_t current_time = g_app.timer ? g_app.timer->millis() : 0;
    static uint32_t lastDiagLog = 0;
    if (current_time - lastDiagLog > 5000) {
        lastDiagLog = current_time;
        if (g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "ENERGY_SVC", "poll()");
    }
    auto snap = SystemState::instance().snapshot();
    bool canCharge = (ocpp::ocppPermitsChargeSafe(1) && snap.transactionActive && snap.chargingEnabled);

    if (_lastChargingEnabled && !canCharge) {
        _lastChargingStopTime = current_time;
    }
    _lastChargingEnabled = canCharge;

    if (canCharge && snap.terminalVolt > 56.0f && snap.terminalCurr > 0.0f) {
        double dt_hours   = (current_time - _lastEnergyTime) / 3600000.0;
        double energyDelta = (double)snap.terminalVolt * (double)snap.terminalCurr * dt_hours;

        if (energyDelta > 0.0 && energyDelta < 1000.0) {
            SystemState::instance().addEnergyWh(energyDelta);
        }
        _lastEnergyTime = current_time;
    } else {
        _lastEnergyTime = current_time;
    }
}

} // namespace prod
