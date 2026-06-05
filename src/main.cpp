#include <Arduino.h>
#include <MicroOcpp.h>
#include <cstdint>
#include "app/ChargePoint.h"
#include "bsp/esp32_rev1/bsp_init.h"
#include "services/safety/HealthMonitor.h"
#include "system/logging/DebugLogger.h"
#include "system/SafeSerial.h"

void setup()
{
    Serial.begin(115200); // 1. start Serial for debugging

    prod::g_healthMonitor.init(); // 2. Initialize the system health and WDT

    Serial.println("[Boot] Waiting 10s for hardware power rails to stabilize...");
    for (int i = 0; i < 100; i++) 
    {
        prod::g_healthMonitor.feed(); //feeding WDT to stop it from resetting
        delay(100);
    }

    if (!BSP_Init()) {
        Serial.println("[BSP] CRITICAL: BSP_Init() failed! Hardware may be partially initialized.");
    } else {
        Serial.println("[BSP] HAL layer initialized. AppContext populated.");
    }

    // CRITICAL FIX: Use a fixed-size static buffer instead of Arduino String.
    // Arduino String uses dynamic heap allocation and causes heap fragmentation
    // in long-running embedded systems, leading to crashes after 3-5 days of uptime.
    mocpp_set_console_out([](const char* msg) {
        static char   logBuffer[512];
        static size_t logLen = 0;
        static uint32_t lastUninitWarn = 0;

        //buffer overflow check
        // Append fragment — guard against overflow by clamping to buffer capacity
        size_t msgLen = strlen(msg);
        if (logLen + msgLen >= sizeof(logBuffer) - 1) {
            // Buffer would overflow: flush what we have and reset
            logBuffer[logLen] = '\0';
            SafeSerial::print(logBuffer);
            logLen = 0;
        }
        memcpy(logBuffer + logLen, msg, msgLen);
        logLen += msgLen;
        logBuffer[logLen] = '\0';

        // Only process complete lines (terminated with '\n')
        if (logLen > 0 && logBuffer[logLen - 1] == '\n') {
            bool suppress = false;

            // Throttle "OCPP uninitialized" to once every 30s
            if (strstr(logBuffer, "OCPP uninitialized") != nullptr) {
                uint32_t now = millis();
                if (now - lastUninitWarn < 30000u) suppress = true;
                else lastUninitWarn = now;
            }

            // Completely suppress noisy RequestQueue mismatch warnings
            if (strstr(logBuffer, "Received response doesn't match") != nullptr) {
                suppress = true;
            }

            if (!suppress) {
                SafeSerial::print(logBuffer);
            }

            // Reset buffer for next line
            logLen = 0;
            logBuffer[0] = '\0';
        }
    });

    // Launch the Application Orchestrator
    prod::ChargePoint::instance().boot(); //register multiple independent functions,
    
    DebugLogger::init(); //Initializes debug console with all the function 
    DebugLogger::printMenu(); //prints menu 
}

void loop()
{
    prod::g_healthMonitor.feed(); //feeding WDT to stop it from resetting
    prod::g_healthMonitor.poll();   
    vTaskDelay(pdMS_TO_TICKS(100));
}
