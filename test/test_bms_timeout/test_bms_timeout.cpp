/**
 * @file test/test_bms_timeout/test_bms_timeout.cpp
 * @brief Unity tests for ChargerSafetyPolicy::isBMSTimedOut().
 *        Verifies that the BMS heartbeat timeout correctly identifies a disconnected vehicle.
 */

#include "stubs/Arduino.h"
#include "stubs/freertos/semphr.h"

unsigned long g_mock_millis = 0;
FakeSerial    Serial;

#include "utils/charger_safety_policy.h"
#include <unity.h>

void setUp()    {}
void tearDown() {}

// ─── Tests ──────────────────────────────────────────────────────────────────

void test_bms_timeout_not_triggered_when_fresh() {
    StateSnapshot snap;
    snap.gunPhysicallyConnected = true;
    snap.batteryConnected       = true;
    snap.lastBMS = 10000;  // BMS heard from at t=10s

    // Current time is only 1 second later — NOT timed out
    TEST_ASSERT_FALSE(ChargerSafetyPolicy::isBMSTimedOut(snap, 11000));
}

void test_bms_timeout_triggered_after_3s() {
    StateSnapshot snap;
    snap.gunPhysicallyConnected = true;
    snap.batteryConnected       = true;
    snap.lastBMS = 10000;  // BMS heard from at t=10s

    // Current time is 13.5 seconds — 3.5s gap — TIMED OUT
    TEST_ASSERT_TRUE(ChargerSafetyPolicy::isBMSTimedOut(snap, 13500));
}

void test_bms_timeout_ignored_when_gun_not_connected() {
    StateSnapshot snap;
    snap.gunPhysicallyConnected = false;
    snap.batteryConnected       = false;
    snap.lastBMS = 0;  // Never heard from BMS

    // Even if the BMS has been silent forever, not connected = not timed out
    TEST_ASSERT_FALSE(ChargerSafetyPolicy::isBMSTimedOut(snap, 99999));
}

void test_bms_timeout_exactly_at_boundary() {
    StateSnapshot snap;
    snap.gunPhysicallyConnected = true;
    snap.batteryConnected       = false;
    snap.lastBMS = 10000;

    // At EXACTLY 3000ms gap — boundary condition  (> not >=) — NOT timed out
    TEST_ASSERT_FALSE(ChargerSafetyPolicy::isBMSTimedOut(snap, 13000));
    // At 3001ms gap — TIMED OUT
    TEST_ASSERT_TRUE(ChargerSafetyPolicy::isBMSTimedOut(snap, 13001));
}

// ─── Runner ─────────────────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_bms_timeout_not_triggered_when_fresh);
    RUN_TEST(test_bms_timeout_triggered_after_3s);
    RUN_TEST(test_bms_timeout_ignored_when_gun_not_connected);
    RUN_TEST(test_bms_timeout_exactly_at_boundary);
    return UNITY_END();
}
