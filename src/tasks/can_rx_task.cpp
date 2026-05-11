#include "tasks/system_tasks.h"
#include <Arduino.h>
#include "services/safety/HealthMonitor.h"
#include "bsp/esp32_rev1/bsp_init.h"
#include "config/hardware.h"

namespace prod {
namespace tasks {

static TaskHandle_t can2RxTaskHandle = nullptr;

static void can2RxTaskLoop(void* arg) {
    Serial.println("[CAN2_RX] MCP2515 drain task started (priority 8, Core 1, INT-driven + 5ms fallback)");
    uint32_t lastWatermarkLog = 0;
    for (;;) {
        // Block until ISR notifies OR 5ms timeout (safety polling fallback)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
        BSP_DrainCAN2();                // Drain HW buffers → SW queue
        g_healthMonitor.feed();         // Feed task watchdog

        // Log stack high-water mark every 60s
        uint32_t now = millis();
        if (now - lastWatermarkLog >= 60000u) {
            lastWatermarkLog = now;
            Serial.printf("[STACK] CAN2_RX   free min: %u words\n",
                          uxTaskGetStackHighWaterMark(nullptr));
        }
    }
}

void start_can_rx_task()
{
    BaseType_t can2RxResult = xTaskCreatePinnedToCore(
        can2RxTaskLoop,
        "CAN2_RX",
        TASK_STACK_SIZE_CAN_RX,
        nullptr,
        TASK_PRIORITY_CAN_RX,     // Priority 8
        &can2RxTaskHandle,
        1);                       // Core 1

    if (can2RxResult != pdPASS) {
        Serial.println("[CRITICAL] Failed to create CAN2_RX task! MCP2515 overflow risk on vehicle CAN.");
    } else {
        g_healthMonitor.addTaskToWatchdog(can2RxTaskHandle, "CAN2_RX");
        // Arm INT-driven mode: pass the task handle to the MCP2515 ISR
        BSP_SetCAN2RxTask(can2RxTaskHandle);
        Serial.println("[CAN2_RX] ✅ INT-driven drain task armed — GPIO 34 ISR will wake task on each frame");
    }
}

} // namespace tasks
} // namespace prod
