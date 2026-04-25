/**
 * @file bsp_init.h
 * @brief BSP initialization entry point for ESP32 Rev1.1
 * @layer BSP
 *
 * Called once from main.cpp at boot before any application logic runs.
 * Statically allocates all HAL objects and device drivers,
 * then populates the global AppContext.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Initialize all hardware peripherals and populate g_app
 * Must be called before any application logic.
 * @return true if all HAL and drivers initialized successfully.
 */
bool BSP_Init();

/**
 * @brief Drain MCP2515 (CAN2) hardware RX buffers into its internal software queue.
 *
 * Called from the CAN2_RX FreeRTOS task — either on ISR notification (INT mode)
 * or every 2ms via polling fallback.
 * Thread-safe: internally protected by the MCP2515 SPI mutex.
 */
void BSP_DrainCAN2();

/**
 * @brief Register the CAN2_RX task handle so the GPIO 34 ISR can wake it.
 *
 * MUST be called after xTaskCreate() returns the task handle.
 * Once registered, the INT ISR fires vTaskNotifyGiveFromISR() instead of
 * the task sleeping on vTaskDelay(2ms) — latency drops from ≤2ms to <10µs.
 */
void BSP_SetCAN2RxTask(TaskHandle_t handle);
