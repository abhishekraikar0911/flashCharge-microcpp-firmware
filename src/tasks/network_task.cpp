#include "tasks/system_tasks.h"
#include <Arduino.h>
#include "services/network/NetworkManager.h"
#include "services/safety/HealthMonitor.h"
#include "config/hardware.h"

namespace prod {
namespace tasks {

static TaskHandle_t networkTaskHandle = nullptr;

static void networkTaskLoop(void *arg) {
    Serial.println("[NETWORK_MGR] Task started, beginning connection...");
    uint32_t lastWatermarkLog = 0;
    while (true) {
        prod::g_networkManager.poll();
        g_healthMonitor.feed();

        // Log stack high-water mark every 60s
        uint32_t now = millis();
        if (now - lastWatermarkLog >= 60000u) {
            lastWatermarkLog = now;
            Serial.printf("[STACK] NETWORK_MGR free min: %u words\n",
                          uxTaskGetStackHighWaterMark(nullptr));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void start_network_task()
{
    BaseType_t netResult = xTaskCreatePinnedToCore(
        networkTaskLoop,
        "NETWORK_MGR",
        TASK_STACK_SIZE_NETWORK,
        nullptr,
        5,  // Priority 5 — between CAN (8) and OCPP (3)
        &networkTaskHandle,
        0); // Core 0

    if (netResult != pdPASS) {
        Serial.println("[CRITICAL] Failed to create NETWORK_MGR task!");
    } else {
        g_healthMonitor.addTaskToWatchdog(networkTaskHandle, "NETWORK_MGR");
    }
}

} // namespace tasks
} // namespace prod
