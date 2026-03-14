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
    // For hot-path variables that change frequently.
    // ═══════════════════════════════════════════════════════════

    // --- Charger Module ---
    float getTerminalVolt()  { extern float terminalVolt; return terminalVolt; }
    void  setTerminalVolt(float v) { extern float terminalVolt; terminalVolt = v; Lock l(_mutex); _data.terminalVolt = v; }

    float getTerminalCurr()  { extern float terminalCurr; return terminalCurr; }
    void  setTerminalCurr(float v) { extern float terminalCurr; terminalCurr = v; Lock l(_mutex); _data.terminalCurr = v; }

    float getChargerTemp()   { extern float chargerTemp; return chargerTemp; }
    void  setChargerTemp(float v) { extern float chargerTemp; chargerTemp = v; Lock l(_mutex); _data.chargerTemp = v; }

    float getTerminalPower() { extern float terminalchargerPower; return terminalchargerPower; }
    void  setTerminalPower(float v) { extern float terminalchargerPower; terminalchargerPower = v; Lock l(_mutex); _data.terminalPower = v; }

    float getChargerVolt()   { extern float chargerVolt; return chargerVolt; }
    void  setChargerVolt(float v) { extern float chargerVolt; chargerVolt = v; Lock l(_mutex); _data.chargerVolt = v; }

    float getChargerCurr()   { extern float chargerCurr; return chargerCurr; }
    void  setChargerCurr(float v) { extern float chargerCurr; chargerCurr = v; Lock l(_mutex); _data.chargerCurr = v; }

    // --- BMS ---
    float getSocPercent()    { extern float socPercent; return socPercent; }
    void  setSocPercent(float v) { extern float socPercent; socPercent = v; Lock l(_mutex); _data.socPercent = v; }

    float getRangeKm()       { extern float rangeKm; return rangeKm; }
    void  setRangeKm(float v) { extern float rangeKm; rangeKm = v; Lock l(_mutex); _data.rangeKm = v; }

    float getBatteryAh()     { extern float batteryAh; return batteryAh; }
    void  setBatteryAh(float v) { extern float batteryAh; batteryAh = v; Lock l(_mutex); _data.batteryAh = v; }

    float getBMS_Vmax()      { extern float BMS_Vmax; return BMS_Vmax; }
    void  setBMS_Vmax(float v) { extern float BMS_Vmax; BMS_Vmax = v; Lock l(_mutex); _data.BMS_Vmax = v; }

    float getBMS_Imax()      { extern float BMS_Imax; return BMS_Imax; }
    void  setBMS_Imax(float v) { extern float BMS_Imax; BMS_Imax = v; Lock l(_mutex); _data.BMS_Imax = v; }

    float getCharger_Vmax()  { extern float Charger_Vmax; return Charger_Vmax; }
    void  setCharger_Vmax(float v) { extern float Charger_Vmax; Charger_Vmax = v; Lock l(_mutex); _data.Charger_Vmax = v; }

    float getCharger_Imax()  { extern float Charger_Imax; return Charger_Imax; }
    void  setCharger_Imax(float v) { extern float Charger_Imax; Charger_Imax = v; Lock l(_mutex); _data.Charger_Imax = v; }

    uint8_t getVehicleModel() { extern uint8_t vehicleModel; return vehicleModel; }
    void    setVehicleModel(uint8_t v) { extern uint8_t vehicleModel; vehicleModel = v; Lock l(_mutex); _data.vehicleModel = v; }

    float getBatterySoc()    { extern float batterySoc; return batterySoc; }
    void  setBatterySoc(float v) { extern float batterySoc; batterySoc = v; Lock l(_mutex); _data.batterySoc = v; }

    float getTotalChargingAh() { extern float totalChargingAh; return totalChargingAh; }
    void  setTotalChargingAh(float v) { extern float totalChargingAh; totalChargingAh = v; Lock l(_mutex); _data.totalChargingAh = v; }

    float getTotalDischargingAh() { extern float totalDischargingAh; return totalDischargingAh; }
    void  setTotalDischargingAh(float v) { extern float totalDischargingAh; totalDischargingAh = v; Lock l(_mutex); _data.totalDischargingAh = v; }

    // --- Connection / Plug ---
    bool getBatteryConnected()  { extern bool batteryConnected; return batteryConnected; }
    void setBatteryConnected(bool v) { extern bool batteryConnected; batteryConnected = v; Lock l(_mutex); _data.batteryConnected = v; }

    bool getGunPhysicallyConnected()  { extern bool gunPhysicallyConnected; return gunPhysicallyConnected; }
    void setGunPhysicallyConnected(bool v) { extern bool gunPhysicallyConnected; gunPhysicallyConnected = v; Lock l(_mutex); _data.gunPhysicallyConnected = v; }

    bool getVehicleConfirmed()  { extern bool vehicleConfirmed; return vehicleConfirmed; }
    void setVehicleConfirmed(bool v) { extern bool vehicleConfirmed; vehicleConfirmed = v; Lock l(_mutex); _data.vehicleConfirmed = v; }

    // --- BMS Safety ---
    bool getBmsSafeToCharge()   { extern bool bmsSafeToCharge; return bmsSafeToCharge; }
    void setBmsSafeToCharge(bool v) { extern bool bmsSafeToCharge; bmsSafeToCharge = v; Lock l(_mutex); _data.bmsSafeToCharge = v; }

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
        
        // BRIDGE: Update legacy global for CAN driver compatibility
        extern bool chargingEnabled;
        chargingEnabled = v;
        
        Serial.printf("[STATE] ⚡ chargingEnabled -> %s (bridged to global)\n", v ? "START" : "STOP");
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
