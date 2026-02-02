#pragma once

/**
 * @file can_twai_driver.h
 * @brief TWAI (ISO1050) CAN Driver for Charger Module Communication
 * @author Rivot Motors
 * @date 2025
 */

#include <Arduino.h>
#include <driver/twai.h>
#include <stdint.h>
#include "../../include/header.h"

#ifndef CAN_MESSAGE_STRUCT
#define CAN_MESSAGE_STRUCT
// Unified CAN message structure (hardware-agnostic)
struct CanMessage
{
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    bool extended;
    uint32_t timestamp_ms;
};
#endif

// Driver status
struct CanTwaiStatus
{
    bool is_initialized;
    bool is_active;
    uint32_t total_rx_messages;
    uint32_t total_tx_messages;
    uint32_t error_count;
    uint32_t last_activity_ms;
};

namespace CAN_TWAI
{
    /**
     * @brief Initialize TWAI CAN driver
     * @return true if initialization successful
     */
    bool init();

    /**
     * @brief Deinitialize TWAI driver
     * @return true if successful
     */
    bool deinit();

    /**
     * @brief Check if driver is active
     * @return true if active and ready
     */
    bool isActive();

    /**
     * @brief Send CAN message
     * @param id CAN message ID
     * @param data Pointer to data buffer (0-8 bytes)
     * @param length Data length (0-8)
     * @param is_extended true for 29-bit ID, false for 11-bit ID
     * @return true if message sent successfully
     */
    bool sendMessage(uint32_t id, const uint8_t *data, uint8_t length, bool is_extended = true);

    /**
     * @brief Receive CAN message (non-blocking)
     * @param[out] msg Pointer to CanMessage structure to fill
     * @return true if message received, false if queue empty
     */
    bool receiveMessage(CanMessage *msg);

    /**
     * @brief Pop frame from buffer (legacy compatibility)
     * @param[out] out RxBufItem structure
     * @return true if message available
     */
    bool popFrame(RxBufItem &out);

    /**
     * @brief Get driver status
     * @return Current driver status
     */
    CanTwaiStatus getStatus();

    /**
     * @brief Flush RX buffer
     */
    void flushRxBuffer();

    /**
     * @brief Get RX buffer usage percentage
     * @return Percentage (0-100)
     */
    uint8_t getRxBufferUsage();

    /**
     * @brief Reset statistics
     */
    void resetStatistics();

    /**
     * @brief Check if TWAI is healthy (receiving messages)
     * @return true if recent activity detected
     */
    bool isHealthy();

} // namespace CAN_TWAI
