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

// CAN2 - MCP2515 (SPI) - Vehicle BMS (VSPI Pins)
#define CAN2_CS_PIN   5     // Changed to 5
#define CAN2_SCK_PIN  18    // Changed to 18
#define CAN2_MOSI_PIN 23    // Changed to 23 (SI)
#define CAN2_MISO_PIN 19    // Changed to 19 (SO)
#define CAN2_INT_PIN  34    // CONNECTED: Using Interrupt Mode
#define CAN2_BAUDRATE 250000
#define MCP2515_CRYSTAL_8MHZ 

#define CAN_RX_QUEUE_SIZE 64
#define CAN_TX_QUEUE_SIZE 16

// ========== CAN DECODER SCALE FACTORS (M2 FIX) ==========
// Charger Module (CAN1 — ISO1050 TWAI)
#define CHARGER_VMAX_SCALE      1024.0f    // raw_u32 / 1024 → Volts
#define CHARGER_IMAX_SCALE      30.5f      // raw_u32 / 30.5  → Amps
#define CHARGER_CURR_U16_SCALE  1024.0f    // raw_u16 / 1024 → Amps
#define CHARGER_TEMP_SCALE      0.001f     // raw_u16 × 0.001 → °C
#define CHARGER_VOLT_FB_SCALE   10.0f      // feedback: V × 10 → uint16 code
#define CHARGER_CURR_FB_SCALE   10.0f      // feedback: A × 10 → uint16 code

// ========== GSM MODEM CONFIGURATION (SIM A7670C) ==========
#define GSM_TX_PIN        17         // ESP32 TX → Modem RXD (UART2)
#define GSM_RX_PIN        16         // ESP32 RX ← Modem TXD (UART2)
#define GSM_RESET_PIN     27         // Modem RESET — moved from 23 (GPIO 23 now = MCP2515 MOSI)
#define GSM_BAUD_RATE     115200
#define GSM_SERIAL        Serial2    // Hardware UART2

// GSM APN Configuration: loaded at runtime from secure NVS via SecureConfig::getGSMCredentials()
// (No hardcoded APN to avoid leaking credentials in source control)

// GSM Timing Configuration
#define GSM_RESET_PULSE_MS      2500   // Active-HIGH reset pulse duration
#define GSM_AT_TIMEOUT_MS       30000  // AT command watchdog timeout
#define GSM_CONNECT_TIMEOUT_MS  60000  // Network registration timeout (Idle)
#define GSM_MAX_RETRIES         3      // Retries before WiFi fallback (Idle)

// Industrial Charging Fallback (Fast-failover)
#define GSM_CHARGING_CONNECT_TIMEOUT_MS 20000 // 20s registration wait during Tx
#define GSM_CHARGING_MAX_RETRIES        1     // Only 1 retry before WiFi during Tx
#define COMM_LOSS_TIMEOUT_MS            120000 // 120s Comm Loss -> Emergency Stop (was 30s)

#define GSM_RECHECK_INTERVAL_MS 300000 // Retry GSM every 5 min while on WiFi (was 10min)
#define GSM_CIPSTATUS_INTERVAL  30000  // AT+CIPSTATUS check interval
#define GSM_WS_IDLE_TIMEOUT_MS  60000  // FIX E: Reduced from 90s to 60s — detect dead socket faster

// ========== LED AND INTERFACE CONFIGURATION ==========
#define LED_CHARGER_STATUS     13         // D13: Green/Yellow Charger Status
#define LED_NETWORK_STATUS     15         // D15: Blue/White Network Status
#define BTN_ESTOP              32         // Emergency Stop (Active LOW) - MOVED to 32
#define BTN_REBOOT             35         // System Reboot (Active LOW)

// ========== SAFETY LIMITS ==========
#define MIN_VOLTAGE_V 56.0f
#define MAX_VOLTAGE_V 86.0f  // Increased from 95V to accommodate transients
#define MAX_CURRENT_A 100.0f
#define MAX_TEMPERATURE_C 95.0f
#define BATTERY_CAPACITY_AH 30.0f

// ========== ALERT THRESHOLDS ==========
#define ALERT_TEMP_WARNING_C  60.0f   // H2 FIX: Graduated — throttle/warn at 60°C
#define ALERT_TEMP_CRITICAL_C 70.0f   // Emergency stop at 70°C (unchanged)
#define ALERT_VOLTAGE_MIN_V 56.0f
#define ALERT_VOLTAGE_MAX_V 95.0f  // Changed to 95V to accommodate 92V batteries without false OverVoltage faults
#define ALERT_CURRENT_MAX_A 100.0f

// ========== FAULT STABILIZATION ==========
#define FAULT_STABILIZATION_PERIOD_MS 10000  // 10 seconds after fault before allowing new transaction

// ========== PLUG DETECTION (HYBRID) ==========
#define PLUG_DISCONNECT_CURRENT_THRESHOLD 0.5f  // Amps
#define PLUG_DISCONNECT_CURRENT_TIMEOUT 5000    // ms
#define PLUG_DISCONNECT_BMS_TIMEOUT 3000        // ms
#define PLUG_DISCONNECT_VOLTAGE_RATE 2.0f       // V/s

// ========== WATCHDOG CONFIGURATION ==========
#define WATCHDOG_TIMEOUT_S 60

// ========== TASK STACK SIZES ==========
#define TASK_STACK_SIZE_CAN_RX 6144       // Increased from 4096 — proven stable
#define TASK_STACK_SIZE_CHARGER_COMM 6144  // Increased from 4096 — prevents stack overflow
#define TASK_STACK_SIZE_UI 4096
#define TASK_STACK_SIZE_OCPP 24576         // Increased to 24KB - MbedTLS handshake + MicroOcpp can be heavy
#define TASK_STACK_SIZE_WATCHDOG 2048
#define TASK_STACK_SIZE_NETWORK 8192       // GSM/WiFi state machine + TLS

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
