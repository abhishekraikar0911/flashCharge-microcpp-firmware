#pragma once

/**
 * @file bms_interface.h
 * @brief Battery Management System Interface
 * @author Rivot Motors
 * @date 2026
 *
 * This module handles communication with the BMS (Battery Management System)
 * and maintains battery state information.
 */

#include <stdint.h>
#include <stdbool.h>

// ========== BMS DATA STRUCTURES ==========

/// Battery cell voltages (packed)
struct BatteryVoltages
{
    float pack_voltage;
    float max_cell_voltage;
    float min_cell_voltage;
    uint8_t cell_count;
};

/// Battery temperature readings
struct BatteryTemperature
{
    float mosfet_temp;
    float cell_max_temp;
    float cell_min_temp;
};

/// Battery State of Charge
struct BatterySOC
{
    float soc_percent;
    float soh_percent; // State of Health
    uint32_t remaining_capacity_ah;
    uint32_t total_capacity_ah;
};

/// Complete battery state
struct BatteryState
{
    BatteryVoltages voltages;
    BatteryTemperature temperature;
    BatterySOC soc;
    float discharge_current_a;
    float charge_current_a;
    uint8_t status_flags;
    uint32_t timestamp_ms;
    bool is_valid;
};

// ========== BMS INTERFACE ==========

namespace BMS
{

    /**
     * @brief Initialize BMS communication
     * @return true if initialization successful
     */
    bool init();

    /**
     * @brief Get current battery state
     * @return Battery state structure with latest values
     */
    BatteryState getState();

    /**
     * @brief Update battery state from CAN message
     * @param can_id CAN message ID
     * @param data CAN data payload
     * @param length Payload length
     */
    void handleCanMessage(uint32_t can_id, const uint8_t *data, uint8_t length);

    /**
     * @brief Request State of Charge from BMS
     */
    void requestSOC();

    /**
     * @brief Request voltage readings from BMS
     */
    void requestVoltages();

    /**
     * @brief Check if BMS is communicating (alive)
     * @return true if recent messages received within timeout
     */
    bool isAlive();

    /**
     * @brief Get time since last valid message (milliseconds)
     * @return Time in milliseconds
     */
    uint32_t getTimeSinceLastMessage();

    /**
     * @brief Check if battery is in safe operating state
     * @return true if all safety parameters are within limits
     */
    bool isSafeState();

    /**
     * @brief Get BMS error message if any
     * @return Error code, 0 if no error
     */
    uint8_t getErrorCode();

} // namespace BMS
