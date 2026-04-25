/**
 * @file Esp32MCP2515.h
 * @brief ESP32 HAL implementation for MCP2515 external SPI CAN controller
 * @layer HAL
 *
 * Key design decisions for vehicle CAN robustness:
 *  - Hardware acceptance filters limit RX to BMS-only IDs.
 *  - Dedicated FreeRTOS task drains the 2-frame hardware buffer.
 *  - INT pin (GPIO 34, active-LOW) triggers ISR → task notification,
 *    replacing the 2ms polling timer with true event-driven drain.
 *  - Mutex protects all SPI transactions from concurrent task access.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "hal/interfaces/ICan.h"
#include <mcp2515.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/task.h>

class Esp32MCP2515 : public ICan {
public:
    /**
     * @param csPin   SPI Chip Select pin
     * @param freq    Oscillator frequency (e.g. MCP_8MHZ) — must match physical crystal
     * @param intPin  MCP2515 INT pin (active-LOW open-drain, GPIO 34).
     *                Pass -1 to fall back to 2ms polling mode.
     */
    Esp32MCP2515(int csPin, CAN_CLOCK freq = MCP_8MHZ, int intPin = -1);
    virtual ~Esp32MCP2515() = default;

    bool init(uint32_t baudrate) override;
    bool send(const CanFrame& frame) override;

    /**
     * Non-blocking receive from the internal software RX queue.
     * The queue is populated by drainHardwareBuffer() which must be called
     * from a high-priority dedicated task.
     */
    bool receive(CanFrame& frame) override;

    /**
     * @return true if MCP2515 is NOT in Bus-Off state.
     * Also clears RX overflow flags (EFLG.RX0OVR / RX1OVR).
     */
    bool isHealthy() override;

    /**
     * Re-initialize MCP2515 to recover from Bus-Off.
     * Re-applies acceptance filters and restores Normal mode.
     */
    void reset() override;

    /**
     * @brief Drain MCP2515 hardware RX buffers into the internal SW queue.
     *
     * INT mode  : called by CAN2_RX task when ISR fires (zero-latency).
     * Polling mode: called every 2ms from the CAN2_RX task loop.
     *
     * Thread-safe: protected by internal SPI mutex.
     */
    void drainHardwareBuffer();

    /**
     * @brief Register the CAN2_RX FreeRTOS task for ISR wake-up.
     *
     * Must be called AFTER xTaskCreate() for the CAN2_RX task and AFTER
     * init() has been called (so the INT ISR is already attached).
     * Once set, the INT ISR notifies this task instead of the task sleeping
     * for 2ms — latency drops from ≤2ms to <10µs.
     */
    void setNotifyTask(TaskHandle_t handle);

private:
    MCP2515*          mcp;
    CAN_CLOCK         clockFreq;
    bool              isInit;
    uint32_t          currentBaud;
    int               csPin;
    int               intPin;          ///< GPIO 34 (INT, active-LOW). -1 = polling.
    volatile TaskHandle_t rxTaskHandle; ///< Task to wake from ISR. nullptr = polling.
    SemaphoreHandle_t mutex;
    QueueHandle_t     rxQueue;

    static const int RX_QUEUE_SIZE = 16;

    CAN_SPEED getMcpSpeed(uint32_t baudrate);
    void      applyFilters();

    /// ISR handler — MUST be in IRAM, calls vTaskNotifyGiveFromISR.
    static void IRAM_ATTR intISR(void* arg);
};
