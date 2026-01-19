#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include <Arduino.h>
#include <esp_task_wdt.h>

/**
 * @file health_monitor.h
 * @brief System health monitoring with watchdog and auto-recovery
 *
 * Monitors:
 * - OCPP loop execution (Task Watchdog Timer)
 * - WiFi connection stability (5-minute timeout)
 * - Hardware fault conditions
 * - Graceful degradation on failures
 */

namespace prod
{

    class HealthMonitor
    {
    private:
        static const uint32_t WATCHDOG_TIMEOUT_SECONDS = 10;
        static const uint32_t WIFI_DISCONNECT_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes
        static const uint32_t HEALTH_CHECK_INTERVAL_MS = 10000;           // 10 seconds

        uint32_t lastWiFiConnectTime = 0;
        uint32_t lastHealthCheck = 0;
        bool watchdogInitialized = false;
        bool transactionInProgress = false;
        uint32_t transactionStartTime = 0;

    public:
        /**
         * Initialize watchdog timer
         * Called once in setup()
         */
        void init();

        /**
         * Feed the watchdog (must be called regularly from OCPP loop)
         */
        void feed();

        /**
         * Poll health status
         * Checks WiFi timeout, handles transaction aborts, etc.
         */
        void poll();

        /**
         * Mark transaction start time
         */
        void onTransactionStarted();

        /**
         * Mark transaction end
         */
        void onTransactionEnded();

        /**
         * Check if WiFi has been disconnected too long
         */
        bool isWiFiDisconnectTimeout() const;

        /**
         * Get system uptime in seconds
         */
        uint32_t getUptimeSeconds() const;

        /**
         * Get transaction duration in seconds (if active)
         */
        uint32_t getTransactionDurationSeconds() const;

        /**
         * Check for hardware faults (temperature, overcurrent, etc.)
         */
        bool checkHardwareFault();
    };

    extern HealthMonitor g_healthMonitor;

} // namespace prod

#endif // HEALTH_MONITOR_H
