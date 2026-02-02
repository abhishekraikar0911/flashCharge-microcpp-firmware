#pragma once
#include <Arduino.h>
#include <driver/twai.h>
#include <freertos/semphr.h>
#include <cstdarg>

#ifndef HEADER_H
#define HEADER_H

// =========================================================
// CAN ID CONSTANTS
// =========================================================
#define ID_CTRL_RESP 0x0681817EUL
#define ID_TELEM_RESP 0x0681827EUL
#define ID_TERM_POWER 0x00433F01UL
#define ID_TERM_STATUS 0x00473F01UL
#define ID_HEARTBEAT 0x18FF50E5UL
#define ID_BMS_REQUEST 0x1806E5F4UL
#define ID_SOC_REQUEST 0x160B0180UL
#define ID_SOC_RESPONSE 0x160B8001UL
#define ID_CHARGE_AH_REQUEST 0x160B0180UL
#define ID_CHARGE_AH_RESPONSE 0x160B8001UL
#define ID_DISCHARGE_AH_REQUEST 0x160D0180UL
#define ID_DISCHARGE_AH_RESPONSE 0x160D8001UL

// =========================================================
// GLOBAL SYNCHRONIZATION
// =========================================================
extern SemaphoreHandle_t dataMutex;
extern SemaphoreHandle_t serialMutex;

// =========================================================
// SHARED STATE VARIABLES
// =========================================================
extern bool vehicleConfirmed;
extern bool gunPhysicallyConnected;

extern float energyWh;
extern bool batteryConnected;
extern bool chargingEnabled;
extern bool chargingswitch;

// BMS Safety flags
extern bool bmsSafeToCharge;  // TRUE only when byte4=0x00
extern bool bmsHeatingActive;  // TRUE when byte5=0x01

extern float BMS_Vmax, BMS_Imax;
extern float Charger_Vmax, Charger_Imax;
extern float chargerVolt, chargerCurr, chargerTemp, terminalchargerPower;
extern float terminalVolt, terminalCurr;
extern float socPercent;
extern float rangeKm;
extern uint8_t vehicleModel;  // 0=Unknown, 1=Classic, 2=Pro, 3=Max
extern float batteryAh;
extern float batterySoc;
extern float totalChargingAh;    // NEW: Total charging Ah (lifetime)
extern float totalDischargingAh; // NEW: Total discharging Ah (lifetime)

extern uint16_t metric79_raw;
extern float metric79_scaled;
extern uint32_t metric83_raw;
extern float metric83_scaled;

extern unsigned long lastBMS;
extern uint8_t heating;
extern unsigned long lastHeartbeat;
extern unsigned long lastChargerResponse;
extern unsigned long lastTerminalPower;  // NEW: Track terminal data CAN messages
extern unsigned long lastTerminalStatus; // NEW: Track terminal status CAN messages

extern const char *chargerStatus;
extern const char *terminalchargerStatus;
extern const char *terminalStatus;

extern int userChoice;
extern unsigned long lastPrint;
extern uint8_t stopCmd;

// Session lock
extern bool sessionActive;

// OCPP initialization status
extern bool ocppInitialized;

// =========================================================
// TRANSACTION GATE (FIX 1 - HARD GATE)
// =========================================================
extern bool transactionActive;      // TRUE only when valid transaction running
extern int activeTransactionId;     // Valid transaction ID (>0)
extern bool remoteStartAccepted;    // TRUE only after RemoteStart accepted

// Transaction state (for safe MeterValues) - UNUSED
// extern bool transactionActive;
// extern int currentTransactionId;

// Buffers
extern uint8_t lastData[8], lastBMSData[8], lastStatusData[8], lastHData[8];
extern uint8_t lastVmaxData[8], lastImaxData[8], lastBattData[8];
extern uint8_t lastVoltData[8], lastCurrData[8], lastTempData[8];
extern uint8_t lastTermData1[8], lastTermData2[8];

extern uint32_t cachedRawV;
extern uint32_t cachedRawI;

// =========================================================
// STRUCTURES
// =========================================================
// Legacy RxBufItem for backward compatibility with existing code
struct RxBufItem
{
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    bool ext;
    bool rtr;
};

struct Group
{
    uint32_t reqId;
    uint32_t respId;
    uint8_t funcs[5];
    uint8_t funcCount;
    unsigned long period;
    unsigned long lastReq;
    uint8_t funcIndex;
};

// CAN Update Flag
extern volatile bool updateCAN;

// Groups
extern Group groups[];

// =========================================================
// CHARGER HEALTH MONITORING
// =========================================================
extern bool chargerModuleOnline;  // NEW: Charger module communication status
bool isChargerModuleHealthy();     // NEW: Check if charger is responding
void notifyChargerFault(bool faulted); // NEW: Notify OCPP about charger fault
void initGlobals();
void can1_rx_task(void *arg);  // CAN1 - ISO1050 - Charger
void can2_rx_task(void *arg);  // CAN2 - MCP2515 - BMS
void chargerCommTask(void *arg);
void handleBMSMessage(const twai_message_t &msg);
void handleChargerMessage(const twai_message_t &msg);
void requestSOCFromBMS();
void handleSOCMessage(const twai_message_t &msg);
void requestChargingAh();        // NEW: Request total charging Ah
void requestDischargingAh();     // NEW: Request total discharging Ah
void handleChargingAhMessage(const twai_message_t &msg);    // NEW
void handleDischargingAhMessage(const twai_message_t &msg); // NEW

bool popFrame(RxBufItem &out);
void pushFrame(const twai_message_t &msg);
void sendGroupRequest(Group &g);
void sendChargerFeedback();

void printDecodedData();
void printMenu();
void processSerialInput();
void printChargerFeedback(float volt, float curr, uint8_t flags, esp_err_t res);

// OCPP Functions
void startOCPP();
void ocpp_sendTelemetry();

// Safe Serial print functions
inline void safePrint(const char *str)
{
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        Serial.print(str);
        xSemaphoreGive(serialMutex);
    }
}

inline void safePrintln(const char *str = "")
{
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        Serial.println(str);
        xSemaphoreGive(serialMutex);
    }
}

inline void safePrintf(const char *format, ...)
{
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        va_list args;
        va_start(args, format);
        Serial.printf(format, args);
        va_end(args);
        xSemaphoreGive(serialMutex);
    }
}

#endif // HEADER_H