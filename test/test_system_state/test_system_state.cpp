/**
 * @file test/test_system_state/test_system_state.cpp
 * @brief Unity tests for SystemState: verifies thread-safe getters/setters
 *        and snapshot() function work correctly without the ESP32 hardware.
 */

// --- Stub headers must come first so they shadow real <Arduino.h>/<freertos/semphr.h> ---
#include "stubs/Arduino.h"
#include "stubs/freertos/semphr.h"

// --- Stub instance definitions ---
unsigned long g_mock_millis = 0;
FakeSerial    Serial;

// --- Production code under test ---
#include "modules/system_state.h"

// --- Unity test framework ---
#include <unity.h>

// ─── Helpers ────────────────────────────────────────────────────────────────

static SystemState& S() { return SystemState::instance(); }

void setUp()    {}  // called before each test
void tearDown() {}  // called after each test

// ─── Tests ──────────────────────────────────────────────────────────────────

void test_charger_temp_set_get() {
    S().setChargerTemp(85.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 85.5f, S().getChargerTemp());
}

void test_soc_set_get() {
    S().setSocPercent(78.3f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 78.3f, S().getSocPercent());
}

void test_battery_connected_flag() {
    S().setBatteryConnected(true);
    TEST_ASSERT_TRUE(S().getBatteryConnected());
    S().setBatteryConnected(false);
    TEST_ASSERT_FALSE(S().getBatteryConnected());
}

void test_charging_enabled_flag() {
    S().setChargingEnabled(true);
    TEST_ASSERT_TRUE(S().getChargingEnabled());
    S().setChargingEnabled(false);
    TEST_ASSERT_FALSE(S().getChargingEnabled());
}

void test_snapshot_is_consistent() {
    S().setTerminalVolt(76.4f);
    S().setTerminalCurr(12.1f);
    S().setBMS_Vmax(84.0f);

    StateSnapshot snap = S().snapshot();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 76.4f, snap.terminalVolt);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.1f, snap.terminalCurr);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 84.0f, snap.BMS_Vmax);
}

void test_last_bms_timestamp() {
    g_mock_millis = 12345;
    S().setLastBMS(millis());
    TEST_ASSERT_EQUAL_UINT32(12345, S().getLastBMS());
}

// ─── Runner ─────────────────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_charger_temp_set_get);
    RUN_TEST(test_soc_set_get);
    RUN_TEST(test_battery_connected_flag);
    RUN_TEST(test_charging_enabled_flag);
    RUN_TEST(test_snapshot_is_consistent);
    RUN_TEST(test_last_bms_timestamp);
    return UNITY_END();
}
