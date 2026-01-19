#pragma once

/**
 * @file version.h
 * @brief Firmware version information and build metadata
 * @author Rivot Motors
 * @date 2026
 */

#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 0
#define FIRMWARE_VERSION_PATCH 0

#define CHARGER_MODEL "RIVOT_100A"
#define MANUFACTURER "Rivot Motors"
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// Build environment
#define BUILD_ENVIRONMENT "production"
#define BUILD_TIMESTAMP FIRMWARE_BUILD_DATE " " FIRMWARE_BUILD_TIME
