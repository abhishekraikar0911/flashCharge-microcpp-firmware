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

extern float BMS_Vmax, BMS_Imax;
extern float Charger_Vmax, Charger_Imax;
extern float chargerVolt, chargerCurr, chargerTemp, terminalchargerPower;
extern float terminalVolt, terminalCurr;
extern float socPercent;
extern float batteryAh;
extern float batterySoc;

extern uint16_t metric79_raw;
extern float metric79_scaled;
extern uint32_t metric83_raw;
extern float metric83_scaled;

extern unsigned long lastBMS;
extern uint8_t heating;
extern unsigned long lastHeartbeat;
extern unsigned long lastChargerResponse;

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
// FUNCTION PROTOTYPES
// =========================================================
void twai_init();
void can_rx_task(void *arg);
void chargerCommTask(void *arg);
void handleBMSMessage(const twai_message_t &msg);
void handleChargerMessage(const twai_message_t &msg);
void requestSOCFromBMS();
void handleSOCMessage(const twai_message_t &msg);

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