/**
 * @file test/stubs/freertos/semphr.h
 * @brief Minimal FreeRTOS semaphore stub wrapping std::mutex for native testing.
 */
#pragma once
#include <cassert>
#include <mutex>

// --- Types ---
using SemaphoreHandle_t = std::mutex*;
using BaseType_t        = int;
using TickType_t        = unsigned long;

#define pdTRUE   1
#define pdFALSE  0
#define pdMS_TO_TICKS(ms) (ms)
#define configASSERT(x)   assert(x)

// --- API ---
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new std::mutex();
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t) {
    if (!m) return pdFALSE;
    m->lock();
    return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t m) {
    if (!m) return pdFALSE;
    m->unlock();
    return pdTRUE;
}
