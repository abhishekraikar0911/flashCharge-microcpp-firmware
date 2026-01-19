#pragma once

/**
 * @file hardware.h
 * @brief Hardware configuration and pin definitions
 * @author Rivot Motors
 * @date 2026
 */

#include <Arduino.h>

// ========== CAN BUS CONFIGURATION ==========
#define CAN_TX_PIN GPIO_NUM_21
#define CAN_RX_PIN GPIO_NUM_22
#define CAN_BAUDRATE 250000
#define CAN_RX_QUEUE_SIZE 64
#define CAN_TX_QUEUE_SIZE 16

// ========== SAFETY LIMITS ==========
#define MIN_VOLTAGE_V 56.0f
#define MAX_VOLTAGE_V 85.5f
#define MAX_CURRENT_A 300.0f
#define MAX_TEMPERATURE_C 70.0f
#define BATTERY_CAPACITY_AH 30.0f

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
