#pragma once

/**
 * @file hardware.h
 * @brief Hardware configuration and pin definitions
 * @author Rivot Motors
 * @date 2026
 */

#include <Arduino.h>

// ========== CAN BUS CONFIGURATION ==========
// CAN1 - ISO1050 (TWAI) - Charger Module
#define CAN1_TX_PIN GPIO_NUM_21
#define CAN1_RX_PIN GPIO_NUM_22
#define CAN1_BAUDRATE 250000

// CAN2 - MCP2515 (SPI) - Vehicle BMS
#define CAN2_CS_PIN 5
#define CAN2_INT_PIN 4
#define CAN2_SCK_PIN 18
#define CAN2_MISO_PIN 19
#define CAN2_MOSI_PIN 23
#define CAN2_BAUDRATE 250000
#define MCP2515_CRYSTAL_8MHZ  // CRITICAL: 8MHz crystal on MCP2515 module

#define CAN_RX_QUEUE_SIZE 64
#define CAN_TX_QUEUE_SIZE 16

// ========== SAFETY LIMITS ==========
#define MIN_VOLTAGE_V 56.0f
#define MAX_VOLTAGE_V 102.0f  // Increased from 95V to accommodate transients
#define MAX_CURRENT_A 110.0f
#define MAX_TEMPERATURE_C 95.0f
#define BATTERY_CAPACITY_AH 30.0f

// ========== ALERT THRESHOLDS ==========
#define ALERT_TEMP_WARNING_C 70.0f
#define ALERT_TEMP_CRITICAL_C 70.0f
#define ALERT_VOLTAGE_MIN_V 56.0f
#define ALERT_VOLTAGE_MAX_V 102.0f  // Increased from 95V to accommodate transients
#define ALERT_CURRENT_MAX_A 100.0f

// ========== FAULT STABILIZATION ==========
#define FAULT_STABILIZATION_PERIOD_MS 10000  // 10 seconds after fault before allowing new transaction

// ========== PLUG DETECTION (HYBRID) ==========
#define PLUG_DISCONNECT_CURRENT_THRESHOLD 0.5f  // Amps
#define PLUG_DISCONNECT_CURRENT_TIMEOUT 5000    // ms
#define PLUG_DISCONNECT_BMS_TIMEOUT 3000        // ms
#define PLUG_DISCONNECT_VOLTAGE_RATE 2.0f       // V/s

// ========== WATCHDOG CONFIGURATION ==========
#define WATCHDOG_TIMEOUT_S 30

// ========== TASK STACK SIZES ==========
#define TASK_STACK_SIZE_CAN_RX 6144       // Increased from 4096 — proven stable
#define TASK_STACK_SIZE_CHARGER_COMM 6144  // Increased from 4096 — prevents stack overflow
#define TASK_STACK_SIZE_UI 4096
#define TASK_STACK_SIZE_OCPP 16384         // Increased from 10240 — WSS/TLS (MbedTLS) handshake needs 16KB
#define TASK_STACK_SIZE_WATCHDOG 2048

// ========== TASK PRIORITIES ==========
#define TASK_PRIORITY_WATCHDOG 9
#define TASK_PRIORITY_CAN_RX 8            // Safety-critical: highest after watchdog
#define TASK_PRIORITY_CHARGER_COMM 7      // Safety-critical: charger hardware control
#define TASK_PRIORITY_OCPP 3
#define TASK_PRIORITY_UI 2

// ========== TEST/DEBUG MODE CONFIGURATION ==========
/**
 * @brief Test Mode Bypass for Development
 * 
 * When ENABLE_TEST_MODE is set to 1, the firmware will bypass hardware
 * safety checks for RemoteStart/RemoteStop commands. This allows testing
 * OCPP communication with CitrineOS without requiring all hardware to be
 * connected and operational.
 * 
 * ⚠️  WARNING: This MUST be set to 0 for production deployment!
 * 
 * Bypassed checks when enabled:
 * - Gun physical connection requirement
 * - BMS safety flag validation
 * - Terminal voltage range checks
 * - Charger module health checks
 * - Fault stabilization lock
 * 
 * Usage:
 * - Development/Testing: Set to 1
 * - Production: Set to 0 (default)
 */
#ifndef ENABLE_TEST_MODE
#define ENABLE_TEST_MODE 0  // ✅ PRODUCTION MODE — all safety checks enforced
#endif

#if ENABLE_TEST_MODE
#warning "⚠️  TEST MODE ENABLED - Hardware safety checks will be bypassed! DO NOT USE IN PRODUCTION!"
#endif
