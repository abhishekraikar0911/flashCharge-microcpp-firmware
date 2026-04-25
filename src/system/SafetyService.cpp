#include "system/SafetyService.h"
#include "app/AppContext.h"
#include "config/hardware.h"
#include "services/OcppClient.h"

namespace prod {

void SafetyService::begin() {
    if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "SAFETY_SVC", "Started");
    
    // Initialize the Normally Closed E-Stop pin with an internal Pull-Up resistor
    if (g_app.gpio) {
        g_app.gpio->setMode(BTN_ESTOP, IGpio::GPIO_INPUT_PULLUP);
    }
}

void SafetyService::poll() {
    uint32_t now = g_app.timer ? g_app.timer->millis() : 0;
    static uint32_t lastDiagLog = 0;
    if (now - lastDiagLog > 5000) {
        lastDiagLog = now;
        if (g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "SAFETY_SVC", "poll()");
    }
    auto snap = SystemState::instance().snapshot();
    pollEStop();
    pollSafetyLimits(snap);
    pollFaultLock(snap);
}

void SafetyService::pollEStop() {
    if (!g_app.gpio) return;
    uint32_t now = g_app.timer ? g_app.timer->millis() : 0;

    // NC Button: Normal=LOW, Pushed/Cut=HIGH
    bool estopPushed = g_app.gpio->read(BTN_ESTOP);

    if (estopPushed && !_estopActive) {
        if (g_app.logger) g_app.logger->log(ILogger::Level::ERROR, "SAFETY", "🚨 EMERGENCY STOP ACTIVATED");

        if (g_app.charger) g_app.charger->stopCharging();
        if (g_app.relay)   g_app.relay->open();

        SystemState::instance().setChargingEnabled(false);
        SystemState::instance().setFaultLockActive(true);
        SystemState::instance().setFaultLockTime(now);

        _estopActive = true;
        _pendingEStopNotification = true;
    }
    else if (!estopPushed && _estopActive) {
        if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "SAFETY", "E-Stop released (fault lock remains until stabilization)");
        _estopActive = false;
    }

    // Async OCPP notifications
    if (_pendingEStopNotification) {
        if (ocpp::lock(10)) {
            ocpp::sendSystemAlert("EMERGENCY_STOP", "Physical E-Stop button pushed", "Critical");
            auto snap = SystemState::instance().snapshot();
            if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
                SystemState::instance().setStopReason(StopReason::EMERGENCY_STOP);
                ocpp::endTransactionSafe(nullptr, "EmergencyStop");
            }
            _pendingEStopNotification = false;
            ocpp::unlock();
        }
    }
}

void SafetyService::pollSafetyLimits(const StateSnapshot& snap) {
    uint32_t now = g_app.timer ? g_app.timer->millis() : 0;
    if (now - _lastSafetyCheck < 100) return;
    _lastSafetyCheck = now;

    // BMS Comm Timeout (5s)
    if (snap.transactionActive && (now - snap.lastBMS > 5000)) {
        static unsigned long lastPrint = 0;
        if (now - lastPrint > 2000) {
            if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "SAFETY", "🚨 BMS TIMEOUT: %lu ms", now - snap.lastBMS);
            lastPrint = now;
        }
        SystemState::instance().setChargingEnabled(false);
        if (g_app.charger) g_app.charger->stopCharging();
        if (ocpp::isTransactionRunningSafe(1)) {
            SystemState::instance().setFaultLockActive(true);
            SystemState::instance().setFaultLockTime(now);
            SystemState::instance().setStopReason(StopReason::BMS_TIMEOUT);
            ocpp::endTransactionSafe(nullptr, "Other");
        }
    }

    // BMS Safety Flag
    if (now - _lastBmsSafetyCheck >= 100) {
        if (snap.bmsSafeToCharge != _lastBmsSafe) {
            if (!snap.bmsSafeToCharge) {
                if (g_app.logger) g_app.logger->log(ILogger::Level::ERROR, "SAFETY", "🚨 BMS Charger Switch OFF (flag 0x01)");
                if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
                    SystemState::instance().setFaultLockActive(true);
                    SystemState::instance().setFaultLockTime(now);
                    SystemState::instance().setStopReason(StopReason::BMS_SWITCH_OFF);
                    ocpp::endTransactionSafe(nullptr, "Other");
                }
            } else {
                if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "SAFETY", "BMS charging enabled");
            }
            _lastBmsSafe = snap.bmsSafeToCharge;
        }
        _lastBmsSafetyCheck = now;
    }

    // Temperature
    if (snap.chargerTemp > ALERT_TEMP_WARNING_C && snap.chargerTemp <= ALERT_TEMP_CRITICAL_C && !_tempWarningActive) {
        if (g_app.logger) g_app.logger->logf(ILogger::Level::WARN, "TEMP", "⚠️  %.1f°C — approaching critical", snap.chargerTemp);
        ocpp::sendSystemAlert("TEMP_WARNING", "Charger temperature near critical", "Warning");
        _tempWarningActive = true;
    } else if (snap.chargerTemp <= ALERT_TEMP_WARNING_C && _tempWarningActive) {
        _tempWarningActive = false;
    }

    if (snap.chargerTemp > ALERT_TEMP_CRITICAL_C && !_tempCriticalActive) {
        if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "TEMP", "🚨 CRITICAL: %.1f°C — STOP", snap.chargerTemp);
        SystemState::instance().setChargingEnabled(false);
        if (g_app.charger) g_app.charger->stopCharging();
        if (g_app.relay)   g_app.relay->open();
        if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
            SystemState::instance().setFaultLockActive(true);
            SystemState::instance().setFaultLockTime(now);
            SystemState::instance().setStopReason(StopReason::OVERTEMP);
            ocpp::endTransactionSafe(nullptr, "EmergencyStop");
        }
        _tempCriticalActive = true;
    } else if (snap.chargerTemp < (ALERT_TEMP_CRITICAL_C - 10.0f) && _tempCriticalActive) {
        _tempCriticalActive = false;
        _tempWarningActive  = false;
    }
}

void SafetyService::pollFaultLock(const StateSnapshot& snap) {
    uint32_t now = g_app.timer ? g_app.timer->millis() : 0;
    if (!snap.faultLockActive) return;
    if ((now - snap.faultLockTime) < FAULT_STABILIZATION_PERIOD_MS) return;
    if (_estopActive) return;

    if (snap.bmsSafeToCharge &&
        snap.terminalVolt >= ALERT_VOLTAGE_MIN_V &&
        snap.chargerTemp  <= ALERT_TEMP_CRITICAL_C) {
        if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "FAULT", "Stability restored, fault lock cleared");
        SystemState::instance().setFaultLockActive(false);
    }
}

} // namespace prod
