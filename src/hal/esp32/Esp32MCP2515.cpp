/**
 * @file Esp32MCP2515.cpp
 * @brief ESP32 HAL implementation for MCP2515 external SPI CAN controller
 * @layer HAL
 *
 * Fix summary for vehicle CAN high-traffic robustness:
 *
 *  [P0] applyFilters()        — Hardware acceptance filters: accept ONLY BMS IDs
 *                               (0x1806E5F4 and 0x18904001). All VCU/OBC/ECU
 *                               frames are rejected before entering HW buffers.
 *
 *  [P1] isHealthy()           — Actively clears latched EFLG.RX0OVR/RX1OVR bits.
 *                               Detects and logs Bus-Off state.
 *
 *  [P1] reset()               — Re-applies filters after every reset, ensuring
 *                               recovery doesn't re-expose the bus to overflow.
 *
 *  [P1] send()                — TX retry with backoff + Bus-Off detection + mutex.
 *
 *  [P2] drainHardwareBuffer() — Called by dedicated CAN2_RX task (priority 8).
 *                               Reads from 2 HW buffers into 16-frame SW queue.
 *
 *  [P2] receive()             — Reads from SW queue, not directly from hardware.
 *                               Decouples drain speed from driver polling rate.
 *
 *  [P2] Mutex                 — All SPI operations protected for FreeRTOS safety.
 *
 * @author Rivot Motors
 * @date 2026
 */
#include "hal/esp32/Esp32MCP2515.h"
#include <Arduino.h>
#include "system/SafeSerial.h"

// Override Serial to automatically suppress all logs when provisioning wizard is active
#define Serial SafeSerial::SafeSerialObj

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Esp32MCP2515::Esp32MCP2515(int csPin, CAN_CLOCK freq, int intPin)
    : mcp(nullptr), clockFreq(freq), isInit(false), currentBaud(250000),
      csPin(csPin), intPin(intPin), rxTaskHandle(nullptr) {
    mutex   = xSemaphoreCreateMutex();
    rxQueue = xQueueCreate(RX_QUEUE_SIZE, sizeof(CanFrame));
}

// ---------------------------------------------------------------------------
// init() — P0: Configure filters before entering Normal mode
// ---------------------------------------------------------------------------

bool Esp32MCP2515::init(uint32_t baudrate) {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    if (mcp == nullptr) {
        mcp = new MCP2515(csPin);
    }
    currentBaud = baudrate;

    // Step 1: Hardware reset → enters configuration mode
    mcp->reset();

    // Step 2: Set bitrate (stays in configuration mode)
    CAN_SPEED speed = getMcpSpeed(baudrate);
    if (mcp->setBitrate(speed, clockFreq) != MCP2515::ERROR_OK) {
        Serial.println("[HAL_CAN2] ❌ setBitrate() failed!");
        xSemaphoreGive(mutex);
        return false;
    }

    // Step 3: [P0] Apply BMS-only acceptance filters while still in config mode.
    // This MUST happen before setNormalMode/setListenOnlyMode.
    applyFilters();

    // Step 4: Enter Normal mode to allow full two-way communication (TX/RX)
    if (mcp->setNormalMode() != MCP2515::ERROR_OK) {
        Serial.println("[HAL_CAN2] ❌ setNormalMode() failed!");
        xSemaphoreGive(mutex);
        return false;
    }
    Serial.println("[HAL_CAN2] 📡 Normal Mode Active (TX/RX)");

    isInit = true;
    xSemaphoreGive(mutex);

    // INT pin setup — attach ISR if intPin is configured.
    // GPIO 34 is input-only on ESP32; no internal pull-up available.
    // The MCP2515 module board has a pull-up resistor (4.7kΩ) to VCC.
    // INT is open-drain active-LOW: LOW = message waiting or error condition.
    if (intPin >= 0) {
        pinMode(intPin, INPUT);
        attachInterruptArg(digitalPinToInterrupt(intPin), intISR, this, FALLING);
        Serial.printf("[HAL_CAN2] ✅ MCP2515 init OK — %lu bps, 8MHz crystal, INT mode (GPIO %d)\n",
                      (unsigned long)baudrate, intPin);
    } else {
        Serial.printf("[HAL_CAN2] ✅ MCP2515 init OK — %lu bps, 8MHz crystal, POLLING mode\n",
                      (unsigned long)baudrate);
    }
    return true;
}

