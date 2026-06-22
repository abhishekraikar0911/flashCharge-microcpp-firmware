#pragma once

/**
 * @file hardware.h
 * @brief Hardware configuration and pin definitions
 * @author Rivot Motors
 * @date 2026
 */

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
#define CAN2_INT_PIN  34    // CONNECTED: MCP2515 INT — INPUT ONLY, no internal pull-up (MCP2515 drives it actively, external pull-up on module)
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
#define GSM_TX_PIN        17         // ESP32 TX  → Modem RXD (UART2)
#define GSM_RX_PIN        16         // ESP32 RX  ← Modem TXD (UART2)
#define GSM_RESET_PIN     27         // Modem RESET
// Baud rate strategy: boot at 115200 (factory default), then shift to 460800
// AT+IPR=460800 is sent after modem init. Both ends switch simultaneously.
// RTS/CTS hardware flow control is REQUIRED at high baud (wires soldered: GPIO14, GPIO25)
#define GSM_BOOT_BAUD     115200     // Always boot at this speed (factory default)
#define GSM_HIGH_BAUD     460800     // High-speed target after baud negotiation
#define GSM_SERIAL        Serial2    // Hardware UART2

// GSM Hardware Flow Control (RTS/CTS) — Jumper wires soldered
// Cross-connection (null-modem style):
//   ESP32 GPIO14 (RTS out) ──wire──▶ A7670 CTS pin  (ESP32 tells modem: pause/resume)
//   ESP32 GPIO25 (CTS in)  ◀──wire── A7670 RTS pin  (modem tells ESP32: I have data)
// When ESP32 is erasing flash (CPU frozen), hardware automatically pulls GPIO14 HIGH,
// the A7670 modem sees this and IMMEDIATELY stops sending bytes. Zero UART overflow.
#define GSM_RTS_PIN       14         // ESP32 GPIO14 OUTPUT → A7670 CTS input
#define GSM_CTS_PIN       25         // A7670 RTS output → ESP32 GPIO25 INPUT

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
#define GSM_WS_IDLE_TIMEOUT_MS  90000  // 90s — dead socket detection (CitrineOS responds to Heartbeat every 60s)

// ========== LED AND INTERFACE CONFIGURATION ==========
#define LED_CHARGER_STATUS     4          // D4: Charging Status LED
#define LED_NETWORK_STATUS     15         // D15: Server/Network Status LED
#define BTN_ESTOP              32         // Emergency Stop (Active LOW) - MOVED to 32
#define BTN_REBOOT             35         // System Reboot — INPUT ONLY, NO internal pull-up (needs 10K ext. pull-up to 3.3V)
#define BTN_START              33         // Local Start button (Active LOW, INPUT_PULLUP)
#define BTN_STOP               26         // Local Stop button  (Active LOW, INPUT_PULLUP)
#define LED_FAULT_STATUS       13         // D13: Fault Detection LED (Active HIGH)

// ========== SAFETY LIMITS ==========
// Note: Use ALERT_VOLTAGE_MIN_V / ALERT_VOLTAGE_MAX_V from the ALERT THRESHOLDS section below
// for all runtime voltage checks. These are kept for legacy references only.
#define MIN_VOLTAGE_V 56.0f
#define MAX_CURRENT_A 100.0f
#define MAX_TEMPERATURE_C 95.0f
#define BATTERY_CAPACITY_AH 30.0f

// ========== ALERT THRESHOLDS ==========
#define ALERT_TEMP_WARNING_C  60.0f   // H2 FIX: Graduated — throttle/warn at 60°C
#define ALERT_TEMP_CRITICAL_C 70.0f   // Emergency stop at 70°C (unchanged)
#define ALERT_VOLTAGE_MIN_V 56.0f
#define ALERT_VOLTAGE_MAX_V 92.0f  // Increased to 95V to accommodate 92V batteries without false OverVoltage faults
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
#define TASK_STACK_SIZE_HW_SVC 8192        // Doubled from 4096 — watermark was at 1424 words (too tight)
#define TASK_STACK_SIZE_OCPP 40960         // 40KB: 24KB base + ~16KB for SSLClient/mbedTLS during GSM OTA download
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
