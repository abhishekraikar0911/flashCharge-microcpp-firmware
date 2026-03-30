#ifndef LOG_MACROS_H
#define LOG_MACROS_H

#include "LogFormatter.h"

/**
 * @file log_macros.h
 * @brief Convenient macros for structured logging
 * 
 * Usage examples:
 *   LOG_INFO(SYSTEM, "WiFi connected");
 *   LOG_ERROR(CAN, "Bus-off detected");
 *   LOG_DEBUG_F(OCPP, "Transaction ID: %d", txId);
 */

// Simple logging macros
#define LOG_DEBUG(cat, msg)     LogFormatter::print(LogFormatter::cat, LogFormatter::DEBUG, msg)
#define LOG_INFO(cat, msg)      LogFormatter::print(LogFormatter::cat, LogFormatter::INFO, msg)
#define LOG_WARN(cat, msg)      LogFormatter::print(LogFormatter::cat, LogFormatter::WARN, msg)
#define LOG_ERROR(cat, msg)     LogFormatter::print(LogFormatter::cat, LogFormatter::ERROR, msg)
#define LOG_CRITICAL(cat, msg)  LogFormatter::print(LogFormatter::cat, LogFormatter::CRITICAL, msg)

// Formatted logging macros
#define LOG_DEBUG_F(cat, ...)    LogFormatter::printf(LogFormatter::cat, LogFormatter::DEBUG, __VA_ARGS__)
#define LOG_INFO_F(cat, ...)     LogFormatter::printf(LogFormatter::cat, LogFormatter::INFO, __VA_ARGS__)
#define LOG_WARN_F(cat, ...)     LogFormatter::printf(LogFormatter::cat, LogFormatter::WARN, __VA_ARGS__)
#define LOG_ERROR_F(cat, ...)    LogFormatter::printf(LogFormatter::cat, LogFormatter::ERROR, __VA_ARGS__)
#define LOG_CRITICAL_F(cat, ...) LogFormatter::printf(LogFormatter::cat, LogFormatter::CRITICAL, __VA_ARGS__)

// Section macros
#define LOG_SECTION_START(title) LogFormatter::printSectionHeader(title)
#define LOG_SECTION_END()        LogFormatter::printSectionFooter()

// Data display macros
#define LOG_DATA(label, value)   LogFormatter::printDataRow(label, value)
#define LOG_DATA_UNIT(label, value, unit) LogFormatter::printDataRow(label, value, unit)

#endif // LOG_MACROS_H
