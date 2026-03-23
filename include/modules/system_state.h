#pragma once
/**
 * @file system_state.h
 * @brief Centralized, thread-safe system state for the ESP32 OCPP EVSE Controller.
 *
 * Replaces the scattered global variables previously declared in header.h.
 * All fields are accessed through getter/setter methods that acquire a mutex,
 * ensuring safe cross-task access (CAN RX tasks on Core 1, OCPP task on Core 0).
 *
 * Usage:
 *   #include "modules/system_state.h"
 *   auto& state = SystemState::instance();
 *   state.setTerminalVolt(76.8f);
 *   float v = state.getTerminalVolt();
 *
 * For bulk reads (e.g., building a MeterValue), use snapshot():
 *   auto snap = state.snapshot();
 *   Serial.printf("V=%.1f I=%.1f SOC=%.1f%%\n", snap.terminalVolt, snap.terminalCurr, snap.socPercent);
 */

#include <Arduino.h>
#include <freertos/semphr.h>

// ═══════════════════════════════════════════════════════════════
// Snapshot — a plain struct copied out under a single mutex lock
// ═══════════════════════════════════════════════════════════════
struct StateSnapshot {
    // ── Charger Module (CAN1) ──
    float terminalVolt      = 0.0f;
    float terminalCurr      = 0.0f;
    float chargerVolt       = 0.0f;
    float chargerCurr       = 0.0f;
    float chargerTemp       = 0.0f;
    float terminalPower     = 0.0f;

    // ── BMS (CAN2) ──
    float socPercent        = 0.0f;
    float rangeKm           = 0.0f;
    float batteryAh         = 0.0f;
    float BMS_Vmax          = 0.0f;
    float BMS_Imax          = 0.0f;
    float Charger_Vmax      = 0.0f;
    float Charger_Imax      = 0.0f;
    float batterySoc        = 0.0f;
    float totalChargingAh   = 0.0f;
    float totalDischargingAh= 0.0f;
    uint8_t vehicleModel    = 0;    // 0=Unknown, 1=Classic, 2=Pro, 3=Max

    // ── Connection / Plug ──
    bool batteryConnected       = false;
    bool gunPhysicallyConnected = false;
    bool vehicleConfirmed       = false;

    // ── BMS Safety ──
    bool bmsSafeToCharge    = false;
    bool bmsHeatingActive   = false;
    bool chargingSwitch     = false;
    uint8_t heating         = 0;

    // ── Charging ──
    bool chargingEnabled    = false;
    float energyWh          = 0.0f;

    // ── Transaction ──
    bool transactionActive      = false;
    int  activeTransactionId    = -1;
    bool remoteStartAccepted    = false;
    bool sessionActive          = false;
    unsigned long txStartTime   = 0;
    unsigned long txStopTime    = 0;

    // ── Fault ──
    bool faultLockActive        = false;
    unsigned long faultLockTime = 0;

    // ── Charger Health ──
    bool chargerModuleOnline    = false;

    // ── Timestamps ──
    unsigned long lastBMS           = 0;
    unsigned long lastHeartbeat     = 0;
    unsigned long lastChargerResp   = 0;
    unsigned long lastTerminalPower = 0;
    unsigned long lastTerminalStatus= 0;

    // ── OCPP ──
    bool ocppInitialized   = false;
};

// ═══════════════════════════════════════════════════════════════
// SystemState Singleton — thread-safe wrapper around StateSnapshot
// All getters/setters operate exclusively on the internal _data struct.
// No extern globals are referenced — SystemState is the single source of truth.
// ═══════════════════════════════════════════════════════════════
class SystemState {
public:
    static SystemState& instance() {
        static SystemState s;
        return s;
    }

