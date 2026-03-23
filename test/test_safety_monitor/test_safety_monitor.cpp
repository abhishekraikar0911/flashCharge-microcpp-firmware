/**
 * @file test/test_safety_monitor/test_safety_monitor.cpp
 * @brief Unity tests for ChargerSafetyPolicy::isSafeToCharge().
 *        Verifies that temperature, BMS timeout, and fault lock all prevent charging.
 */

#include "stubs/Arduino.h"
#include "stubs/freertos/semphr.h"

unsigned long g_mock_millis = 0;
FakeSerial    Serial;

#include "utils/charger_safety_policy.h"
#include <unity.h>

void setUp()    {}
void tearDown() {}

// ─── Helper: build a "all clear" base snapshot ───────────────────────────────

static StateSnapshot makeHealthySnapshot() {
    StateSnapshot snap;
    snap.chargerTemp            = 50.0f;   // well within limits
    snap.gunPhysicallyConnected = true;
    snap.batteryConnected       = true;
    snap.faultLockActive        = false;
    snap.lastBMS                = 10000;   // heard from BMS recently
    return snap;
}

// ─── Tests ──────────────────────────────────────────────────────────────────

void test_safe_when_all_conditions_green() {
    StateSnapshot snap = makeHealthySnapshot();
    // now = 11000 → only 1s since last BMS (well within 3s)
    TEST_ASSERT_TRUE(ChargerSafetyPolicy::isSafeToCharge(snap, 11000));
}

void test_unsafe_when_temperature_critical() {
    StateSnapshot snap = makeHealthySnapshot();
    snap.chargerTemp = 75.0f;   // exceeds MAX_SAFE_TEMP (70.0°C)
    TEST_ASSERT_FALSE(ChargerSafetyPolicy::isSafeToCharge(snap, 11000));
}

void test_unsafe_when_temperature_at_exact_limit() {
    StateSnapshot snap = makeHealthySnapshot();
    snap.chargerTemp = 70.0f;   // exactly at limit → still safe (<=)
    TEST_ASSERT_TRUE(ChargerSafetyPolicy::isSafeToCharge(snap, 11000));
}

void test_unsafe_when_temperature_just_over_limit() {
    StateSnapshot snap = makeHealthySnapshot();
    snap.chargerTemp = 70.1f;   // just over limit → UNSAFE
    TEST_ASSERT_FALSE(ChargerSafetyPolicy::isSafeToCharge(snap, 11000));
}

void test_unsafe_when_bms_timed_out() {
    StateSnapshot snap = makeHealthySnapshot();
    snap.lastBMS = 5000;   // BMS last heard at t=5s
    // now = 9s → 4s gap → timed out
    TEST_ASSERT_FALSE(ChargerSafetyPolicy::isSafeToCharge(snap, 9000));
}

void test_unsafe_when_fault_lock_active() {
    StateSnapshot snap = makeHealthySnapshot();
    snap.faultLockActive = true;
    TEST_ASSERT_FALSE(ChargerSafetyPolicy::isSafeToCharge(snap, 11000));
}

void test_unsafe_when_multiple_violations() {
    StateSnapshot snap = makeHealthySnapshot();
    snap.chargerTemp     = 80.0f;  // over-temp
    snap.faultLockActive = true;   // fault lock
    snap.lastBMS         = 0;      // BMS never responded
    TEST_ASSERT_FALSE(ChargerSafetyPolicy::isSafeToCharge(snap, 99999));
}

// ─── Runner ─────────────────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_safe_when_all_conditions_green);
    RUN_TEST(test_unsafe_when_temperature_critical);
    RUN_TEST(test_unsafe_when_temperature_at_exact_limit);
    RUN_TEST(test_unsafe_when_temperature_just_over_limit);
    RUN_TEST(test_unsafe_when_bms_timed_out);
    RUN_TEST(test_unsafe_when_fault_lock_active);
    RUN_TEST(test_unsafe_when_multiple_violations);
    return UNITY_END();
}
