#include "system/SafetyService.h"
#include "app/AppContext.h"
#include "config/hardware.h"
#include "services/OcppClient.h"
#include "services/TransactionService.h"

namespace prod {

void SafetyService::begin() {
    if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "SAFETY_SVC", "Started");
    
    if (g_app.gpio) {
        g_app.gpio->setMode(BTN_ESTOP, IGpio::GPIO_INPUT_PULLUP);
        // Physical Start/Stop buttons: Active LOW, Normally Open
        g_app.gpio->setMode(BTN_START, IGpio::GPIO_INPUT_PULLUP);
        g_app.gpio->setMode(BTN_STOP,  IGpio::GPIO_INPUT_PULLUP);
        // Fault indicator LED: Output, Active HIGH
        g_app.gpio->setMode(LED_FAULT_STATUS, IGpio::GPIO_OUTPUT);
        g_app.gpio->write(LED_FAULT_STATUS, false); // OFF at boot
        if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "SAFETY_SVC",
            "Buttons initialized: E-Stop=GPIO32, Start=GPIO33, Stop=GPIO26, FaultLED=GPIO4");
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
    pollButtons();
    pollSafetyLimits(snap);
    pollFaultLock(snap);
}

void SafetyService::pollEStop() {
    if (!g_app.gpio) return;
    uint32_t now = g_app.timer ? g_app.timer->millis() : 0;

    // NO Button (Desk Setup): Normal=HIGH, Pushed=LOW
    // Change `!g_app.gpio->read` back to `g_app.gpio->read` for production NC buttons!
    bool rawPin = !g_app.gpio->read(BTN_ESTOP);

    // ── Rising-edge debounce (50ms): pin must stay in pushed state before activating ──
    if (rawPin) {
        if (_estopRisingTime == 0) _estopRisingTime = now;   // mark first PUSH
        _estopFallingTime = 0;                                // reset falling timer
    } else {
        if (_estopFallingTime == 0) _estopFallingTime = now; // mark first RELEASE
        _estopRisingTime = 0;                                 // reset rising timer
    }

    bool estopPushed  = rawPin && (_estopRisingTime > 0) && (now - _estopRisingTime  >= 50);
    bool estopReleased = !rawPin && (_estopFallingTime > 0) && (now - _estopFallingTime >= 100);

    if (estopPushed && !_estopActive) {
        if (g_app.logger) g_app.logger->log(ILogger::Level::ERROR, "SAFETY", "🚨 EMERGENCY STOP ACTIVATED");

        if (g_app.charger) g_app.charger->stopCharging();
        if (g_app.relay)   g_app.relay->open();

        SystemState::instance().setChargingEnabled(false);
        SystemState::instance().setFaultLockActive(true);
        SystemState::instance().setFaultLockTime(now);

        // Turn ON fault LED
        if (g_app.gpio) g_app.gpio->write(LED_FAULT_STATUS, true);

        _estopActive = true;
        _pendingEStopNotification = true;
    }
    else if (estopReleased && _estopActive) {
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
            if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
                "[BMS_TIMEOUT_5S] BmsAge=%lums | V=%.1fV I=%.1fA | BmsSafe=%d ChgReady=%d Fault=%d",
                (now - snap.lastBMS), snap.terminalVolt, snap.terminalCurr,
                (int)snap.bmsSafeToCharge,
                (g_app.charger ? (int)g_app.charger->isReady() : -1),
                (int)snap.faultLockActive);
            lastPrint = now;
        }
        SystemState::instance().setChargingEnabled(false);
        if (g_app.charger) g_app.charger->stopCharging();
        if (!_bmsTimeoutHandled && ocpp::isTransactionRunningSafe(1)) {
            _bmsTimeoutHandled = true;
            SystemState::instance().setFaultLockActive(true);
            SystemState::instance().setFaultLockTime(now);
            SystemState::instance().setStopReason(StopReason::BMS_TIMEOUT);
            if (g_app.gpio) g_app.gpio->write(LED_FAULT_STATUS, true); // Fault LED ON
            if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
                "[BMS_TIMEOUT_5S] >>> endTransaction(Other) fired | txId=%d",
                SystemState::instance().getActiveTransactionId());
            ocpp::endTransactionSafe(nullptr, "Other");
        }
    } else {
        // BMS reconnected — reset the timeout guard
        _bmsTimeoutHandled = false;
    }

    // BMS Safety Flag
    if (now - _lastBmsSafetyCheck >= 100) {
        if (snap.bmsSafeToCharge != _lastBmsSafe) {
            if (!snap.bmsSafeToCharge) {
                if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
                    "[BMS_SWITCH_OFF] bmsSafe=%d->0 | V=%.1fV I=%.1fA | BmsAge=%lums Tx=%d",
                    (int)_lastBmsSafe, snap.terminalVolt, snap.terminalCurr,
                    (snap.lastBMS > 0 ? now - snap.lastBMS : 99999u),
                    (int)snap.transactionActive);
                if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
                    SystemState::instance().setFaultLockActive(true);
                    SystemState::instance().setFaultLockTime(now);
                    SystemState::instance().setStopReason(StopReason::BMS_SWITCH_OFF);
                    if (g_app.gpio) g_app.gpio->write(LED_FAULT_STATUS, true); // Fault LED ON
                    if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
                        "[BMS_SWITCH_OFF] >>> endTransaction(Other) fired | txId=%d",
                        SystemState::instance().getActiveTransactionId());
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
        if (g_app.gpio)    g_app.gpio->write(LED_FAULT_STATUS, true); // Fault LED ON
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
        // Turn OFF fault LED — system is healthy again
        if (g_app.gpio) g_app.gpio->write(LED_FAULT_STATUS, false);
    }
}

void SafetyService::pollButtons() {
    if (!g_app.gpio) return;
    uint32_t now = g_app.timer ? g_app.timer->millis() : 0;

    // ── START BUTTON (GPIO 33, Active LOW, INPUT_PULLUP) ──
    // Pressed = LOW (pin reads false when pulled to GND)
    bool startPressed = !g_app.gpio->read(BTN_START);
    if (startPressed) {
        if (_startBtnRisingTime == 0) _startBtnRisingTime = now;
        // Debounce: must hold for 50ms before triggering
        if (!_startBtnActive && (now - _startBtnRisingTime >= 50)) {
            _startBtnActive = true;
            if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "BTN", "🟢 START button pressed");
            prod::g_transactionManager.startLocalTransaction("LOCAL_ADMIN_1");
        }
    } else {
        _startBtnRisingTime = 0;
        _startBtnActive     = false; // Reset so next press can fire again
    }

    // ── STOP BUTTON (GPIO 4, Active LOW, INPUT_PULLUP) ──
    bool stopPressed = !g_app.gpio->read(BTN_STOP);
    if (stopPressed) {
        if (_stopBtnRisingTime == 0) _stopBtnRisingTime = now;
        if (!_stopBtnActive && (now - _stopBtnRisingTime >= 50)) {
            _stopBtnActive = true;
            if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "BTN", "🔴 STOP button pressed");
            prod::g_transactionManager.stopLocalTransaction();
        }
    } else {
        _stopBtnRisingTime = 0;
        _stopBtnActive     = false;
    }
}

} // namespace prod
