#pragma once

/**
 * @file logger.h
 * @brief Structured logging system for production diagnostics
 * @author Rivot Motors
 * @date 2026
 */

#include <stdint.h>
#include <cstdarg>

// ========== LOG LEVELS ==========

typedef enum
{
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_WARN = 3,
    LOG_LEVEL_ERROR = 4,
    LOG_LEVEL_CRITICAL = 5,
    LOG_LEVEL_SILENT = 6
} LogLevel;

// ========== LOG TAGS (MODULES) ==========

#define LOG_TAG_CAN "CAN"
#define LOG_TAG_BMS "BMS"
#define LOG_TAG_CHG "CHG"
#define LOG_TAG_OCPP "OCPP"
#define LOG_TAG_SYS "SYS"
#define LOG_TAG_NET "NET"
#define LOG_TAG_UI "UI"
#define LOG_TAG_OTA "OTA"

// ========== LOGGER INTERFACE ==========

namespace Logger
{

    /**
     * @brief Initialize logger
     * @param initial_level Minimum log level to output
     */
    void init(LogLevel initial_level = LOG_LEVEL_INFO);

    /**
     * @brief Set minimum log level
     * @param level Minimum level to output
     */
    void setLevel(LogLevel level);

    /**
     * @brief Log message with formatting
     * @param tag Module tag
     * @param level Log level
     * @param format Printf-style format string
     * @param ... Variable arguments
     */
    void log(const char *tag, LogLevel level, const char *format, ...) __attribute__((format(printf, 3, 4)));

// Convenience macros
#define LOG_TRACE(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_CRITICAL, fmt, ##__VA_ARGS__)

    /**
     * @brief Get total messages logged
     * @return Message count
     */
    uint32_t getMessageCount();

    /**
     * @brief Flush log buffer
     */
    void flush();

    /**
     * @brief Enable/disable logging
     * @param enable true to enable, false to disable
     */
    void setEnabled(bool enable);

} // namespace Logger
