/**
 * @file ICan.h
 * @brief HAL interface for CAN bus peripheral
 * @layer HAL — MCU peripheral abstraction
 *
 * Implementations: Esp32TWAI (CAN1), Esp32MCP2515 (CAN2)
 * Do NOT include ESP32/Arduino headers above this layer.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include <stdint.h>

/**
 * @brief Standardized CAN frame structure for all HAL and Driver layers.
 */
struct CanFrame {
    uint32_t id;            // 11-bit or 29-bit CAN ID
    uint8_t  data[8];       // Payload bytes
    uint8_t  len;           // Payload length (0-8)
    bool     extended;      // true for 29-bit extended ID
    uint32_t timestamp_ms;  // Arrival time (for RX)
};

class ICan {
public:
    virtual ~ICan() = default;

    /** Initialize the CAN peripheral at the given baud rate. */
    virtual bool init(uint32_t baudrate) = 0;

    /**
     * Transmit a CAN frame.
     * @param frame The standardized frame to send
     * @return true if frame was enqueued successfully
     */
    virtual bool send(const CanFrame& frame) = 0;

    /**
     * Non-blocking receive.
     * @param frame [out] The standardized frame structure to fill
     * @return true if a frame was available
     */
    virtual bool receive(CanFrame& frame) = 0;

    /** @return true if the peripheral is online and error-free */
    virtual bool isHealthy() = 0;

    /** Attempt bus-off recovery and re-initialize the peripheral. */
    virtual void reset() = 0;
};
