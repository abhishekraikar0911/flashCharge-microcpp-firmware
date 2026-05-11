#include "system/ChargerService.h"
#include "app/AppContext.h"
#include "services/OcppClient.h"

namespace prod {

void ChargerService::begin() {
    _lastChargingStopTime = 0;
    if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "CHARGER_SVC", "Started");
}

void ChargerService::resetDynamicLimits() {
    _lastSentImax = -1.0f;
    _lastSentVmax = -1.0f;
    if (g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "CHARGER_SVC", "Dynamic limit tracking reset for new session");
}

void ChargerService::poll() {
    uint32_t now = g_app.timer ? g_app.timer->millis() : 0;
    auto snap = SystemState::instance().snapshot(); // Take snapshot once at top
    static uint32_t lastDiagLog = 0;
    bool shouldLog = (now - lastDiagLog > 5000);
    if (shouldLog) {
        lastDiagLog = now;
        uint32_t bmsAge = (snap.lastBMS > 0) ? (now - snap.lastBMS) : 99999u;
        int canOk = (int)(snap.gunPhysicallyConnected || snap.batteryConnected);
        int dcOk = (int)(snap.terminalVolt >= 50.0f);
        // During charging, add Energy so the log is self-sufficient (no need for separate TELEM)
        float displaySoc = canOk ? snap.socPercent : 0.0f;
        if (snap.transactionActive) {
            if (g_app.logger) g_app.logger->logf(ILogger::Level::INFO, "SVC_HEARTBEAT",
                "V=%.1fV I=%.1fA SOC=%.1f%% Energy=%.2fWh | BmsAge=%lums CanOk=%d DcOk=%d BmsSafe=%d ChgReady=%d Fault=%d Tx=%d",
                snap.terminalVolt, snap.terminalCurr, displaySoc, snap.energyWh, bmsAge,
                canOk, dcOk,
                (int)snap.bmsSafeToCharge,
                (g_app.charger ? (int)g_app.charger->isReady() : -1),
                (int)snap.faultLockActive,
                (int)snap.transactionActive);
        } else {
            if (g_app.logger) g_app.logger->logf(ILogger::Level::INFO, "SVC_HEARTBEAT",
                "V=%.1fV I=%.1fA SOC=%.1f%% | BmsAge=%lums CanOk=%d DcOk=%d BmsSafe=%d ChgReady=%d Fault=%d Tx=%d",
                snap.terminalVolt, snap.terminalCurr, displaySoc, bmsAge,
                canOk, dcOk,
                (int)snap.bmsSafeToCharge,
                (g_app.charger ? (int)g_app.charger->isReady() : -1),
                (int)snap.faultLockActive,
                (int)snap.transactionActive);
        }
    }

    // 1. Tick HAL Drivers to process incoming CAN strings
    if (g_app.charger) g_app.charger->update();
    if (g_app.bms)     g_app.bms->update();

    // 1.5 Push System flags to BMS driver for heartbeat
    if (g_app.bms) {
        uint8_t flags = 0;
        // Bit 0 = Hardware failure: module is offline only if it isn't online AND isn't actively
        // delivering voltage (active voltage proves the module is physically working).
        bool moduleOnline = snap.chargerModuleOnline || (snap.terminalVolt > 10.0f);
        if (!moduleOnline) flags |= 0x01;
        if (snap.chargerTemp > 70.0f)  flags |= 0x02; // Over-temperature
        if (!snap.batteryConnected)    flags |= 0x08; // Not connected
        if (snap.lastBMS > 0 && (now - snap.lastBMS) > 5000) flags |= 0x10; // BMS timeout
        g_app.bms->updateSystemStatus(snap.terminalVolt, snap.terminalCurr, flags);
    }

    // 2. Sync BMS data to SystemState (replaces legacy bms_interface logic)
    if (g_app.bms && g_app.bms->isConnected()) {
        auto& state = SystemState::instance();
        state.setBatteryConnected(true);
        state.setGunPhysicallyConnected(true); // BMS CAN frames = physical gun proof
        state.setBMS_Vmax(g_app.bms->getPackVoltage());
        state.setBMS_Imax(g_app.bms->getMaxChargeCurrent());
        state.setSocPercent(g_app.bms->getSoc());
        state.setBmsSafeToCharge(g_app.bms->isSafeToCharge());
        state.setChargingSwitch(g_app.bms->isSafeToCharge());
        state.setLastBMS(now);

        // Dynamically update charger limits if charging is actively running
        if (state.getTransactionActive() && g_app.charger) {
            float newVmax = state.getBMS_Vmax();
            float newImax = state.getBMS_Imax();

            if (newVmax > 10.0f && newImax >= 0.0f &&
                (fabsf(newVmax - _lastSentVmax) >= 0.1f || fabsf(newImax - _lastSentImax) >= 0.1f)) {

                g_app.charger->updateLimits(newVmax, newImax);
                _lastSentVmax = newVmax;
                _lastSentImax = newImax;

                if (g_app.logger) g_app.logger->logf(ILogger::Level::INFO, "CHARGER_SVC", "Dynamic limit update: Vmax=%.1fV Imax=%.1fA", newVmax, newImax);
            }
        }
    }

    // 3. Sync Charger telemetry to SystemState
    if (g_app.charger && g_app.charger->isReady()) {
        float v, i, t;
        if (g_app.charger->getTelemetry(v, i, t)) {
            auto& state = SystemState::instance();
            state.setChargerVolt(v);
            state.setChargerCurr(i);
            state.setChargerTemp(t);
            state.setLastChargerResp(now);
            
            // Sync terminal values for UI/OCPP
            state.setTerminalVolt(v);
            state.setTerminalCurr(i);

            // TELEM removed — all data is in SVC_HEARTBEAT (V, I, SOC, Energy)
        }
    }

    snap = SystemState::instance().snapshot(); // Update snap with new BMS/Charger data
    pollPlugDetection(snap);
    pollChargerHealth();
    pollVehicleInfo(snap);
}

