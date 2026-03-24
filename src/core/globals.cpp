#include "header.h"

// =========================================================
// GLOBAL SYNCHRONIZATION
// =========================================================
SemaphoreHandle_t dataMutex = nullptr;
SemaphoreHandle_t serialMutex = nullptr;

// =========================================================
// CAN DIAGNOSTIC BUFFERS (Raw CAN protocol data, not state)
// =========================================================
// H3 FIX: Legacy raw copy-buffers removed to save RAM and mutex contention.

// =========================================================
// CAN RAW PROTOCOL VALUES (Charger Module format)
// =========================================================
uint32_t cachedRawV = 0;
uint32_t cachedRawI = 0;

// =========================================================
// CAN-LEVEL STATUS STRINGS (Not state — used for CAN diagnostics only)
// =========================================================
const char *chargerStatus = "UNKNOWN";
const char *terminalchargerStatus = "UNKNOWN";
const char *terminalStatus = "UNKNOWN";

// CAN Update Flag
volatile bool updateCAN = false;

// CAN PROTOCOL METRICS (diagnostic only, not state)
uint16_t metric79_raw = 0;
float metric79_scaled = 0.0f;
uint32_t metric83_raw = 0;
float metric83_scaled = 0.0f;

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
