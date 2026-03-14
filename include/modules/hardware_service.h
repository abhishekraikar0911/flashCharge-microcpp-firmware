#pragma once
/**
 * @file hardware_service.h
 * @brief Extracts all hardware monitoring logic from main.cpp loop().
 *
 * Responsibilities:
 *   - Plug detection (BMS timeout, voltage-drop rate)
 *   - Safety monitoring (temperature, voltage, current limits)
 *   - Energy accumulation (V * I * dt integration)
 *   - Charger health monitoring
 *   - Fault lock management
 */

#include <Arduino.h>
#include "modules/system_state.h"

class HardwareService {
public:
    void begin();   // Called once in setup()
    void poll();    // Called every loop() iteration

private:
    // Sub-systems (each maps to a block previously in main.cpp loop())
    void pollPlugDetection(const StateSnapshot& snap);
    void pollSafetyMonitor(const StateSnapshot& snap);
    void pollEnergyAccumulation(const StateSnapshot& snap);
    void pollChargerHealth();
    void pollFaultLock(const StateSnapshot& snap);
    void pollWiFiMonitor();
    void pollVehicleInfo(const struct StateSnapshot& snap);
    void pollPostTxVehicleInfo(const struct StateSnapshot& snap);

    // ── Plug Detection State ──
    unsigned long _lastPlugCheck     = 0;
    float _lastVoltageCheck          = 0.0f;
    unsigned long _lastVoltageTime   = 0;
    bool _canRecoveryActive          = false;
    bool _lastPlugState              = false;

    // ── Safety Monitor State ──
    bool _lastBmsSafe                = false;
    unsigned long _lastBmsSafetyCheck= 0;
    bool _tempWarningActive          = false;
    bool _tempCriticalActive         = false;
    bool _voltageAlertActive         = false;
    bool _currentAlertActive         = false;

    // ── Energy Accumulation ──
    unsigned long _lastEnergyTime    = 0;
    bool _lastChargingEnabled        = false;
    unsigned long _lastChargingStopTime = 0;

    // ── Charger Health ──
    unsigned long _lastChargerHealthCheck = 0;
    bool _lastChargerHealthy         = false;
    bool _firstHealthCheck           = true;

    // ── WiFi ──
    bool _lastWifiConnected          = true;

    // ── VehicleInfo ──
    unsigned long _lastVehicleInfoSent   = 0;
    bool _firstSendDone                  = false;
    unsigned long _lastChargerStatusSent = 0;
    unsigned long _lastVehicleDiag       = 0;
    float _lastSentSoc                   = -1.0f;

    // ── Post-Tx VehicleInfo ──
    unsigned long _lastPostTxVehicleInfo = 0;
    bool _sessionEverCompleted           = false;

    // ── Debug ──
    unsigned long _lastDebug = 0;
};

// Global instance
extern HardwareService g_hardwareService;