// ---------------------------------------------------------------------------
// send() — P1: Mutex + retry + Bus-Off detection
// ---------------------------------------------------------------------------

bool Esp32MCP2515::send(const CanFrame& frame) {
    if (!isInit) return false;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;

    can_frame mcpFrame;
    mcpFrame.can_id  = frame.id;
    if (frame.extended) {
        mcpFrame.can_id |= CAN_EFF_FLAG;
    }
    mcpFrame.can_dlc = (frame.len > 8) ? 8 : frame.len;
    for (int i = 0; i < mcpFrame.can_dlc; i++) {
        mcpFrame.data[i] = frame.data[i];
    }

    // [P1] Retry up to 3 times with backoff for ALLTXBUSY
    MCP2515::ERROR err = MCP2515::ERROR_FAILTX;
    for (int attempt = 0; attempt < 3; attempt++) {
        err = mcp->sendMessage(&mcpFrame);
        if (err == MCP2515::ERROR_OK)    break;
        if (err == MCP2515::ERROR_FAILTX) break; // Bus-Off — retrying is pointless
        // ERROR_ALLTXBUSY: all 3 TX buffers in use; release mutex, wait, retry
        xSemaphoreGive(mutex);
        delayMicroseconds(500 * (attempt + 1)); // 0.5ms, 1ms, 1.5ms
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    }

    if (err != MCP2515::ERROR_OK) {
        uint8_t eflg = mcp->getErrorFlags();
        static uint32_t lastErrLog = 0;
        if (millis() - lastErrLog > 3000) {
            lastErrLog = millis();
            Serial.printf("[HAL_CAN2] TX FAIL 0x%08lX err=%d EFLG=0x%02X",
                          (long unsigned int)frame.id, (int)err, eflg);
            if (eflg & MCP2515::EFLG_TXBO)  Serial.print(" [BUS-OFF]");
            if (eflg & MCP2515::EFLG_TXEP)  Serial.print(" [TX-ERR-PASSIVE]");
            if (eflg & MCP2515::EFLG_RXEP)  Serial.print(" [RX-ERR-PASSIVE]");
            Serial.println();
        }
    }

    xSemaphoreGive(mutex);
    return (err == MCP2515::ERROR_OK);
}

// ---------------------------------------------------------------------------
// receive() — P2: Pop from software queue (filled by drainHardwareBuffer)
// ---------------------------------------------------------------------------

bool Esp32MCP2515::receive(CanFrame& frame) {
    if (!isInit) return false;
    // Non-blocking pop from the software queue.
    // drainHardwareBuffer() must be called from a dedicated task to fill it.
    if (xQueueReceive(rxQueue, &frame, 0) == pdTRUE) {
        // Success logs removed to prevent console spam
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// isHealthy() — P1: Detect Bus-Off + clear latched RX overflow flags
// ---------------------------------------------------------------------------

bool Esp32MCP2515::isHealthy() {
    if (!isInit) return false;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) != pdTRUE) return true;

    uint8_t eflg = mcp->getErrorFlags();
    // EFLG encodes TEC/REC ranges — no need to read private registers:
    //   TXWAR(bit4)=TEC≥96  TXEP(bit6)=TEC≥128  TXBO(bit7)=TEC=256(Bus-Off)
    //   RXWAR(bit3)=REC≥96  RXEP(bit5)=REC≥128
    const char* tecRange = (eflg & MCP2515::EFLG_TXBO) ? "256(Bus-Off)" :
                           (eflg & MCP2515::EFLG_TXEP)  ? "128-255(ErrPassive)" :
                           (eflg & MCP2515::EFLG_TXWAR) ? "96-127(Warning)" : "<96(OK)";
    const char* recRange = (eflg & MCP2515::EFLG_RXEP)  ? "≥128(ErrPassive)" :
                           (eflg & MCP2515::EFLG_RXWAR) ? "96-127(Warning)" : "<96(OK)";


    // Clear latched RX overflow flags (they NEVER auto-clear)
    if (eflg & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR)) {
        mcp->clearRXnOVR();
        static uint32_t lastOvrLog = 0;
        if (millis() - lastOvrLog > 2000) {
            lastOvrLog = millis();
            Serial.printf("[HAL_CAN2] ⚠️  RX Overflow! EFLG=0x%02X TEC=%s REC=%s\n",
                          eflg, tecRange, recRange);
        }
    }

    bool busOff = (eflg & MCP2515::EFLG_TXBO) != 0;
    xSemaphoreGive(mutex);

    if (busOff) {
        static uint32_t lastBoLog = 0;
        if (millis() - lastBoLog > 1000) {
            lastBoLog = millis();
            // DIAGNOSTIC: TEC range tells us if it's a TX problem; REC range = RX/bus noise
            if (!SafeSerial::isSuppressed()) {
                Serial.printf("[HAL_CAN2] 🚨 Bus-Off! EFLG=0x%02X | TEC=%s | REC=%s → %s\n",
                              eflg, tecRange, recRange,
                              (eflg & (MCP2515::EFLG_TXBO | MCP2515::EFLG_TXEP | MCP2515::EFLG_TXWAR))
                                  ? "TX fault — check: transceiver RS/STB pin, termination, CANH/CANL polarity"
                                  : "RX fault — check: ground isolation, shielding, bus load");
            }
        }
    }
    return !busOff;
}


