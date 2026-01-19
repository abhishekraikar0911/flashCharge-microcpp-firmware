#pragma once

/**
 * @file charger_interface.h
 * @brief Charger Communication Interface
 * @author Rivot Motors
 * @date 2026
 *
 * This module handles communication with the charger and monitors charging status.
 */

#include <stdint.h>
#include <stdbool.h>

// ========== CHARGER STATE DEFINITIONS ==========

typedef enum
{
    CHARGER_STATE_IDLE = 0,
    CHARGER_STATE_CHARGING = 1,
    CHARGER_STATE_CHARGING_ENABLED = 2,
    CHARGER_STATE_FAULT = 3,
    CHARGER_STATE_OFFLINE = 4
} ChargerState;

// ========== CHARGER DATA STRUCTURES ==========

/// Charger status information
struct ChargerStatus
{
    ChargerState state;
    float output_voltage;
    float output_current;
    float temperature;
    uint16_t fault_code;
    bool relay_enabled;
    bool grid_connected;
    uint32_t timestamp_ms;
    bool is_valid;
};

/// Charger terminal information
struct TerminalStatus
{
    bool connected;
    float voltage_available;
    float current_available;
    char model[32];
    uint32_t last_update_ms;
};

// ========== CHARGER INTERFACE ==========

namespace Charger
{

    /**
     * @brief Initialize charger communication
     * @return true if successful
     */
    bool init();

    /**
     * @brief Get current charger status
     * @return Charger status structure
     */
    ChargerStatus getStatus();

    /**
     * @brief Get terminal status
     * @return Terminal status information
     */
    TerminalStatus getTerminalStatus();

    /**
     * @brief Enable charging output
     * @param enable true to enable, false to disable
     * @return true if command sent successfully
     */
    bool setChargingEnabled(bool enable);

    /**
     * @brief Set charging current limit
     * @param current_a Maximum current in Amperes
     * @return true if command sent successfully
     */
    bool setChargingCurrent(float current_a);

    /**
     * @brief Handle CAN message from charger
     * @param can_id CAN message ID
     * @param data CAN data payload
     * @param length Payload length
     */
    void handleCanMessage(uint32_t can_id, const uint8_t *data, uint8_t length);

    /**
     * @brief Check if charger is communicating (alive)
     * @return true if recent messages received within timeout
     */
    bool isAlive();

    /**
     * @brief Get time since last valid message (milliseconds)
     * @return Time in milliseconds
     */
    uint32_t getTimeSinceLastMessage();

    /**
     * @brief Get charger error message if any
     * @return Error code, 0 if no error
     */
    uint8_t getErrorCode();

    /**
     * @brief Get human-readable error string
     * @param error_code Error code from fault
     * @return Error description string
     */
    const char *getErrorString(uint8_t error_code);

} // namespace Charger
