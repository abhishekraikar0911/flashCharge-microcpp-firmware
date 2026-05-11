#include "services/safety/SafetyService.h"
#include "app/AppContext.h"
#include "config/hardware.h"
#include "services/ocpp/OcppClient.h"
#include "services/charging/TransactionService.h"

namespace prod {

void SafetyService::begin() {
    if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "SAFETY_SVC", "Started");
    
    if (g_app.gpio) {
        g_app.gpio->setMode(BTN_ESTOP, IGpio::GPIO_INPUT_PULLUP);
        // Physical Start/Stop buttons: Active LOW, Normally Open
        g_app.gpio->setMode(BTN_START, IGpio::GPIO_INPUT_PULLUP);
        g_app.gpio->setMode(BTN_STOP,  IGpio::GPIO_INPUT_PULLUP);
        // Note: LED_FAULT_STATUS (D13) is initialized and owned by LedService
        if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "SAFETY_SVC",
            "Buttons initialized: E-Stop=GPIO32, Start=GPIO33, Stop=GPIO26");
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
    pollContactWelding(snap);
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
        // LED_FAULT_STATUS blink is handled by LedService polling faultLockActive

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

    // BMS Comm Timeout (10s)
    if (snap.transactionActive && (now - snap.lastBMS > 10000)) {
        static unsigned long lastPrint = 0;
        if (now - lastPrint > 2000) {
            if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
                "[BMS_TIMEOUT_10S] BmsAge=%lums | V=%.1fV I=%.1fA | BmsSafe=%d ChgReady=%d Fault=%d",
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
            // LED blink handled by LedService
            if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
                "[BMS_TIMEOUT_10S] >>> endTransaction(Other) fired | txId=%d",
                SystemState::instance().getActiveTransactionId());
            // Notify CSMS with a descriptive SystemAlert (StopTransaction.reason='Other' is OCPP-mandated)
            ocpp::sendSystemAlert("BMS_CAN_TIMEOUT",
                "Vehicle BMS CAN communication lost for >10s during charging", "Critical");
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
                // ── Byte 5 of 0x1806E5F4 just went to 0x01 (charger switch OFF) ──
                // Disambiguate: SOC==100% is normal charge completion.
                // SOC<100% is a BMS protection trip (cell fault, overtemp, imbalance, etc.)
                bool chargeComplete = (snap.socPercent >= 100.0f);

                if (chargeComplete) {
                    // ── PATH A: Normal full charge ──────────────────────────────
                    if (g_app.logger) g_app.logger->logf(ILogger::Level::INFO, "STOP_TRIGGER",
                        "[BMS_FULL_CHARGE] SOC=%.1f%% | V=%.1fV | BmsAge=%lums | Tx=%d",
                        snap.socPercent, snap.terminalVolt,
                        (snap.lastBMS > 0 ? now - snap.lastBMS : 99999u),
                        (int)snap.transactionActive);

                    if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
                        SystemState::instance().setStopReason(StopReason::BMS_FULL_CHARGE);
                        // No FaultLock — this is a clean, expected stop
                        if (g_app.logger) g_app.logger->logf(ILogger::Level::INFO, "STOP_TRIGGER",
                            "[BMS_FULL_CHARGE] >>> endTransaction(Other) fired | txId=%d",
                            SystemState::instance().getActiveTransactionId());
                        ocpp::sendSystemAlert("CHARGE_COMPLETE",
                            "Battery fully charged (SOC=100%). BMS stopped charging normally.", "Info");
                        ocpp::endTransactionSafe(nullptr, "Other");
                    }

                } else {
                    // ── PATH B: BMS protection / MOSFET fault ────────────────────
                    if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
                        "[BMS_SWITCH_OFF] bmsSafe=%d->0 | SOC=%.1f%% V=%.1fV I=%.1fA | BmsAge=%lums Tx=%d",
                        (int)_lastBmsSafe, snap.socPercent,
                        snap.terminalVolt, snap.terminalCurr,
                        (snap.lastBMS > 0 ? now - snap.lastBMS : 99999u),
                        (int)snap.transactionActive);

