#include "hal/esp32/Esp32Timer.h"
#include <Arduino.h>

Esp32Timer::Esp32Timer() {
    // No initialization required for Arduino millis/delay wrapper
}

uint32_t Esp32Timer::millis() {
    return ::millis();
}

void Esp32Timer::delayMs(uint32_t ms) {
    if (ms > 0) {
        // Yield to FreeRTOS scheduler
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        // vTaskDelay(0) behaves differently (yields time slice to equal-priority tasks)
        // Usually, 0 ms delay is a specialized use case or mistake.
        vTaskDelay(0);
    }
}

uint64_t Esp32Timer::micros() {
    return ::micros();
}
