/**
 * @file hardware_service.cpp
 * @brief Implementation of HardwareService — extracted from main.cpp loop().
 */

#include "modules/hardware_service.h"
#include "header.h"
#include "config/hardware.h"
#include "ocpp/ocpp_client.h"
// PHASE 4: Removed #include "ocpp_state_machine.h" — library manages state internally
#include "modules/wifi_manager.h"
#include "modules/network_manager.h"
#include "modules/health_monitor.h"
#include "modules/system_state.h"
#include <MicroOcpp.h>


using namespace prod;

HardwareService g_hardwareService;

// ═══════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════

void HardwareService::begin() {
    _lastEnergyTime = millis();
    
    // Initialize Status LEDs
    pinMode(LED_CHARGER_STATUS, OUTPUT);
    pinMode(LED_NETWORK_STATUS, OUTPUT);
    digitalWrite(LED_CHARGER_STATUS, LOW);
    digitalWrite(LED_NETWORK_STATUS, LOW);

    // Initialize E-Stop (Internal Pull-up for NC button)
    pinMode(BTN_ESTOP, INPUT_PULLUP);
    
    _lastNetworkTime = millis();
    
    Serial.println("[HW_SVC] Hardware monitoring service started");
}

void HardwareService::poll() {
    auto snap = SystemState::instance().snapshot();
    pollEStop(snap);
    pollPlugDetection(snap);
    pollSafetyMonitor(snap);
    pollEnergyAccumulation(snap);
    pollChargerHealth();
    pollFaultLock(snap);
    pollWiFiMonitor();
    pollVehicleInfo(snap);
    pollPostTxVehicleInfo(snap);
    pollStatusLEDs();
}

// ═══════════════════════════════════════════════════════════════
// 1. PLUG DETECTION (Hybrid — BMS timeout + Voltage drop)
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollPlugDetection(const StateSnapshot& snap) {
    if (millis() - _lastPlugCheck < 500)
        return;

    bool shouldDisconnect = false;

    // Method 1: BMS timeout (3 seconds) — most reliable
    if ((snap.gunPhysicallyConnected || snap.batteryConnected) && (millis() - snap.lastBMS > 3000)) {
        Serial.println("[PLUG] 🔌 Disconnected: BMS timeout (3s)");
        shouldDisconnect = true;
    }

    // Method 3: Voltage drop rate (>2V/s) 
    bool bmsActive = (millis() - snap.lastBMS < 5000);
    bool chargingJustStopped = (millis() - _lastChargingStopTime < 10000);

    if (snap.terminalVolt > 10.0f && !_canRecoveryActive && !bmsActive && !chargingJustStopped) {
        if (_lastVoltageTime > 0) {
            float deltaV = _lastVoltageCheck - snap.terminalVolt;
            float deltaT = (millis() - _lastVoltageTime) / 1000.0f;
            if (deltaT > 0.5f && (deltaV / deltaT) > 2.0f) {
                Serial.printf("[PLUG] 🔌 Disconnected: Fast voltage drop (%.1fV/s)\n", deltaV / deltaT);
                shouldDisconnect = true;
            }
        }
        _lastVoltageCheck = snap.terminalVolt;
        _lastVoltageTime = millis();
    } else {
        _lastVoltageCheck = 0.0f;
        _lastVoltageTime = 0;
    }

    // Check charger health for CAN recovery
    bool chargerHealthy = isChargerModuleHealthy();
    if (!chargerHealthy) {
        _canRecoveryActive = true;
    } else if (_canRecoveryActive) {
        _canRecoveryActive = false;
    }

    // Execute disconnect
    if (shouldDisconnect && (snap.gunPhysicallyConnected || snap.batteryConnected)) {
        SystemState::instance().setGunPhysicallyConnected(false);
        SystemState::instance().setBatteryConnected(false);
        // Also update globals for legacy compat
        // gunPhysicallyConnected = false;
        // batteryConnected = false;
        Serial.println("[PLUG] ✅ Status: DISCONNECTED");

        if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
            Serial.printf("[PLUG] 🛑 Stopping transaction due to EV disconnect (txId=%d)\n", SystemState::instance().getActiveTransactionId());
            ocpp::endTransactionSafe(nullptr, "EVDisconnected");
        }
    }

    // Monitor plug connection state changes
    bool currentPlugState = (snap.gunPhysicallyConnected && snap.batteryConnected);
    if (currentPlugState != _lastPlugState) {
        if (currentPlugState) {
            Serial.println("[PLUG] 🔌 Gun plugged, vehicle detected");
        }
        _lastPlugState = currentPlugState;
    }

    _lastPlugCheck = millis();
}

