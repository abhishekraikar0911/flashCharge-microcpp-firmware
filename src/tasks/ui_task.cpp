#include "tasks/system_tasks.h"
#include <Arduino.h>
#include "system/SafeSerial.h"

extern void processDebugCommand(char c);

namespace prod {
namespace tasks {

static void uiTaskLoop(void *arg)
{
    Serial.println("[UI_TASK] Serial input listener started");
    uint32_t lastWatermarkLog = 0;
    while (true)
    {
        // Consume all available characters to prevent buffer buildup
        while (Serial.available() > 0) {
            char c = Serial.read();
            processDebugCommand(c);
        }

        // Log stack high-water mark every 60s
        uint32_t now = millis();
        if (now - lastWatermarkLog >= 60000u) {
            lastWatermarkLog = now;
            if (!SafeSerial::isSuppressed()) {
                Serial.printf("[STACK] UI_TASK    free min: %u words\n",
                              uxTaskGetStackHighWaterMark(nullptr));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // Poll at 20Hz
    }
}

void start_ui_task()
{
    BaseType_t uiResult = xTaskCreatePinnedToCore(
        uiTaskLoop,
        "UI_TASK",
        4096,
        nullptr,
        3, // priority 3
        nullptr,
        1);
    
    if (uiResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create UI_TASK!");
    }
}

} // namespace tasks
} // namespace prod