    // ── Bulk snapshot (single lock) ──
    StateSnapshot snapshot() {
        StateSnapshot copy;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            copy = _data;
            xSemaphoreGive(_mutex);
        }
        return copy;
    }

    // ═══════════════════════════════════════════════════════════
    // Individual Getters / Setters (auto-lock)
    // Single source of truth — no extern global bridges.
    // ═══════════════════════════════════════════════════════════

    // --- Charger Module ---
    float getTerminalVolt()  { Lock l(_mutex); return _data.terminalVolt; }
    void  setTerminalVolt(float v) { Lock l(_mutex); _data.terminalVolt = v; }

    float getTerminalCurr()  { Lock l(_mutex); return _data.terminalCurr; }
    void  setTerminalCurr(float v) { Lock l(_mutex); _data.terminalCurr = v; }

    float getChargerTemp()   { Lock l(_mutex); return _data.chargerTemp; }
    void  setChargerTemp(float v) { Lock l(_mutex); _data.chargerTemp = v; }

    float getTerminalPower() { Lock l(_mutex); return _data.terminalPower; }
    void  setTerminalPower(float v) { Lock l(_mutex); _data.terminalPower = v; }

    float getChargerVolt()   { Lock l(_mutex); return _data.chargerVolt; }
    void  setChargerVolt(float v) { Lock l(_mutex); _data.chargerVolt = v; }

    float getChargerCurr()   { Lock l(_mutex); return _data.chargerCurr; }
    void  setChargerCurr(float v) { Lock l(_mutex); _data.chargerCurr = v; }

    // --- BMS ---
    float getSocPercent()    { Lock l(_mutex); return _data.socPercent; }
    void  setSocPercent(float v) { Lock l(_mutex); _data.socPercent = v; }

    float getRangeKm()       { Lock l(_mutex); return _data.rangeKm; }
    void  setRangeKm(float v) { Lock l(_mutex); _data.rangeKm = v; }

    float getBatteryAh()     { Lock l(_mutex); return _data.batteryAh; }
    void  setBatteryAh(float v) { Lock l(_mutex); _data.batteryAh = v; }

    float getBMS_Vmax()      { Lock l(_mutex); return _data.BMS_Vmax; }
    void  setBMS_Vmax(float v) { Lock l(_mutex); _data.BMS_Vmax = v; }

    float getBMS_Imax()      { Lock l(_mutex); return _data.BMS_Imax; }
    void  setBMS_Imax(float v) { Lock l(_mutex); _data.BMS_Imax = v; }

    float getCharger_Vmax()  { Lock l(_mutex); return _data.Charger_Vmax; }
    void  setCharger_Vmax(float v) { Lock l(_mutex); _data.Charger_Vmax = v; }

    float getCharger_Imax()  { Lock l(_mutex); return _data.Charger_Imax; }
    void  setCharger_Imax(float v) { Lock l(_mutex); _data.Charger_Imax = v; }

    uint8_t getVehicleModel() { Lock l(_mutex); return _data.vehicleModel; }
    void    setVehicleModel(uint8_t v) { Lock l(_mutex); _data.vehicleModel = v; }

    float getBatterySoc()    { Lock l(_mutex); return _data.batterySoc; }
    void  setBatterySoc(float v) { Lock l(_mutex); _data.batterySoc = v; }

    float getTotalChargingAh() { Lock l(_mutex); return _data.totalChargingAh; }
    void  setTotalChargingAh(float v) { Lock l(_mutex); _data.totalChargingAh = v; }

    float getTotalDischargingAh() { Lock l(_mutex); return _data.totalDischargingAh; }
    void  setTotalDischargingAh(float v) { Lock l(_mutex); _data.totalDischargingAh = v; }

    // --- Connection / Plug ---
    bool getBatteryConnected()  { Lock l(_mutex); return _data.batteryConnected; }
    void setBatteryConnected(bool v) { Lock l(_mutex); _data.batteryConnected = v; }

    bool getGunPhysicallyConnected()  { Lock l(_mutex); return _data.gunPhysicallyConnected; }
    void setGunPhysicallyConnected(bool v) { Lock l(_mutex); _data.gunPhysicallyConnected = v; }

    bool getVehicleConfirmed()  { Lock l(_mutex); return _data.vehicleConfirmed; }
    void setVehicleConfirmed(bool v) { Lock l(_mutex); _data.vehicleConfirmed = v; }

    // --- BMS Safety ---
    bool getBmsSafeToCharge()   { Lock l(_mutex); return _data.bmsSafeToCharge; }
    void setBmsSafeToCharge(bool v) { Lock l(_mutex); _data.bmsSafeToCharge = v; }

    bool getBmsHeatingActive()  { Lock l(_mutex); return _data.bmsHeatingActive; }
    void setBmsHeatingActive(bool v) { Lock l(_mutex); _data.bmsHeatingActive = v; }

    bool getChargingSwitch()    { Lock l(_mutex); return _data.chargingSwitch; }
    void setChargingSwitch(bool v) { Lock l(_mutex); _data.chargingSwitch = v; }

    uint8_t getHeating()        { Lock l(_mutex); return _data.heating; }
    void    setHeating(uint8_t v) { Lock l(_mutex); _data.heating = v; }

    // --- Charging ---
    bool getChargingEnabled()   { Lock l(_mutex); return _data.chargingEnabled; }
    void setChargingEnabled(bool v) { 
        Lock l(_mutex); 
        _data.chargingEnabled = v; 
        Serial.printf("[STATE] ⚡ chargingEnabled -> %s\n", v ? "START" : "STOP");
    }

    float getEnergyWh()         { Lock l(_mutex); return _data.energyWh; }
    void  setEnergyWh(float v)  { Lock l(_mutex); _data.energyWh = v; }
    void  addEnergyWh(float delta) { Lock l(_mutex); _data.energyWh += delta; }

    // --- Transaction ---
    bool getTransactionActive()  { Lock l(_mutex); return _data.transactionActive; }
    void setTransactionActive(bool v) { Lock l(_mutex); _data.transactionActive = v; }

    int  getActiveTransactionId()  { Lock l(_mutex); return _data.activeTransactionId; }
    void setActiveTransactionId(int v) { Lock l(_mutex); _data.activeTransactionId = v; }

    bool getRemoteStartAccepted()  { Lock l(_mutex); return _data.remoteStartAccepted; }
    void setRemoteStartAccepted(bool v) { Lock l(_mutex); _data.remoteStartAccepted = v; }

    bool getSessionActive()  { Lock l(_mutex); return _data.sessionActive; }
    void setSessionActive(bool v) { Lock l(_mutex); _data.sessionActive = v; }

    unsigned long getTxStartTime()  { Lock l(_mutex); return _data.txStartTime; }
    void setTxStartTime(unsigned long v) { Lock l(_mutex); _data.txStartTime = v; }

    unsigned long getTxStopTime()  { Lock l(_mutex); return _data.txStopTime; }
    void setTxStopTime(unsigned long v) { Lock l(_mutex); _data.txStopTime = v; }

    // --- Fault ---
    bool getFaultLockActive()  { Lock l(_mutex); return _data.faultLockActive; }
    void setFaultLockActive(bool v) { Lock l(_mutex); _data.faultLockActive = v; }

    unsigned long getFaultLockTime()  { Lock l(_mutex); return _data.faultLockTime; }
    void setFaultLockTime(unsigned long v) { Lock l(_mutex); _data.faultLockTime = v; }

    // --- Charger Health ---
    bool getChargerModuleOnline()  { Lock l(_mutex); return _data.chargerModuleOnline; }
    void setChargerModuleOnline(bool v) { Lock l(_mutex); _data.chargerModuleOnline = v; }

    // --- Timestamps ---
    unsigned long getLastBMS()  { Lock l(_mutex); return _data.lastBMS; }
    void setLastBMS(unsigned long v) { Lock l(_mutex); _data.lastBMS = v; }

    unsigned long getLastHeartbeat()  { Lock l(_mutex); return _data.lastHeartbeat; }
    void setLastHeartbeat(unsigned long v) { Lock l(_mutex); _data.lastHeartbeat = v; }

    unsigned long getLastChargerResp()  { Lock l(_mutex); return _data.lastChargerResp; }
    void setLastChargerResp(unsigned long v) { Lock l(_mutex); _data.lastChargerResp = v; }

    unsigned long getLastTerminalPower()  { Lock l(_mutex); return _data.lastTerminalPower; }
    void setLastTerminalPower(unsigned long v) { Lock l(_mutex); _data.lastTerminalPower = v; }

    unsigned long getLastTerminalStatus()  { Lock l(_mutex); return _data.lastTerminalStatus; }
    void setLastTerminalStatus(unsigned long v) { Lock l(_mutex); _data.lastTerminalStatus = v; }

    // --- OCPP ---
    bool getOcppInitialized()  { Lock l(_mutex); return _data.ocppInitialized; }
    void setOcppInitialized(bool v) { Lock l(_mutex); _data.ocppInitialized = v; }

private:
    SystemState() {
        _mutex = xSemaphoreCreateMutex();
        configASSERT(_mutex);  // Fatal if mutex creation fails
    }

    // Non-copyable
    SystemState(const SystemState&) = delete;
    SystemState& operator=(const SystemState&) = delete;

    // RAII lock helper
    struct Lock {
        SemaphoreHandle_t m;
        bool acquired;
        Lock(SemaphoreHandle_t mutex) : m(mutex), acquired(false) {
            acquired = (xSemaphoreTake(m, pdMS_TO_TICKS(50)) == pdTRUE);
        }
        ~Lock() { if (acquired) xSemaphoreGive(m); }
        operator bool() const { return acquired; }
    };

    SemaphoreHandle_t _mutex;
    StateSnapshot _data;
};
