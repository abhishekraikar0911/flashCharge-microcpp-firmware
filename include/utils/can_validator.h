#ifndef CAN_VALIDATOR_H
#define CAN_VALIDATOR_H

#include <Arduino.h>
#include <driver/twai.h>

/**
 * @file can_validator.h
 * @brief Input validation for CAN messages to prevent malformed data processing
 */

namespace CANValidator
{
    /**
     * @brief Validate CAN message structure
     * @param msg CAN message to validate
     * @return true if valid, false otherwise
     */
    inline bool validateMessage(const twai_message_t &msg)
    {
        // Check DLC is within valid range
        if (msg.data_length_code > 8)
            return false;

        // For RTR frames, data should be ignored
        if (msg.rtr && msg.data_length_code > 0)
            return false;

        return true;
    }

    /**
     * @brief Validate voltage reading from CAN
     * @param voltage Voltage value to validate
     * @return true if within safe operating range
     */
    inline bool validateVoltage(float voltage)
    {
        return (voltage >= 0.0f && voltage <= 100.0f);
    }

    /**
     * @brief Validate current reading from CAN
     * @param current Current value to validate
     * @return true if within safe operating range
     */
    inline bool validateCurrent(float current)
    {
        return (current >= -10.0f && current <= 350.0f);
    }

    /**
     * @brief Validate SOC percentage
     * @param soc State of charge percentage
     * @return true if within valid range
     */
    inline bool validateSOC(float soc)
    {
        return (soc >= 0.0f && soc <= 100.0f);
    }

    /**
     * @brief Validate temperature reading
     * @param temp Temperature in Celsius
     * @return true if within reasonable range
     */
    inline bool validateTemperature(float temp)
    {
        return (temp >= -40.0f && temp <= 120.0f);
    }

    /**
     * @brief Check if raw CAN data appears to be noise/garbage
     * @param data Pointer to 8-byte CAN data
     * @param dlc Data length code
     * @return true if data appears valid
     */
    inline bool validateRawData(const uint8_t *data, uint8_t dlc)
    {
        if (!data || dlc > 8)
            return false;

        // Check for all 0xFF (common bus-off pattern)
        bool allFF = true;
        for (uint8_t i = 0; i < dlc; i++)
        {
            if (data[i] != 0xFF)
            {
                allFF = false;
                break;
            }
        }

        // Check for all 0x00 (another common error pattern)
        bool allZero = true;
        for (uint8_t i = 0; i < dlc; i++)
        {
            if (data[i] != 0x00)
            {
                allZero = false;
                break;
            }
        }

        // Reject if all bytes are identical (likely noise)
        return !(allFF || allZero);
    }

    /**
     * @brief Validate CAN ID is in expected range
     * @param id CAN identifier
     * @param isExtended Whether ID is extended format
     * @return true if valid
     */
    inline bool validateCANID(uint32_t id, bool isExtended)
    {
        if (isExtended)
        {
            return (id <= 0x1FFFFFFF);
        }
        else
        {
            return (id <= 0x7FF);
        }
    }
}

#endif // CAN_VALIDATOR_H
