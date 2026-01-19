#pragma once

/**
 * @file can_driver.h
 * @brief CAN/TWAI Driver Interface for ESP32
 * @author Rivot Motors
 * @date 2026
 *
 * This module provides hardware abstraction for the CAN bus interface.
 * It handles:
 * - CAN bus initialization and configuration
 * - Message transmission and reception
 * - Error handling and status monitoring
 * - Ring buffer management for received messages
 */

#include <Arduino.h>
#include <driver/twai.h>
#include <stdint.h>
#include <string>

// ========== DATA STRUCTURES ==========

// RxBufItem is defined in header.h for backward compatibility

/// CAN driver status
struct CanStatus
{
    bool is_initialized;
    bool is_active;
    uint32_t total_rx_messages;
    uint32_t total_tx_messages;
    uint32_t error_count;
    uint32_t last_activity_ms;
};

// ========== DRIVER INTERFACE ==========

namespace CAN
{

    /**
     * @brief Initialize CAN bus (TWAI) driver
     * @return true if initialization successful, false otherwise
     */
    bool init();

    /**
     * @brief Deinitialize CAN bus driver
     * @return true if successful, false otherwise
     */
    bool deinit();

    /**
     * @brief Check if CAN driver is active
     * @return true if active and ready for communication
     */
    bool isActive();

    /**
     * @brief Send CAN message
     * @param id CAN message ID
     * @param data Pointer to data buffer (0-8 bytes)
     * @param length Data length (0-8)
     * @param is_extended true for 29-bit ID, false for 11-bit ID
     * @return true if message queued successfully
     */
    bool sendMessage(uint32_t id, const uint8_t *data, uint8_t length, bool is_extended = false);

    /**
     * @brief Receive CAN message (non-blocking)
     * @param[out] frame Pointer to frame structure to fill
     * @param[out] timestamp_ms Optional timestamp of reception
     * @return true if message received, false if queue empty
     */
    bool receiveMessage(twai_message_t *frame, uint32_t *timestamp_ms = nullptr);

    /**
     * @brief Peek at next message without removing it
     * @param[out] frame Pointer to frame structure to fill
     * @return true if message available, false if queue empty
     */
    bool peekMessage(twai_message_t *frame);

    /**
     * @brief Get driver status
     * @return Current CAN driver status structure
     */
    CanStatus getStatus();

    /**
     * @brief Clear all received messages from queue
     */
    void flushRxBuffer();

    /**
     * @brief Get receive buffer usage percentage
     * @return Percentage of buffer capacity in use (0-100)
     */
    uint8_t getRxBufferUsage();

    /**
     * @brief Reset driver statistics
     */
    void resetStatistics();

} // namespace CAN
