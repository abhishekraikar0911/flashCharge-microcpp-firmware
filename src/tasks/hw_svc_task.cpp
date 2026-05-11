#include "tasks/system_tasks.h"
#include <Arduino.h>
#include "services/safety/SystemMonitor.h"
#include "services/safety/HealthMonitor.h"

namespace prod {
namespace tasks {

static TaskHandle_t hwSvcTaskHandle = nullptr;

static void hwSvcTaskLoop(void *arg) {
    Serial.println("[HW_SVC] Task started for deterministic hardware monitoring");
    uint32_t lastWatermarkLog = 0;
    while (true) {
        g_healthMonitor.feed();
        
        prod::SystemMonitor::instance().poll();

        // Log stack high-water mark every 60s to detect stack overflow risks
        uint32_t now = millis();
        if (now - lastWatermarkLog >= 60000u) {
            lastWatermarkLog = now;
            Serial.printf("[STACK] HW_SVC    free min: %u words\n",
                          uxTaskGetStackHighWaterMark(nullptr));
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // Poll at 20Hz
    }
}

void start_hw_svc_task()
{
    BaseType_t hwResult = xTaskCreatePinnedToCore(
        hwSvcTaskLoop,
        "HW_SVC",
        4096,
        nullptr,
        4,  // Priority 4 — between Ui (2) and Network (5)
        &hwSvcTaskHandle,
        1); // Core 1

    if (hwResult != pdPASS) {
        Serial.println("[CRITICAL] Failed to create HW_SVC task!");
    } else {
        g_healthMonitor.addTaskToWatchdog(hwSvcTaskHandle, "HW_SVC");
    }
}

} // namespace tasks
} // namespace prod
