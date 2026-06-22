#pragma once
/**
 * @file FaultQueue.h
 * @brief Zero-dependency hardware fault ring buffer.
 *
 * Any low-level module (GsmManager, Esp32MCP2515, ChargePoint) can push
 * a fault here WITHOUT needing to include OcppClient.h or any network headers.
 * The OCPP task drains the queue and sends each fault as a DataTransfer
 * "HardwareFault" message once the WebSocket is connected.
 *
 * Thread-safe: protected by a FreeRTOS mutex.
 * Capacity: 8 faults max. When full, oldest fault is silently dropped.
 */

#include <stdint.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ── Severity levels ────────────────────────────────────────────────────
#define FAULT_SEV_INFO     0
#define FAULT_SEV_WARNING  1
#define FAULT_SEV_CRITICAL 2

// ── Max sizes ──────────────────────────────────────────────────────────
#define FAULT_CODE_LEN    32
#define FAULT_DESC_LEN   160
#define FAULT_QUEUE_SIZE   8   // Max queued faults before oldest is dropped

struct PendingFault {
    char     code[FAULT_CODE_LEN];
    char     description[FAULT_DESC_LEN];
    uint8_t  severity;          // FAULT_SEV_*
    uint32_t uptimeMs;          // millis() at time of fault
};

// ═══════════════════════════════════════════════════════════════════════
// FaultQueue — static singleton, no heap allocations
// ═══════════════════════════════════════════════════════════════════════
class FaultQueue {
public:
    /**
     * Push a new fault into the queue.
     * Can be called from ANY task / ISR-safe context.
     * If queue is full, the oldest entry is dropped to make room.
     *
     * @param code       Short identifier, e.g. "CAN_TX_FAIL"
     * @param description Human-readable explanation
     * @param severity   FAULT_SEV_INFO / WARNING / CRITICAL
     */
    static void push(const char* code, const char* description,
                     uint8_t severity = FAULT_SEV_CRITICAL);

    /**
     * Pop one fault from the queue (FIFO).
     * @param out  Filled on success.
     * @return true if a fault was available, false if queue empty.
     */
    static bool pop(PendingFault& out);

    /** Returns true if at least one fault is waiting. */
    static bool hasItems();

private:
    static PendingFault    _buf[FAULT_QUEUE_SIZE];
    static uint8_t         _head;   // next write position
    static uint8_t         _count;  // number of items in queue
    static SemaphoreHandle_t _mutex;

    static void ensureMutex();
};
