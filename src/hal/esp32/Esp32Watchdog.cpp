#include "hal/esp32/Esp32Watchdog.h"
#include <esp_task_wdt.h>
#include <Arduino.h>

Esp32Watchdog::Esp32Watchdog() : initialized(false), enabled(false) {}

bool Esp32Watchdog::init(uint32_t timeoutMs) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = timeoutMs,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t err = esp_task_wdt_init(&twdt_config);
#else
    esp_err_t err = esp_task_wdt_init(timeoutMs / 1000, true);
#endif
    if (err != ESP_OK) {
        return false;
    }
    initialized = true;
    return true;
}

void Esp32Watchdog::kick() {
    if (initialized && enabled) {
        esp_task_wdt_reset();
    }
}

void Esp32Watchdog::enable() {
    if (initialized) {
        esp_task_wdt_add(NULL); // Add the current task to the WDT
        enabled = true;
    }
}

void Esp32Watchdog::disable() {
    if (initialized && enabled) {
        esp_task_wdt_delete(NULL); // Remove the current task from the WDT
        enabled = false;
    }
}
