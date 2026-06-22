#include "tasks/system_tasks.h"
#include <Arduino.h>
#include "services/safety/SystemMonitor.h"
#include "services/safety/HealthMonitor.h"
#include "system/SafeSerial.h"
#include "config/hardware.h"     // TASK_STACK_SIZE_HW_SVC
#include "services/ota/OtaManager.h"      // Deferred OTA reboot check
#include "system/state/SystemState.h"     // getGunPhysicallyConnected()

// Override Serial to automatically suppress all logs when provisioning wizard is active
#define Serial SafeSerial::SafeSerialObj

namespace prod {
namespace tasks {

static TaskHandle_t hwSvcTaskHandle = nullptr;

// ── Deferred OTA timeout: 24 hours ──────────────────────────────────────────
static constexpr uint32_t OTA_DEFER_TIMEOUT_MS = 24UL * 60UL * 60UL * 1000UL;

static void hwSvcTaskLoop(void *arg) {
    Serial.println("[HW_SVC] Task started for deterministic hardware monitoring");
    uint32_t lastWatermarkLog  = 0;
    uint32_t lastDeferredCheck = 0;

    while (true) {
        g_healthMonitor.feed();

        prod::SystemMonitor::instance().poll();

        uint32_t now = millis();

        // ── Deferred OTA: reboot when gun safely unplugged ──────────────
        // Checked every 5s — safe for any duration (WDT fed each loop).
        if (now - lastDeferredCheck >= 5000u) {
            lastDeferredCheck = now;

            if (prod::OTAManager::hasDeferredReboot()) {
                uint32_t deferredFor = now - prod::OTAManager::getDeferredSinceMs();

                // 24-hour safety timeout: cancel stale deferred installs
                if (deferredFor > OTA_DEFER_TIMEOUT_MS) {
                    Serial.println("[OTA] ⚠️  Deferred OTA cancelled — pending for >24h. "
                                   "Trigger OTA again from CSMS.");
                    prod::OTAManager::setDeferredReboot(false);
                }
                // CRITICAL RULE: only block reboot if a billing session is ACTIVE.
                // getGunPhysicallyConnected() is driven by BMS CAN frames — on a test bench
                // (or idle charger) the BMS still broadcasts, making this flag always true.
                // If no transaction is running, there is zero billing risk. Reboot after 60s
                // grace period to let MicroOcpp flush the FirmwareStatusNotification.
                else if (SystemState::instance().getTransactionActive()) {
                    if (deferredFor % 60000 < 5000) {
                        Serial.printf("[OTA] ⏳ OTA waiting — active charging session in progress (%lus elapsed)\n",
                                      (unsigned long)(deferredFor / 1000));
                    }
                }
                // No active transaction → safe to reboot. Wait 60s grace first so OCPP can
                // send FirmwareStatusNotification("Installing") before we pull the plug.
                else if (deferredFor >= 60000u) {
                    Serial.println("[OTA] ✅ No active session. Applying deferred firmware — rebooting in 3s...");
                    g_healthMonitor.feed();
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    ESP.restart();
                } else {
                    // Still in 60s grace window — log every 10s
                    if (deferredFor % 10000 < 5000) {
                        Serial.printf("[OTA] ⏳ OTA grace period — rebooting in %lus...\n",
                                      (unsigned long)((60000u - deferredFor) / 1000));
                    }
                }
            }
        }

        // Log stack high-water mark every 60s to detect stack overflow risks
        if (now - lastWatermarkLog >= 60000u) {
            lastWatermarkLog = now;
            if (!SafeSerial::isSuppressed()) {
                Serial.printf("[STACK] HW_SVC    free min: %u words\n",
                              uxTaskGetStackHighWaterMark(nullptr));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // Poll at 20Hz
    }
}

void start_hw_svc_task()
{
    BaseType_t hwResult = xTaskCreatePinnedToCore(
        hwSvcTaskLoop,
        "HW_SVC",
        TASK_STACK_SIZE_HW_SVC,  // 8192 words — was 4096, watermark hit 1424 (too tight)
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
