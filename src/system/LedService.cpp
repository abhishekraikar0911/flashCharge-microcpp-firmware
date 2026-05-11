#include "system/LedService.h"
#include "app/AppContext.h"
#include "config/hardware.h"
#include "system/NetworkManager.h"
#include "system/SystemState.h"
#include <MicroOcpp.h>

namespace prod {

void LedService::begin() {
    if (g_app.gpio) {
        g_app.gpio->setMode(LED_CHARGER_STATUS, IGpio::GPIO_OUTPUT);
        g_app.gpio->setMode(LED_NETWORK_STATUS, IGpio::GPIO_OUTPUT);
        g_app.gpio->setMode(LED_FAULT_STATUS, IGpio::GPIO_OUTPUT);
        g_app.gpio->write(LED_CHARGER_STATUS, false);
        g_app.gpio->write(LED_NETWORK_STATUS, false);
        g_app.gpio->write(LED_FAULT_STATUS, false);
    }
    if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "LED_SVC", "Started");
}

void LedService::poll() {
    if (!g_app.gpio) return;

    unsigned long now = g_app.timer ? g_app.timer->millis() : 0;
    bool blinkToggle = false;
    if (now - _lastBlinkTime >= 500) {
        _chargerLedState = !_chargerLedState;
        _networkLedState = !_networkLedState;
        _lastBlinkTime   = now;
        blinkToggle      = true;
    }

    // Network LED (D15): Blink = Connected, Steady ON = Offline
    if (prod::g_networkManager.isConnected()) {
        if (blinkToggle) g_app.gpio->write(LED_NETWORK_STATUS, _networkLedState);
    } else {
        g_app.gpio->write(LED_NETWORK_STATUS, true);
    }

    // Charger LED (D13): Blink = Charging, Steady ON = Ready, OFF = Faulted
    ChargePointStatus libStatus = ChargePointStatus_Available;
    if (SystemState::instance().getOcppInitialized()) {
        libStatus = getChargePointStatus(1);
    }

    if (libStatus == ChargePointStatus_Charging) {
        if (blinkToggle) g_app.gpio->write(LED_CHARGER_STATUS, _chargerLedState);
    }
    else if (libStatus == ChargePointStatus_Available  ||
             libStatus == ChargePointStatus_Finishing  ||
             libStatus == ChargePointStatus_Preparing) {
        g_app.gpio->write(LED_CHARGER_STATUS, true);
    }
    else if (libStatus == ChargePointStatus_Faulted) {
        g_app.gpio->write(LED_CHARGER_STATUS, false);
    }
    else {
        g_app.gpio->write(LED_CHARGER_STATUS, true);
    }
    // ── FAULT LED (D13): Blink 500ms = Fault Active, OFF = Healthy ──
    // SafetyService no longer drives this pin directly — LedService is the
    // single owner of LED_FAULT_STATUS to ensure consistent blink behaviour.
    bool faultActive = SystemState::instance().getFaultLockActive();
    if (faultActive) {
        if (blinkToggle) g_app.gpio->write(LED_FAULT_STATUS, _faultLedState = !_faultLedState);
    } else {
        _faultLedState = false;
        g_app.gpio->write(LED_FAULT_STATUS, false);
    }
}

} // namespace prod