// ═══════════════════════════════════════════════════════════════
// 2b. EMERGENCY STOP BUTTON (Push-to-Off / Normally Closed)
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollEStop(const StateSnapshot& snap) {
    // NC Button: Normal = LOW (connected to GND), Pushed/Cut = HIGH (Pulled up)
    bool estopPushed = (digitalRead(BTN_ESTOP) == HIGH);

    if (estopPushed && !_estopActive) {
        Serial.println("[SAFETY] 🚨 EMERGENCY STOP BUTTON PUSHED (Hardware Action)");
        
        // 1. FAST: Immediate hardware stop (bypass dataMutex to guarantee speed)
        sendImmediateChargerStop();
        
        // Ensure flag is set so other logic knows charging must stop
        // chargingEnabled = false;
        SystemState::instance().setChargingEnabled(false);

        // Set fault lock immediately
        SystemState::instance().setFaultLockActive(true);
        SystemState::instance().setFaultLockTime(millis());

        _estopActive = true;
        // Schedule slow network notifications for later
        _pendingEStopNotification = true; 
    } 
    else if (!estopPushed && _estopActive) {
        Serial.println("[SAFETY] ✅ Emergency Stop Button released");
        _estopActive = false;
        // System remains in faultLock until stabilization period ends
    }

    // 2. SLOW: Handle network notifications asynchronously to prevent button delay
    if (_pendingEStopNotification) {
        // Attempt to get the lock without blocking forever. 
        // If we fail, we'll try again next loop.
        if (ocpp::lock(10)) { 
            Serial.println("[SAFETY] 📡 Processing pending E-Stop OCPP notifications...");
            ocpp::sendSystemAlert("EMERGENCY_STOP", "Physical E-Stop button pushed", "Critical");
            
            if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
                ocpp::endTransactionSafe(nullptr, "EmergencyStop");
            }
            
            _pendingEStopNotification = false; // Successfully notified
            ocpp::unlock();
        } else {
            // Couldn't get lock instantly, will retry on next poll
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// 2. SAFETY MONITOR (BMS, Temperature, Voltage, Current)
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollSafetyMonitor(const StateSnapshot& snap) {
    // --- BMS Communication Timeout (5s per Protocol V1.0) ---
    if (snap.transactionActive && (millis() - snap.lastBMS > 5000)) {
        static unsigned long lastTimeoutPrint = 0;
        if (millis() - lastTimeoutPrint > 2000) {
            Serial.printf("[SAFETY] \xf0\x9f\x9a\xa8 BMS COMM TIMEOUT: No data for %lu ms\n", millis() - snap.lastBMS);
            lastTimeoutPrint = millis();
        }

        // C1 FIX: SystemState setters are already self-locking via their internal mutex.
        // The legacy dataMutex wrapper here created a dual-lock ordering hazard with
        // the CAN decoder tasks (which hold dataMutex while calling state.setXxx()).
        SystemState::instance().setChargingEnabled(false);
        sendImmediateChargerStop();

        if (ocpp::isTransactionRunningSafe(1)) {
            SystemState::instance().setFaultLockActive(true);
            SystemState::instance().setFaultLockTime(millis());
            ocpp::endTransactionSafe(nullptr, "Other");
        }
    }

    // --- BMS Safety ---
    if (millis() - _lastBmsSafetyCheck >= 100) {
        if (snap.bmsSafeToCharge != _lastBmsSafe) {
            if (!snap.bmsSafeToCharge) {
                Serial.printf("[SAFETY] 🚨 BMS disabled charging (bmsSafeToCharge=%d)\n", snap.bmsSafeToCharge);
                // PHASE 2: Removed sendBMSAlert — library handles fault notification automatically
                if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
                    SystemState::instance().setFaultLockActive(true);
                    SystemState::instance().setFaultLockTime(millis());
                    ocpp::endTransactionSafe(nullptr, "EmergencyStop");
                }
            } else {
                Serial.println("[SAFETY] ✅ BMS charging enabled");
            }
            _lastBmsSafe = snap.bmsSafeToCharge;
        }
        _lastBmsSafetyCheck = millis();
    }

    // --- Temperature — Graduated Response ---
    // WARNING tier (60°C): log & notify, continue charging
    if (snap.chargerTemp > ALERT_TEMP_WARNING_C && snap.chargerTemp <= ALERT_TEMP_CRITICAL_C && !_tempWarningActive) {
        Serial.printf("[TEMP] ⚠️  WARNING: Charger temperature %.1f\xc2\xb0""C — approaching critical\n", snap.chargerTemp);
        ocpp::sendSystemAlert("TEMP_WARNING", "Charger temperature near critical limit", "Warning");
        _tempWarningActive = true;
    } else if (snap.chargerTemp <= ALERT_TEMP_WARNING_C && _tempWarningActive) {
        Serial.printf("[TEMP] ✅ Temperature back to normal: %.1f\xc2\xb0""C\n", snap.chargerTemp);
        _tempWarningActive = false;
    }

    // CRITICAL tier (70°C): immediate hardware stop
    if (snap.chargerTemp > ALERT_TEMP_CRITICAL_C && !_tempCriticalActive) {
        Serial.printf("[TEMP] \xf0\x9f\x9a\xa8 CRITICAL overheat: %.1f\xc2\xb0""C — EMERGENCY STOP\n", snap.chargerTemp);
        SystemState::instance().setChargingEnabled(false);
        sendImmediateChargerStop();

        if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
            SystemState::instance().setFaultLockActive(true);
            SystemState::instance().setFaultLockTime(millis());
            ocpp::endTransactionSafe(nullptr, "EmergencyStop");
        }
        _tempCriticalActive = true;
    } else if (snap.chargerTemp < (ALERT_TEMP_CRITICAL_C - 10.0f) && _tempCriticalActive) {
        Serial.printf("[TEMP] \xe2\x9c\x85 Temperature normalized: %.1f\xc2\xb0""C\n", snap.chargerTemp);
        _tempCriticalActive = false;
        _tempWarningActive  = false;
    }

    // --- Voltage/Current (Summary) ---
    if (snap.terminalVolt > 0.0f && snap.batteryConnected) {
        if ((snap.terminalVolt > ALERT_VOLTAGE_MAX_V || snap.terminalVolt < ALERT_VOLTAGE_MIN_V) && !_voltageAlertActive) {
            // CRITICAL FIX: Only trigger low voltage fault if we're actively expecting voltage (charging)
            if (snap.terminalVolt < ALERT_VOLTAGE_MIN_V && !snap.chargingEnabled && !snap.transactionActive) {
                // Ignore low voltage when not charging (e.g. after StopTransaction when contactor opens)
            } else {
                Serial.printf("[VOLTAGE] 🚨 Fault: %.1fV\n", snap.terminalVolt);
                if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
                    SystemState::instance().setFaultLockActive(true);
                    SystemState::instance().setFaultLockTime(millis());
                    ocpp::endTransactionSafe(nullptr, "EmergencyStop");
                }
                _voltageAlertActive = true;
            }
        } else if (snap.terminalVolt >= MIN_VOLTAGE_V && snap.terminalVolt <= MAX_VOLTAGE_V && _voltageAlertActive) {
            _voltageAlertActive = false;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// 3. ENERGY ACCUMULATION
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollEnergyAccumulation(const StateSnapshot& snap) {
    bool canCharge = (ocpp::ocppPermitsChargeSafe(1) && snap.transactionActive && snap.chargingEnabled);
    
    if (_lastChargingEnabled && !canCharge) {
        _lastChargingStopTime = millis();
    }
    _lastChargingEnabled = canCharge;

    if (canCharge && snap.terminalVolt > 56.0f && snap.terminalCurr > 0.0f) {
        unsigned long now = millis();
        float dt_hours = (now - _lastEnergyTime) / 3600000.0f;
        float energyDelta = snap.terminalVolt * snap.terminalCurr * dt_hours;

        if (energyDelta > 0.0f && energyDelta < 1000.0f) {
            // SystemState handles energy accumulation — no legacy global needed
            SystemState::instance().addEnergyWh(energyDelta);
        }
        _lastEnergyTime = now;
    } else {
        _lastEnergyTime = millis();
    }
}

// ═══════════════════════════════════════════════════════════════
// 4. CHARGER HEALTH
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollChargerHealth() {
    if (millis() - _lastChargerHealthCheck < 2000)
        return;

    bool chargerHealthy = isChargerModuleHealthy();
    if (!_firstHealthCheck && chargerHealthy != _lastChargerHealthy) {
        if (!chargerHealthy) {
            Serial.println("[CHARGER] ❌ CRITICAL: Communication lost");
            // PHASE 2: Removed sendSystemAlert — library addErrorDataInput("PowerSwitchFailure") handles StatusNotification
        } else {
            Serial.println("[CHARGER] ✅ Communication restored");
        }
        _lastChargerHealthy = chargerHealthy;
    }
    _firstHealthCheck = false;
    _lastChargerHealthCheck = millis();
}

// ═══════════════════════════════════════════════════════════════
// 5. FAULT LOCK
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollFaultLock(const StateSnapshot& snap) {
    if (!snap.faultLockActive) return;
    if ((millis() - snap.faultLockTime) < FAULT_STABILIZATION_PERIOD_MS) return;

    // CRITICAL: Do not clear fault lock if E-Stop is still physically pushed
    if (_estopActive) return;

    if (snap.bmsSafeToCharge && snap.terminalVolt >= ALERT_VOLTAGE_MIN_V && snap.chargerTemp <= ALERT_TEMP_CRITICAL_C) {
        Serial.println("[FAULT] ✅ Stability recovered, lock cleared");
        SystemState::instance().setFaultLockActive(false);
    }
}

// ═══════════════════════════════════════════════════════════════
// 6. WIFI MONITOR
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollWiFiMonitor() {
    bool networkConnected = g_networkManager.isConnected();
    auto snap = SystemState::instance().snapshot();

    if (networkConnected) {
        _lastNetworkTime = millis();
        _commLossTriggered = false; // Reset when network is back
    } else if (snap.transactionActive && !_commLossTriggered) {
        // Only trigger safety stop if we are actively charging
        if (millis() - _lastNetworkTime > COMM_LOSS_TIMEOUT_MS) {
            Serial.printf("[SAFETY] 🚨 COMM LOSS: Emergency Stop triggered after %ds outage during transaction\n", 
                          (int)(COMM_LOSS_TIMEOUT_MS / 1000));
            
            // 1. Inform OCPP system (will be buffered in Flash)
            ocpp::sendSystemAlert("COMMUNICATION_LOST", "Charging stopped due to 30s network outage", "Critical");
            
            // 2. End transaction locally
            ocpp::endTransactionSafe(nullptr, "Local");
            
            // 3. Prevent multiple triggers for the same outage
            _commLossTriggered = true;
        }
    }

    if (networkConnected != _lastWifiConnected) {
        _lastWifiConnected = networkConnected;
    }
}

// ═══════════════════════════════════════════════════════════════
// 7. PRE-TRANSACTION VEHICLE INFO
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollVehicleInfo(const StateSnapshot& snap) {
    bool shouldSendVehicleInfo = (
        snap.batteryConnected &&
        snap.gunPhysicallyConnected &&
        !ocpp::isTransactionRunningSafe(1) &&
        snap.BMS_Imax > 0.0f &&
        snap.terminalVolt > 56.0f &&
        snap.socPercent > 0.0f
    );

    if (millis() - _lastVehicleDiag > 30000 && snap.gunPhysicallyConnected) {
        _lastVehicleDiag = millis();
        Serial.printf("[VEHICLE_DIAG] shouldSend=%d (Imax=%.1f V=%.1f SOC=%.1f)\n",
                      shouldSendVehicleInfo, snap.BMS_Imax, snap.terminalVolt, snap.socPercent);
    }

    if (shouldSendVehicleInfo) {
        bool socChanged = (_lastSentSoc >= 0.0f && fabsf(snap.socPercent - _lastSentSoc) >= 1.0f);
        unsigned long interval = _firstSendDone ? 300000 : 3000;

        if (millis() - _lastVehicleInfoSent >= interval || socChanged) {
            if (::isOperative()) {
                ocpp::sendVehicleInfo(snap.socPercent, snap.BMS_Imax, snap.terminalVolt, snap.terminalCurr, snap.chargerTemp, snap.vehicleModel, snap.rangeKm);
                _lastVehicleInfoSent = millis();
                _lastSentSoc = snap.socPercent;
                _firstSendDone = true;
            }
        }

        if (millis() - _lastChargerStatusSent >= 5000) {
            if (!isChargerModuleHealthy()) {
                ocpp::sendChargerStatus(false, "Charger module offline");
                _lastChargerStatusSent = millis();
            } else if (!snap.bmsSafeToCharge) {
                ocpp::sendChargerStatus(false, "Vehicle not ready");
                _lastChargerStatusSent = millis();
            }
        }
    } else {
        if (snap.transactionActive || ocpp::isTransactionRunningSafe(1) || !snap.batteryConnected) {
            _lastVehicleInfoSent = 0;
            _firstSendDone = false;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// 8. POST-TRANSACTION VEHICLE INFO
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollPostTxVehicleInfo(const StateSnapshot& snap) {
    // Current logic: Send info periodically while vehicle is plugged but not charging after a session
    // Problem: It spams if conditions flicker. Changed to use a more robust timer.

    bool transJustFinished = (snap.txStopTime > 0 && !snap.transactionActive);
    
    bool conditionsMet = (
        transJustFinished &&
        !ocpp::isTransactionRunningSafe(1) &&
        snap.gunPhysicallyConnected &&
        snap.batteryConnected &&
        snap.terminalVolt > 56.0f &&
        snap.BMS_Imax > 0.0f
    );

    if (conditionsMet) {
        // Only send if it's the first time after stop or 5 mins have passed
        if (_lastPostTxVehicleInfo == 0 || (millis() - _lastPostTxVehicleInfo >= 300000)) {
            Serial.printf("[OCPP] 📊 Sending Post-Tx VehicleInfo: SOC=%.1f%% Energy=%.2fWh\n", 
                         snap.socPercent, snap.energyWh);
            ocpp::sendVehicleInfo(snap.socPercent, snap.BMS_Imax, snap.terminalVolt, snap.terminalCurr, snap.chargerTemp, snap.vehicleModel, snap.rangeKm);
            _lastPostTxVehicleInfo = millis();
        }
    } else if (!snap.gunPhysicallyConnected) {
        // Reset timer only when truly unplugged so it's ready for next session
        _lastPostTxVehicleInfo = 0;
    }
}

// ═══════════════════════════════════════════════════════════════
// 9. STATUS LEDS (D13 & D15)
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollStatusLEDs() {
    unsigned long now = millis();
    
    // Check if we need to toggle blinking state (every 500ms)
    bool blinkToggle = false;
    if (now - _lastLedBlinkTime >= 500) {
        _chargerLedState = !_chargerLedState;
        _networkLedState = !_networkLedState;
        _lastLedBlinkTime = now;
        blinkToggle = true;
    }

    // --- 1. LED_NETWORK_STATUS (D15 - Blue/White) ---
    // Steady ON if GSM/WiFi has no internet.
    // Blinking if connected and online.
    if (g_networkManager.isConnected()) {
        if (blinkToggle) digitalWrite(LED_NETWORK_STATUS, _networkLedState ? HIGH : LOW);
    } else {
        digitalWrite(LED_NETWORK_STATUS, HIGH); // Steady ON = No network
    }

    // --- 2. LED_CHARGER_STATUS (D13 - Green/Yellow) ---
    // Steady ON if Available or Finishing.
    // Blinking if Charging.
    // OFF if Faulted.
    // PHASE 1: Use library's authoritative status instead of custom state machine
    ChargePointStatus libStatus = getChargePointStatus(1);
    
    if (libStatus == ChargePointStatus_Charging) {
        // Blinking = Charging
        if (blinkToggle) digitalWrite(LED_CHARGER_STATUS, _chargerLedState ? HIGH : LOW);
    } 
    else if (libStatus == ChargePointStatus_Available || 
             libStatus == ChargePointStatus_Finishing || 
             libStatus == ChargePointStatus_Preparing) {
        // Steady ON = Available/Ready
        digitalWrite(LED_CHARGER_STATUS, HIGH);
    } 
    else if (libStatus == ChargePointStatus_Faulted || !isChargerModuleHealthy()) {
        // OFF = Faulted/Unavailable
        digitalWrite(LED_CHARGER_STATUS, LOW);
    }
    else {
        // Default (Suspended, etc) -> Keep steady ON
        digitalWrite(LED_CHARGER_STATUS, HIGH);
    }

}
