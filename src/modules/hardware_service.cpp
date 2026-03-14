/**
 * @file hardware_service.cpp
 * @brief Implementation of HardwareService — extracted from main.cpp loop().
 */

#include "../include/modules/hardware_service.h"
#include "../include/header.h"
#include "../include/config/hardware.h"
#include "../include/ocpp/ocpp_client.h"
#include "../include/ocpp_state_machine.h"
#include "../include/wifi_manager.h"
#include "../include/modules/network_manager.h"
#include "../include/health_monitor.h"
#include "../include/modules/system_state.h"

using namespace prod;

HardwareService g_hardwareService;

// ═══════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════

void HardwareService::begin() {
    _lastEnergyTime = millis();
    Serial.println("[HW_SVC] Hardware monitoring service started");
}

void HardwareService::poll() {
    auto snap = SystemState::instance().snapshot();
    pollPlugDetection(snap);
    pollSafetyMonitor(snap);
    pollEnergyAccumulation(snap);
    pollChargerHealth();
    pollFaultLock(snap);
    pollWiFiMonitor();
    pollVehicleInfo(snap);
    pollPostTxVehicleInfo(snap);
}

// ═══════════════════════════════════════════════════════════════
// 1. PLUG DETECTION (Hybrid — BMS timeout + Voltage drop)
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollPlugDetection(const StateSnapshot& snap) {
    if (millis() - _lastPlugCheck < 500)
        return;

    bool shouldDisconnect = false;

    // Method 1: BMS timeout (3 seconds) — most reliable
    if ((snap.gunPhysicallyConnected || snap.batteryConnected) && (millis() - lastBMS > 3000)) {
        Serial.println("[PLUG] 🔌 Disconnected: BMS timeout (3s)");
        shouldDisconnect = true;
    }

    // Method 3: Voltage drop rate (>2V/s) 
    bool bmsActive = (millis() - lastBMS < 5000);
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
        gunPhysicallyConnected = false;
        batteryConnected = false;
        Serial.println("[PLUG] ✅ Status: DISCONNECTED");

        if (transactionActive && ocpp::isTransactionRunningSafe(1)) {
            Serial.printf("[PLUG] 🛑 Stopping transaction due to EV disconnect (txId=%d)\n", activeTransactionId);
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
// 2. SAFETY MONITOR (BMS, Temperature, Voltage, Current)
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollSafetyMonitor(const StateSnapshot& snap) {
    // --- BMS Safety ---
    if (millis() - _lastBmsSafetyCheck >= 100) {
        if (snap.bmsSafeToCharge != _lastBmsSafe) {
            if (!snap.bmsSafeToCharge) {
                Serial.printf("[SAFETY] 🚨 BMS disabled charging (bmsSafeToCharge=%d)\n", snap.bmsSafeToCharge);
                if (transactionActive && ocpp::isTransactionRunningSafe(1)) {
                    faultLockActive = true;
                    faultLockTime = millis();
                    ocpp::sendBMSAlert("BMS_EMERGENCY_STOP", "BMS disabled charging during transaction");
                    ocpp::endTransactionSafe(nullptr, "EmergencyStop");
                } else {
                    ocpp::sendBMSAlert("BMS_CHARGING_DISABLED", "BMS not ready for charging");
                }
            } else {
                Serial.println("[SAFETY] ✅ BMS charging enabled");
                ocpp::sendBMSAlert("BMS_CHARGING_ENABLED", "BMS ready for charging");
            }
            _lastBmsSafe = snap.bmsSafeToCharge;
        }
        _lastBmsSafetyCheck = millis();
    }

    // --- Temperature ---
    if (snap.chargerTemp > ALERT_TEMP_CRITICAL_C && !_tempCriticalActive) {
        Serial.printf("[TEMP] 🚨 CRITICAL overheat: %.1f°C\n", snap.chargerTemp);
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            chargingEnabled = false;
            // Update SystemState too
            SystemState::instance().setChargingEnabled(false);
            xSemaphoreGive(dataMutex);
        }
        sendImmediateChargerStop();
        ocpp::sendSystemAlert("TEMPERATURE_CRITICAL", "Overheat detected", "Critical");

        if (transactionActive && ocpp::isTransactionRunningSafe(1)) {
            faultLockActive = true;
            faultLockTime = millis();
            ocpp::endTransactionSafe(nullptr, "EmergencyStop");
        }
        _tempCriticalActive = true;
    } else if (snap.chargerTemp < (ALERT_TEMP_CRITICAL_C - 10.0f) && _tempCriticalActive) {
        Serial.printf("[TEMP] ✅ Temperature normalized: %.1f°C\n", snap.chargerTemp);
        _tempCriticalActive = false;
    }

    // --- Voltage/Current (Summary) ---
    if (snap.terminalVolt > 0.0f && snap.batteryConnected) {
        if ((snap.terminalVolt > ALERT_VOLTAGE_MAX_V || snap.terminalVolt < ALERT_VOLTAGE_MIN_V) && !_voltageAlertActive) {
            Serial.printf("[VOLTAGE] 🚨 Fault: %.1fV\n", snap.terminalVolt);
            if (transactionActive && ocpp::isTransactionRunningSafe(1)) {
                faultLockActive = true;
                faultLockTime = millis();
                ocpp::endTransactionSafe(nullptr, "EmergencyStop");
            }
            _voltageAlertActive = true;
        } else if (snap.terminalVolt >= MIN_VOLTAGE_V && snap.terminalVolt <= MAX_VOLTAGE_V && _voltageAlertActive) {
            _voltageAlertActive = false;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// 3. ENERGY ACCUMULATION
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollEnergyAccumulation(const StateSnapshot& snap) {
    bool canCharge = (ocpp::ocppPermitsChargeSafe(1) && transactionActive && snap.chargingEnabled);
    
    if (_lastChargingEnabled && !canCharge) {
        _lastChargingStopTime = millis();
    }
    _lastChargingEnabled = canCharge;

    if (canCharge && snap.terminalVolt > 56.0f && snap.terminalCurr > 0.0f) {
        unsigned long now = millis();
        float dt_hours = (now - _lastEnergyTime) / 3600000.0f;
        float energyDelta = snap.terminalVolt * snap.terminalCurr * dt_hours;

        if (energyDelta > 0.0f && energyDelta < 1000.0f) {
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                energyWh += energyDelta;
                xSemaphoreGive(dataMutex);
            }
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
            ocpp::sendSystemAlert("CHARGER_OFFLINE", "CAN communication timeout", "Critical");
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
    if (!faultLockActive) return;
    if ((millis() - faultLockTime) < FAULT_STABILIZATION_PERIOD_MS) return;

    if (snap.bmsSafeToCharge && snap.terminalVolt >= ALERT_VOLTAGE_MIN_V && snap.chargerTemp <= ALERT_TEMP_CRITICAL_C) {
        Serial.println("[FAULT] ✅ Stability recovered, lock cleared");
        faultLockActive = false;
    }
}

// ═══════════════════════════════════════════════════════════════
// 6. WIFI MONITOR
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollWiFiMonitor() {
    bool networkConnected = g_networkManager.isConnected();
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

    if (millis() - _lastVehicleDiag > 10000 && snap.gunPhysicallyConnected) {
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
        if (transactionActive || ocpp::isTransactionRunningSafe(1) || !batteryConnected) {
            _lastVehicleInfoSent = 0;
            _firstSendDone = false;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// 8. POST-TRANSACTION VEHICLE INFO
// ═══════════════════════════════════════════════════════════════

void HardwareService::pollPostTxVehicleInfo(const StateSnapshot& snap) {
    if (txStopTime > 0 && !transactionActive) {
        _sessionEverCompleted = true;
    }

    bool shouldSend = (
        _sessionEverCompleted &&
        !snap.transactionActive &&
        !ocpp::isTransactionRunningSafe(1) &&
        snap.gunPhysicallyConnected &&
        snap.batteryConnected &&
        snap.terminalVolt > 56.0f &&
        snap.BMS_Imax > 0.0f &&
        snap.socPercent > 0.0f
    );

    if (shouldSend) {
        if (millis() - _lastPostTxVehicleInfo >= 300000) {
            Serial.printf("[OCPP] 📊 Sending Post-Tx VehicleInfo: SOC=%.1f%% Energy=%.2fWh\n", 
                         snap.socPercent, snap.energyWh);
            ocpp::sendVehicleInfo(snap.socPercent, snap.BMS_Imax, snap.terminalVolt, snap.terminalCurr, snap.chargerTemp, snap.vehicleModel, snap.rangeKm);
            _lastPostTxVehicleInfo = millis();
        }
    } else {
        _lastPostTxVehicleInfo = 0;
    }
}
