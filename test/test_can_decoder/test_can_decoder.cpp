/**
 * @file test/test_can_decoder/test_can_decoder.cpp
 * @brief Native Unity tests for CANValidator logic used by the charger and BMS.
 */

#include "stubs/Arduino.h"
#include "stubs/driver/twai.h"
#include <unity.h>

// --- Production code under test ---
#include "utils/can_validator.h"

void setUp() {}
void tearDown() {}

// ─── Tests ──────────────────────────────────────────────────────────────────

void test_validate_message_structure() {
    twai_message_t msg;
    
    // Normal 8-byte message
    msg.data_length_code = 8;
    msg.rtr = 0;
    TEST_ASSERT_TRUE(CANValidator::validateMessage(msg));
    
    // Invalid DLC > 8
    msg.data_length_code = 9;
    TEST_ASSERT_FALSE(CANValidator::validateMessage(msg));
    
    // RTR frame with payload (not allowed)
    msg.data_length_code = 2;
    msg.rtr = 1;
    TEST_ASSERT_FALSE(CANValidator::validateMessage(msg));
    
    // RTR frame with no payload (allowed)
    msg.data_length_code = 0;
    msg.rtr = 1;
    TEST_ASSERT_TRUE(CANValidator::validateMessage(msg));
}

void test_validate_raw_data_noise_rejection() {
    uint8_t dlc = 8;
    uint8_t badDataFF[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t badData00[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t goodData[8]  = {0x00, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00};
    
    // Should reject all FF (bus-off or floating line)
    TEST_ASSERT_FALSE(CANValidator::validateRawData(badDataFF, dlc));
    
    // Should reject all 00 (shorted line)
    TEST_ASSERT_FALSE(CANValidator::validateRawData(badData00, dlc));
    
    // Should accept mixed data
    TEST_ASSERT_TRUE(CANValidator::validateRawData(goodData, dlc));
}

void test_validate_voltage_limits() {
    TEST_ASSERT_TRUE(CANValidator::validateVoltage(0.0f));
    TEST_ASSERT_TRUE(CANValidator::validateVoltage(86.5f));
    TEST_ASSERT_TRUE(CANValidator::validateVoltage(100.0f));
    
    // Out of bounds
    TEST_ASSERT_FALSE(CANValidator::validateVoltage(-0.1f));
    TEST_ASSERT_FALSE(CANValidator::validateVoltage(100.1f));
    TEST_ASSERT_FALSE(CANValidator::validateVoltage(999.0f));
}

void test_validate_current_limits() {
    TEST_ASSERT_TRUE(CANValidator::validateCurrent(0.0f));
    TEST_ASSERT_TRUE(CANValidator::validateCurrent(100.0f));
    TEST_ASSERT_TRUE(CANValidator::validateCurrent(350.0f));
    
    // Slight negative allowed for calibration offsets
    TEST_ASSERT_TRUE(CANValidator::validateCurrent(-5.0f));
    
    // Out of bounds
    TEST_ASSERT_FALSE(CANValidator::validateCurrent(-10.1f));
    TEST_ASSERT_FALSE(CANValidator::validateCurrent(350.1f));
}

void test_validate_temperature_limits() {
    TEST_ASSERT_TRUE(CANValidator::validateTemperature(25.0f));
    TEST_ASSERT_TRUE(CANValidator::validateTemperature(120.0f));
    TEST_ASSERT_TRUE(CANValidator::validateTemperature(-40.0f));
    
    // Out of bounds (e.g., from bit-flip corruption)
    TEST_ASSERT_FALSE(CANValidator::validateTemperature(121.0f));
    TEST_ASSERT_FALSE(CANValidator::validateTemperature(-41.0f));
    TEST_ASSERT_FALSE(CANValidator::validateTemperature(6553.5f)); 
}

// ─── Runner ─────────────────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_validate_message_structure);
    RUN_TEST(test_validate_raw_data_noise_rejection);
    RUN_TEST(test_validate_voltage_limits);
    RUN_TEST(test_validate_current_limits);
    RUN_TEST(test_validate_temperature_limits);
    return UNITY_END();
}
