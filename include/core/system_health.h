#pragma once

/**
 * @file system_health.h
 * @brief System health monitoring and diagnostics
 * @author Rivot Motors
 * @date 2026
 */

#include <stdint.h>
#include <stdbool.h>

// ========== HEALTH STATUS CODES ==========

typedef enum
{
    HEALTH_OK = 0,
    HEALTH_WARNING = 1,
    HEALTH_CRITICAL = 2,
    HEALTH_FAULT = 3
} HealthStatus;

// ========== HEALTH METRICS ==========

struct SystemHealth
{
    HealthStatus overall_status;
    uint32_t uptime_seconds;
    uint8_t heap_usage_percent;
    uint32_t free_heap_bytes;
    uint8_t core_temp_c;
    bool watchdog_active;
    uint32_t total_errors;
    uint32_t total_warnings;
    uint32_t last_check_ms;
};

// ========== SYSTEM HEALTH INTERFACE ==========

namespace SystemHealth
{

    /**
     * @brief Initialize health monitoring
     */
    void init();

    /**
     * @brief Perform system health check
     * @return Health status structure
     */
    SystemHealth check();

    /**
     * @brief Get overall system health
     * @return Health status code
     */
    HealthStatus getStatus();

    /**
     * @brief Log health event
     * @param status Status code
     * @param message Event message
     */
    void logEvent(HealthStatus status, const char *message);

    /**
     * @brief Reset error counters
     */
    void resetCounters();

    /**
     * @brief Enable/disable health checks
     * @param enable true to enable, false to disable
     */
    void setEnabled(bool enable);

} // namespace SystemHealth
