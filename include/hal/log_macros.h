/**
 * @file log_macros.h
 * @brief Convenience LOG_ macros that wrap ILogger via AppContext
 *
 * Include this SEPARATELY in any application/driver file that wants LOG_D/I/W/E.
 * Do NOT include in HAL interface headers to avoid circular dependencies.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "hal/interfaces/ILogger.h"
#include "app/AppContext.h"

#define LOG_D(tag, ...) if(g_app.logger) g_app.logger->logf(ILogger::Level::DEBUG, tag, __VA_ARGS__)
#define LOG_I(tag, ...) if(g_app.logger) g_app.logger->logf(ILogger::Level::INFO,  tag, __VA_ARGS__)
#define LOG_W(tag, ...) if(g_app.logger) g_app.logger->logf(ILogger::Level::WARN,  tag, __VA_ARGS__)
#define LOG_E(tag, ...) if(g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, tag, __VA_ARGS__)
