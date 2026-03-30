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

/**
 * @brief Initialize all hardware peripherals and populate g_app
 * Must be called before any application logic.
 * @return true if all HAL and drivers initialized successfully.
 */
bool BSP_Init();
