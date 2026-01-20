#pragma once

/**
 * @file version.h
 * @brief Firmware version information and build metadata
 * @author Rivot Motors
 * @date 2025
 */

#define FIRMWARE_VERSION "2.4.0"
#define FIRMWARE_VERSION_MAJOR 2
#define FIRMWARE_VERSION_MINOR 4
#define FIRMWARE_VERSION_PATCH 0

#define CHARGER_MODEL "RIVOT_100A"
#define MANUFACTURER "Rivot Motors"
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// Build environment
#define BUILD_ENVIRONMENT "production"
#define BUILD_TIMESTAMP FIRMWARE_BUILD_DATE " " FIRMWARE_BUILD_TIME

// Feature flags
#define FEATURE_OCPP_1_6 1
#define FEATURE_CAN_BUS 1
#define FEATURE_TERMINAL_VALUES 1
#define FEATURE_ENERGY_METERING 1
