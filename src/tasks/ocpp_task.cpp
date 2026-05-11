#include "tasks/system_tasks.h"
#include <Arduino.h>
#include <MicroOcpp.h>
#include "services/ocpp/OcppClient.h"
#include "services/network/NetworkManager.h"
#include "services/safety/HealthMonitor.h"
#include "system/state/SystemState.h"
#include "config/hardware.h"

namespace prod {
namespace tasks {

static TaskHandle_t ocppTaskHandle = nullptr;

static void ocppTaskLoop(void *pvParameters)
{
    Serial.println("[OCPP] OCPP Task started");

    uint32_t backoffMs = 5000;
    uint32_t nextAttemptMs = 0;
    uint32_t lastWatermarkLog = 0;

    for (;;)
    {
        if (!SystemState::instance().getOcppInitialized())
        {
            if (prod::g_networkManager.isConnected() && (int32_t)(millis() - nextAttemptMs) >= 0)
            {
                Serial.printf("[OCPP] Init attempt (backoff %u ms)\n", backoffMs);
                if (ocpp::init())
                {
                    backoffMs = 5000;
                    nextAttemptMs = 0;
                }
                else
                {
                    nextAttemptMs = millis() + backoffMs;
                    backoffMs = (backoffMs < 60000) ? (backoffMs * 2) : 60000;
                }
            }

            g_healthMonitor.feed();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ocpp::poll();
        g_healthMonitor.feed();

        // Log stack high-water mark every 60s — OCPP/TLS is the heaviest stack user
        uint32_t now = millis();
        if (now - lastWatermarkLog >= 60000u) {
            lastWatermarkLog = now;
            Serial.printf("[STACK] OCPP_LOOP  free min: %u words\n",
                          uxTaskGetStackHighWaterMark(nullptr));
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void start_ocpp_task()
{
    BaseType_t ocppResult = xTaskCreatePinnedToCore(
        ocppTaskLoop,
        "OCPP_LOOP",
        TASK_STACK_SIZE_OCPP,
        nullptr,
        TASK_PRIORITY_OCPP,
        &ocppTaskHandle,
        0); // Core 0 for OCPP
    
    if (ocppResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create OCPP_LOOP task!");
    }
    else
    {
        g_healthMonitor.addTaskToWatchdog(ocppTaskHandle, "OCPP_LOOP");
    }
}

} // namespace tasks
} // namespace prod