// ---------------------------------------------------------------------------
// drainHardwareBuffer() — P2: Called by dedicated CAN2_RX task at priority 8
// ---------------------------------------------------------------------------

void Esp32MCP2515::drainHardwareBuffer() {
    if (!isInit) return;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

    // [P1] Clear error interrupts to prevent INT pin from getting stuck LOW
    uint8_t intf = mcp->getInterrupts();
    if (intf & (MCP2515::CANINTF_ERRIF | MCP2515::CANINTF_MERRF)) {
        if (intf & MCP2515::CANINTF_ERRIF) mcp->clearERRIF();
        if (intf & MCP2515::CANINTF_MERRF) mcp->clearMERR();
    }

    // [P1] Check and clear overflow flags before reading —
    // overflow flag must be cleared for MCP2515 to resume accepting new frames.
    uint8_t eflg = mcp->getErrorFlags();
    if (eflg & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR)) {
        mcp->clearRXnOVR();
        static uint32_t lastOvrLog = 0;
        if (millis() - lastOvrLog > 2000) {
            lastOvrLog = millis();
            Serial.printf("[HAL_CAN2] ⚠️  Overflow cleared in drain task (EFLG=0x%02X)\n", eflg);
        }
    }

    // Drain all frames from the 2 hardware RX buffers into the software queue.
    // Safety cap at 8 iterations so we don't hold the mutex indefinitely.
    can_frame mcpFrame;
    int drained = 0;
    while (drained < 8 && mcp->readMessage(&mcpFrame) == MCP2515::ERROR_OK) {
        CanFrame frame;
        frame.id           = mcpFrame.can_id & 0x1FFFFFFF;
        frame.len          = mcpFrame.can_dlc;
        frame.extended     = (mcpFrame.can_id & CAN_EFF_FLAG) != 0;
        frame.timestamp_ms = (uint32_t)millis();
        for (int i = 0; i < frame.len; i++) {
            frame.data[i] = mcpFrame.data[i];
        }
        // Non-blocking push. If SW queue is full, drop the frame
        // (shouldn't happen with filters active — only 2 BMS IDs accepted).
        xQueueSend(rxQueue, &frame, 0);
        drained++;
    }

    xSemaphoreGive(mutex);
}

// ---------------------------------------------------------------------------
// reset() — P1: Full recovery with filter re-application
// ---------------------------------------------------------------------------

void Esp32MCP2515::reset() {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(500)) != pdTRUE) return;

    if (!SafeSerial::isSuppressed()) {
        Serial.println("[HAL_CAN2] 🔄 Resetting MCP2515 for Bus-Off recovery...");
    }

    mcp->reset();                                // → configuration mode
    CAN_SPEED speed = getMcpSpeed(currentBaud);
    mcp->setBitrate(speed, clockFreq);

    // [P1] CRITICAL: Re-apply filters after every reset.
    // Without this, recovery would return to promiscuous mode → overflow again.
    applyFilters();

    mcp->setNormalMode();

    // Flush stale software queue so driver doesn't process old frames
    xQueueReset(rxQueue);

    xSemaphoreGive(mutex);
    if (!SafeSerial::isSuppressed()) {
        Serial.println("[HAL_CAN2] ✅ MCP2515 reset complete — filters restored, bus rejoined");
    }
}

