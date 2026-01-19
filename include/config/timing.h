#pragma once

/**
 * @file timing.h
 * @brief Timing constants for all system operations
 * @author Rivot Motors
 * @date 2026
 */

// ========== COMMUNICATION TIMEOUTS (milliseconds) ==========
#define BMS_TIMEOUT_MS 5000
#define CHARGER_RESPONSE_TIMEOUT_MS 3000
#define HEARTBEAT_INTERVAL_MS 100
#define SOC_REQUEST_INTERVAL_MS 2000
#define GROUP_REQUEST_INTERVAL_MS 500

// ========== ENERGY CALCULATION ==========
#define ENERGY_CALC_INTERVAL_MS 1000

// ========== HEALTH MONITORING ==========
#define HEALTH_CHECK_INTERVAL_MS 10000

// ========== OCPP CONFIGURATION ==========
#define OCPP_METER_VALUE_INTERVAL_S 10
#define OCPP_HEARTBEAT_INTERVAL_S 60
#define OCPP_RECONNECT_INTERVAL_MS 5000
#define OCPP_MAX_RECONNECT_ATTEMPTS 10

// ========== FEATURE FLAGS ==========
#define ENABLE_OTA_UPDATES 1
#define ENABLE_REMOTE_LOGGING 1
#define ENABLE_DIAGNOSTICS 1
#define ENABLE_WATCHDOG 1
#define ENABLE_CRASH_RECOVERY 1
