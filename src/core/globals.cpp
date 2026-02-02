#include "../../include/header.h"

// =========================================================
// GLOBAL SYNCHRONIZATION
// =========================================================
SemaphoreHandle_t dataMutex = nullptr;
SemaphoreHandle_t serialMutex = nullptr;

// =========================================================
// SHARED STATE VARIABLES
// =========================================================
bool vehicleConfirmed = false;
bool gunPhysicallyConnected = false;

float energyWh = 0.0f;
bool batteryConnected = false;
bool chargingEnabled = false;
bool chargingswitch = false;

float BMS_Vmax = 0.0f;
float BMS_Imax = 0.0f;
float Charger_Vmax = 0.0f;
float Charger_Imax = 0.0f;
float chargerVolt = 0.0f;
float chargerCurr = 0.0f;
float chargerTemp = 0.0f;
float terminalchargerPower = 0.0f;
float terminalVolt = 0.0f;
float terminalCurr = 0.0f;
float socPercent = 0.0f;
float rangeKm = 0.0f;
uint8_t vehicleModel = 0;  // 0=Unknown, 1=Classic, 2=Pro, 3=Max

uint16_t metric79_raw = 0;
float metric79_scaled = 0.0f;
uint32_t metric83_raw = 0;
float metric83_scaled = 0.0f;

unsigned long lastBMS = 0;
uint8_t heating = 0;
// CRITICAL: Initialize to 0 for proper timeout detection at boot
// With millis() - 0, timeout will trigger immediately if no messages received
unsigned long lastHeartbeat = 0;
unsigned long lastChargerResponse = 0;
unsigned long lastTerminalPower = 0;
unsigned long lastTerminalStatus = 0;
bool chargerModuleOnline = false;     // Charger offline at boot

const char *chargerStatus = "UNKNOWN";
const char *terminalchargerStatus = "UNKNOWN";
const char *terminalStatus = "UNKNOWN";

int userChoice = 0;
unsigned long lastPrint = 0;
uint8_t stopCmd = 0;

bool sessionActive = false;
bool ocppInitialized = false;

// =========================================================
// TRANSACTION GATE (FIX 1 - HARD GATE)
// =========================================================
bool transactionActive = false;      // TRUE only when valid transaction running
int activeTransactionId = -1;        // Valid transaction ID (>0)
bool remoteStartAccepted = false;    // TRUE only after RemoteStart accepted

// Buffers
uint8_t lastData[8] = {0};
uint8_t lastBMSData[8] = {0};
uint8_t lastStatusData[8] = {0};
uint8_t lastHData[8] = {0};
uint8_t lastVmaxData[8] = {0};
uint8_t lastImaxData[8] = {0};
uint8_t lastBattData[8] = {0};
uint8_t lastVoltData[8] = {0};
uint8_t lastCurrData[8] = {0};
uint8_t lastTempData[8] = {0};
uint8_t lastTermData1[8] = {0};
uint8_t lastTermData2[8] = {0};

uint32_t cachedRawV = 0;
uint32_t cachedRawI = 0;

// CAN Update Flag
volatile bool updateCAN = false;

// Initialize mutexes in setup
void initGlobals()
{
    // FIX: Retry mutex creation with reboot on failure (production-ready)
    if (dataMutex == nullptr)
    {
        for (int attempt = 1; attempt <= 3; attempt++)
        {
            dataMutex = xSemaphoreCreateMutex();
            if (dataMutex != nullptr) break;
            
            Serial.printf("[CRITICAL] Failed to create dataMutex (attempt %d/3)\n", attempt);
            delay(100);
        }
        
        if (dataMutex == nullptr)
        {
            Serial.println("[CRITICAL] dataMutex creation failed after 3 attempts - REBOOTING...");
            delay(1000);
            ESP.restart();
        }
    }
    
    if (serialMutex == nullptr)
    {
        for (int attempt = 1; attempt <= 3; attempt++)
        {
            serialMutex = xSemaphoreCreateMutex();
            if (serialMutex != nullptr) break;
            
            Serial.printf("[CRITICAL] Failed to create serialMutex (attempt %d/3)\n", attempt);
            delay(100);
        }
        
        if (serialMutex == nullptr)
        {
            Serial.println("[CRITICAL] serialMutex creation failed after 3 attempts - REBOOTING...");
            delay(1000);
            ESP.restart();
        }
    }
}
