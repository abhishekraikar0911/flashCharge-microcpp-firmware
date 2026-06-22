#include "system/FaultQueue.h"
#include <string.h>
#include <Arduino.h>

// ── Static storage ────────────────────────────────────────────────────
PendingFault         FaultQueue::_buf[FAULT_QUEUE_SIZE];
uint8_t              FaultQueue::_head  = 0;
uint8_t              FaultQueue::_count = 0;
SemaphoreHandle_t    FaultQueue::_mutex = nullptr;

void FaultQueue::ensureMutex() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
}

void FaultQueue::push(const char* code, const char* description, uint8_t severity) {
    ensureMutex();
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    // If full: advance head (drop oldest) to make room
    uint8_t writeIdx;
    if (_count < FAULT_QUEUE_SIZE) {
        writeIdx = (_head + _count) % FAULT_QUEUE_SIZE;
        _count++;
    } else {
        // Overwrite oldest — advance read pointer
        writeIdx = _head;
        _head = (_head + 1) % FAULT_QUEUE_SIZE;
    }

    PendingFault& f = _buf[writeIdx];
    strncpy(f.code,        code,        FAULT_CODE_LEN  - 1); f.code[FAULT_CODE_LEN - 1]   = '\0';
    strncpy(f.description, description, FAULT_DESC_LEN  - 1); f.description[FAULT_DESC_LEN - 1] = '\0';
    f.severity  = severity;
    f.uptimeMs  = (uint32_t)millis();

    xSemaphoreGive(_mutex);
}

bool FaultQueue::pop(PendingFault& out) {
    ensureMutex();
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;

    if (_count == 0) {
        xSemaphoreGive(_mutex);
        return false;
    }

    out   = _buf[_head];
    _head = (_head + 1) % FAULT_QUEUE_SIZE;
    _count--;

    xSemaphoreGive(_mutex);
    return true;
}

bool FaultQueue::hasItems() {
    ensureMutex();
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    bool has = (_count > 0);
    xSemaphoreGive(_mutex);
    return has;
}
