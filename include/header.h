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
#define ID_SOC_REQUEST 0x18900140UL
#define ID_SOC_RESPONSE 0x18904001UL

// =========================================================
// GLOBAL SYNCHRONIZATION
// =========================================================
extern SemaphoreHandle_t dataMutex;
extern SemaphoreHandle_t serialMutex;

// =========================================================
// CAN DIAGNOSTIC BUFFERS (Raw protocol data)
// =========================================================
extern uint8_t lastData[8], lastBMSData[8], lastStatusData[8], lastHData[8];
extern uint8_t lastVmaxData[8], lastImaxData[8], lastBattData[8];
extern uint8_t lastVoltData[8], lastCurrData[8], lastTempData[8];
extern uint8_t lastTermData1[8], lastTermData2[8];

// CAN RAW PROTOCOL VALUES (Charger Module format)
extern uint32_t cachedRawV;
extern uint32_t cachedRawI;

// CAN-LEVEL STATUS STRINGS (diagnostic only)
extern const char *chargerStatus;
extern const char *terminalchargerStatus;
extern const char *terminalStatus;

// CAN PROTOCOL METRICS (diagnostic only)
extern uint16_t metric79_raw;
extern float metric79_scaled;
extern uint32_t metric83_raw;
extern float metric83_scaled;

// CAN Update Flag
extern volatile bool updateCAN;

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

// Groups
extern Group groups[];

// =========================================================
// FUNCTION DECLARATIONS
// =========================================================
bool isChargerModuleHealthy();
void notifyChargerFault(bool faulted);
void initGlobals();
void can1_rx_task(void *arg);  // CAN1 - ISO1050 - Charger
void can2_rx_task(void *arg);  // CAN2 - MCP2515 - BMS
void chargerCommTask(void *arg);
void handleBMSMessage(const twai_message_t &msg);
void handleChargerMessage(const twai_message_t &msg);
void requestSOCFromBMS();
void handleSOCMessage(const twai_message_t &msg);

bool popFrame(RxBufItem &out);
void pushFrame(const twai_message_t &msg);
void sendGroupRequest(Group &g);
void sendChargerFeedback();
void sendImmediateChargerStop();

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
        char buf[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        Serial.print(buf);
        xSemaphoreGive(serialMutex);
    }
}

#endif // HEADER_H