// ---------------------------------------------------------------------------
// applyFilters() — P0: BMS-only acceptance filters (call in config mode only)
// ---------------------------------------------------------------------------

/**
 * MCP2515 filter architecture:
 *   RXB0: served by MASK0 and RXF0, RXF1
 *   RXB1: served by MASK1 and RXF2, RXF3, RXF4, RXF5
 *
 * Strategy:
 *   Both buffers → accept ONLY 0x1806E5F4 (BMS charge request)
 *   This frame carries Vmax (bytes 0-1), Imax (bytes 2-3),
 *   Fault flags (byte 4), and SOC (bytes 5-6) — all in one frame.
 *
 *   0x18904001 (old SOC response) is now REJECTED at hardware level.
 *   No separate SOC query is sent — SOC is read from 0x1806E5F4 bytes 5-6.
 */
void Esp32MCP2515::applyFilters() {
    // --- RXB0: Accept ONLY 0x1806E5F4 ---
    mcp->setFilterMask(MCP2515::MASK0, /*extended=*/true, 0x1FFFFFFF);
    mcp->setFilter(MCP2515::RXF0,      /*extended=*/true, 0x1806E5F4);
    mcp->setFilter(MCP2515::RXF1,      /*extended=*/true, 0x1806E5F4);

    // --- RXB1: Accept ONLY 0x1806E5F4 (same — no separate SOC frame needed) ---
    mcp->setFilterMask(MCP2515::MASK1, /*extended=*/true, 0x1FFFFFFF);
    mcp->setFilter(MCP2515::RXF2,      /*extended=*/true, 0x1806E5F4);
    mcp->setFilter(MCP2515::RXF3,      /*extended=*/true, 0x1806E5F4);
    mcp->setFilter(MCP2515::RXF4,      /*extended=*/true, 0x1806E5F4);
    mcp->setFilter(MCP2515::RXF5,      /*extended=*/true, 0x1806E5F4);

    if (!SafeSerial::isSuppressed()) {
        Serial.println("[HAL_CAN2] ✅ HW filters: RXB0=RXB1=0x1806E5F4 only (0x18904001 blocked)");
    }
}

// ---------------------------------------------------------------------------
// getMcpSpeed()
// ---------------------------------------------------------------------------

CAN_SPEED Esp32MCP2515::getMcpSpeed(uint32_t baudrate) {
    switch (baudrate) {
        case 125000:  return CAN_125KBPS;
        case 250000:  return CAN_250KBPS;
        case 500000:  return CAN_500KBPS;
        case 1000000: return CAN_1000KBPS;
        default:      return CAN_250KBPS;
    }
}

// ---------------------------------------------------------------------------
// setNotifyTask() — register the CAN2_RX task for ISR wake-up
// ---------------------------------------------------------------------------

void Esp32MCP2515::setNotifyTask(TaskHandle_t handle) {
    rxTaskHandle = handle;
    if (handle != nullptr) {
        Serial.println("[HAL_CAN2] ✅ INT-driven mode armed — CAN2_RX task registered");
    }
}

// ---------------------------------------------------------------------------
// intISR() — MCP2515 INT falling-edge ISR (stored in IRAM)
//
// Called in ISR context (no Serial, no malloc, no FreeRTOS blocking calls).
// Uses vTaskNotifyGiveFromISR() — the lightest possible task wakeup.
// portYIELD_FROM_ISR() triggers an immediate context switch so the
// CAN2_RX task preempts whatever was running, minimising frame latency.
// ---------------------------------------------------------------------------

void IRAM_ATTR Esp32MCP2515::intISR(void* arg) {
    Esp32MCP2515* self = static_cast<Esp32MCP2515*>(arg);
    if (self == nullptr) return;

    BaseType_t higherPriorityTaskWoken = pdFALSE;

    TaskHandle_t task = self->rxTaskHandle;   // volatile read is safe in ISR
    if (task != nullptr) {
        vTaskNotifyGiveFromISR(task, &higherPriorityTaskWoken);
        // FIX: Do NOT call portYIELD_FROM_ISR() here!
        // During OTA flash erases, the ESP32 disables the SPI flash cache.
        // If an immediate context switch occurs to the CAN2_RX task (which is in flash),
        // it causes a fatal "Cache disabled but cached memory region accessed" panic.
        // By omitting yield, the task switch is deferred until the next RTOS tick 
        // (which safely respects cache locks). Max added latency is 1ms.
    }
}
