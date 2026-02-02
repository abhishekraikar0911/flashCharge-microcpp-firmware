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
#define MAX_VOLTAGE_V 85.5f
#define MAX_CURRENT_A 300.0f
#define MAX_TEMPERATURE_C 70.0f
#define BATTERY_CAPACITY_AH 30.0f

// ========== PLUG DETECTION (HYBRID) ==========
#define PLUG_DISCONNECT_CURRENT_THRESHOLD 0.5f  // Amps
#define PLUG_DISCONNECT_CURRENT_TIMEOUT 5000    // ms
#define PLUG_DISCONNECT_BMS_TIMEOUT 3000        // ms
#define PLUG_DISCONNECT_VOLTAGE_RATE 2.0f       // V/s

// ========== WATCHDOG CONFIGURATION ==========
#define WATCHDOG_TIMEOUT_S 30

// ========== TASK STACK SIZES ==========
#define TASK_STACK_SIZE_CAN_RX 4096
#define TASK_STACK_SIZE_CHARGER_COMM 4096
#define TASK_STACK_SIZE_UI 4096
#define TASK_STACK_SIZE_OCPP 8192
#define TASK_STACK_SIZE_WATCHDOG 2048

// ========== TASK PRIORITIES ==========
#define TASK_PRIORITY_WATCHDOG 6
#define TASK_PRIORITY_CAN_RX 5
#define TASK_PRIORITY_CHARGER_COMM 4
#define TASK_PRIORITY_OCPP 3
#define TASK_PRIORITY_UI 2