                    if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
                        SystemState::instance().setFaultLockActive(true);
                        SystemState::instance().setFaultLockTime(now);
                        SystemState::instance().setStopReason(StopReason::BMS_SWITCH_OFF);
                        // LED blink handled by LedService
                        if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
                            "[BMS_SWITCH_OFF] >>> endTransaction(Other) fired | txId=%d",
                            SystemState::instance().getActiveTransactionId());
                        ocpp::sendSystemAlert("BMS_CHARGER_SWITCH_OFF",
                            "BMS charger MOSFET tripped (SOC<100%) — possible cell fault or protection", "Warning");
                        ocpp::endTransactionSafe(nullptr, "Other");
                    }
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
        // LED blink handled by LedService
        if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
            SystemState::instance().setFaultLockActive(true);
            SystemState::instance().setFaultLockTime(now);
            SystemState::instance().setStopReason(StopReason::OVERTEMP);
            // Alert CSMS with temperature value before stopping
            char tempMsg[64];
            snprintf(tempMsg, sizeof(tempMsg),
                "Charger terminal temperature critical: %.1f C", snap.chargerTemp);
            ocpp::sendSystemAlert("OVER_TEMPERATURE", tempMsg, "Critical");
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
        // LED_FAULT_STATUS will be turned OFF by LedService on next poll()
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

    // ── STOP BUTTON (GPIO 26, Active LOW, INPUT_PULLUP) ──
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

void SafetyService::pollContactWelding(const StateSnapshot& snap) {
    // Feature temporarily disabled as per user request. 
    // Remove the early return below to re-enable 15-second decay check.
    return;

    uint32_t now = g_app.timer ? g_app.timer->millis() : 0;

    // Trigger on FALLING EDGE of chargingEnabled
    if (_lastChargingState && !snap.chargingEnabled) {
        _weldCheckActive = true;
        _weldCheckStartTime = now;
        _weldCheckStep = 0;
        _decayVoltages[0] = snap.terminalVolt; // t=0s
        if (g_app.logger) g_app.logger->logf(ILogger::Level::INFO, "SAFETY", 
            "Contact Welding Check STARTED. V0 = %.1fV", _decayVoltages[0]);
    }
    _lastChargingState = snap.chargingEnabled;

    if (!_weldCheckActive) return;

    // Sample voltage non-blockingly at t=5s, t=10s, t=15s
    int elapsedSeconds = (now - _weldCheckStartTime) / 1000;
    
    if (elapsedSeconds >= 5 && _weldCheckStep == 0) {
        _weldCheckStep = 1;
        _decayVoltages[1] = snap.terminalVolt; // t=5s
    } 
    else if (elapsedSeconds >= 10 && _weldCheckStep == 1) {
        _weldCheckStep = 2;
        _decayVoltages[2] = snap.terminalVolt; // t=10s
    }
    else if (elapsedSeconds >= 15 && _weldCheckStep == 2) {
        _weldCheckStep = 3;
        _decayVoltages[3] = snap.terminalVolt; // t=15s
        
        _weldCheckActive = false;
        float v0 = _decayVoltages[0];
        float v3 = _decayVoltages[3];

        if (g_app.logger) g_app.logger->logf(ILogger::Level::DEBUG, "SAFETY", 
            "Weld Check FINISHED. V0=%.1fV, V15=%.1fV (Drop: %.1fV)", v0, v3, (v0 - v3));

        // Only check if we started from a reasonably high charging voltage
        if (v0 > 15.0f) {
            // Did it drop at least 3.0V over 15 seconds?
            // If the contactor is welded, it will stay near the battery voltage (drop < 1V)
            bool sufficientDrop = (v0 - v3) >= MIN_DROP_THRESHOLD;

            if (!sufficientDrop) {
                if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "SAFETY", 
                    "🚨 FAULT: CONTACT WELDING DETECTED! Voltage only dropped %.1fV in 15 seconds (Expected >3.0V)", (v0 - v3));
                
                // Hardware Safety Action
                SystemState::instance().setFaultLockActive(true);
                SystemState::instance().setFaultLockTime(now);
                SystemState::instance().setStopReason(StopReason::FAULT);
                // LED blink handled by LedService

                // OCPP Notification
                ocpp::sendSystemAlert("PowerSwitchFailure", "Contact welding detected: no voltage decay", "Critical");
                if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
                    ocpp::endTransactionSafe(nullptr, "HardReset");
                }
            } else {
                if (g_app.logger) g_app.logger->logf(ILogger::Level::INFO, "SAFETY", 
                    "Contact Welding Check PASSED. Healthy decay confirmed (Drop: %.1fV).", (v0 - v3));
            }
        } else {
            if (g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "SAFETY", "Weld Check skipped (V0 < 15V)");
        }
    } else if (elapsedSeconds > 20) {
        _weldCheckActive = false; // Timeout safety catch
    }
}

} // namespace prod