// Assuming SafetyService::poll() is intended for a different file or class definition.
// As per the instruction, I am only modifying the provided document.
// The instruction asks to add logs to poll() methods of ChargerService, SafetyService, EnergyService, and NetworkService.
// This document only contains ChargerService. ChargerService::poll() already has a log.
// If SafetyService::poll() was meant to be added to this file, it would require a class definition.
// For now, I will assume the user's snippet for SafetyService::poll() is an example for other files.

void ChargerService::pollPlugDetection(const StateSnapshot& snap) {
    uint32_t now = g_app.timer ? g_app.timer->millis() : 0;
    if (now - _lastPlugCheck < 500) return;

    bool shouldDisconnect = false;

    // Method 1: BMS frame timeout
    // Threshold: 8s (increased from 3s).
    // Rationale: BMS may pause CAN frames for 2-5s during:
    //   - Cell balancing at high SOC
    //   - Internal protection evaluation
    //   - CAN bus glitch recovery
    // A 3s timeout was triggering false PLUG_DISCONNECT at 93%+ SOC mid-session.
    uint32_t bmsAge = (snap.lastBMS > 0) ? (now - snap.lastBMS) : 0;
    if ((snap.gunPhysicallyConnected || snap.batteryConnected) && bmsAge > 8000) {
        if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
            "[BMS_TIMEOUT_8S] lastBMS=%lums ago | V=%.1fV I=%.1fA | BmsConn=%d BmsSafe=%d | Tx=%d Fault=%d",
            bmsAge, snap.terminalVolt, snap.terminalCurr,
            (int)snap.batteryConnected, (int)snap.bmsSafeToCharge,
            (int)snap.transactionActive, (int)snap.faultLockActive);

        if (snap.terminalVolt < 50.0f) {
            // Voltage confirms disconnect
            shouldDisconnect = true;
        } else if (!snap.transactionActive) {
            // No active transaction: BMS silent > 8s = gun is being removed.
            // The charger module (CAN1) keeps reporting voltage even after gun removal
            // so we cannot rely on voltage alone — mark as disconnected now.
            shouldDisconnect = true;
            if (g_app.logger) g_app.logger->logf(ILogger::Level::INFO, "PLUG",
                "BMS silent %lums (no active tx) — marking disconnected (V=%.1fV held by charger module)", bmsAge, snap.terminalVolt);
        } else {
            // Transaction active: BMS may be recovering (cell balancing at high SOC)
            // Keep waiting but keep logging.
            if (g_app.logger) g_app.logger->logf(ILogger::Level::WARN, "PLUG",
                "BMS silent %lums but V=%.1fV — charger still connected, waiting for BMS recovery", bmsAge, snap.terminalVolt);
        }
    }

    // Method 2: Voltage drop rate (>2V/s)
    bool bmsActive = (now - snap.lastBMS < 5000);
    bool chargingJustStopped = (now - _lastChargingStopTime < 10000);

    if (snap.terminalVolt > 10.0f && !bmsActive && !chargingJustStopped) {
        if (_lastVoltageTime > 0) {
            float deltaV = _lastVoltageCheck - snap.terminalVolt;
            float deltaT = (now - _lastVoltageTime) / 1000.0f;
            if (deltaT > 0.5f && (deltaV / deltaT) > 2.0f) {
                if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
                    "[VOLT_DROP] %.1fV/s | V=%.1f->%.1f | BmsAge=%lums | Tx=%d Fault=%d",
                    deltaV / deltaT, _lastVoltageCheck, snap.terminalVolt,
                    (now - snap.lastBMS), (int)snap.transactionActive, (int)snap.faultLockActive);
                shouldDisconnect = true;
            }
        }
        _lastVoltageCheck = snap.terminalVolt;
        _lastVoltageTime = now;
    } else {
        _lastVoltageCheck = 0.0f;
        _lastVoltageTime = 0;
    }

    if (shouldDisconnect && (snap.gunPhysicallyConnected || snap.batteryConnected)) {
        SystemState::instance().setGunPhysicallyConnected(false);
        SystemState::instance().setBatteryConnected(false);
        if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "PLUG", "Status: DISCONNECTED");

        if (snap.transactionActive && ocpp::isTransactionRunningSafe(1)) {
            if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "STOP_TRIGGER",
                "[PLUG_DISCONNECT] calling endTransaction EVDisconnected | txId=%d V=%.1fV I=%.1fA BmsAge=%lums BmsSafe=%d ChgReady=%d",
                SystemState::instance().getActiveTransactionId(),
                snap.terminalVolt, snap.terminalCurr,
                (snap.lastBMS > 0 ? now - snap.lastBMS : 99999u),
                (int)snap.bmsSafeToCharge,
                (g_app.charger ? (int)g_app.charger->isReady() : -1));
            ocpp::endTransactionSafe(nullptr, "EVDisconnected");
        }
    }

    bool currentPlugState = (snap.gunPhysicallyConnected && snap.batteryConnected);
    if (currentPlugState != _lastPlugState) {
        if (currentPlugState && g_app.logger) g_app.logger->log(ILogger::Level::INFO, "PLUG", "Gun plugged, vehicle detected");
        _lastPlugState = currentPlugState;
    }

    _lastPlugCheck = now;
}

void ChargerService::pollChargerHealth() {
    uint32_t now = g_app.timer ? g_app.timer->millis() : 0;
    if (now - _lastChargerHealthCheck < 2000) return;
    _lastChargerHealthCheck = now;

    bool healthy = g_app.charger ? (g_app.charger->isReady() && !g_app.charger->hasFault()) : false;

    if (healthy != _chargerHealthy) {
        if (g_app.logger) g_app.logger->logf(ILogger::Level::INFO, "CHARGER_SVC", "%s", healthy ? "Restored" : "Lost");
        _chargerHealthy = healthy;
        SystemState::instance().setChargerModuleOnline(healthy);
    }

    // Periodic debug if unhealthy (every 5s)
    static unsigned long lastDebugPrint = 0;
    if (!healthy && (now - lastDebugPrint > 5000)) {
        lastDebugPrint = now;
        if (g_app.charger) {
            auto snap = SystemState::instance().snapshot();
            if (g_app.logger) g_app.logger->logf(ILogger::Level::DEBUG, "CHARGER_SVC", "DRV_DEBUG: Ready=%d Fault=%d | Age: BMS=%lums Chg=%lums", 
                          g_app.charger->isReady(), g_app.charger->hasFault(),
                          (snap.lastBMS > 0 ? now - snap.lastBMS : 99999),
                          (snap.lastChargerResp > 0 ? now - snap.lastChargerResp : 99999));
        } else {
            if (g_app.logger) g_app.logger->log(ILogger::Level::ERROR, "CHARGER_SVC", "DRV_DEBUG: g_app.charger is NULL!");
        }
    }
}

void ChargerService::pollVehicleInfo(const StateSnapshot& snap) {
    // Moved to OcppService::poll() for state-based triggers 
    // (Preparing, Charging, Finishing) to avoid mid-charge spam.
}

} // namespace prod
