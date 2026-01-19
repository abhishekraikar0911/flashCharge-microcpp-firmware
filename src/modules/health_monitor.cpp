#include "../include/health_monitor.h"
#include "../include/wifi_manager.h"
#include "../include/header.h"
#include <Arduino.h>

namespace prod
{

    void HealthMonitor::init()
    {
        if (watchdogInitialized)
            return;

        // Initialize Task Watchdog Timer
        esp_task_wdt_init(WATCHDOG_TIMEOUT_SECONDS, true); // Panic on timeout
        esp_task_wdt_add(xTaskGetCurrentTaskHandle());

        watchdogInitialized = true;
        lastWiFiConnectTime = millis();
        lastHealthCheck = millis();
        Serial.printf("[Health] ✅ Watchdog initialized (%d second timeout)\n", WATCHDOG_TIMEOUT_SECONDS);
    }

    void HealthMonitor::feed()
    {
        if (!watchdogInitialized)
            return;
        esp_task_wdt_reset(); // Feed the watchdog
    }

    void HealthMonitor::poll()
    {
        uint32_t now = millis();
        if (now - lastHealthCheck < HEALTH_CHECK_INTERVAL_MS)
        {
            return;
        }
        lastHealthCheck = now;

        // Check WiFi timeout
        if (g_wifiManager.isConnected())
        {
            lastWiFiConnectTime = now;
        }
        else if (transactionInProgress)
        {
            uint32_t disconnectDuration = now - lastWiFiConnectTime;
            if (disconnectDuration > WIFI_DISCONNECT_TIMEOUT_MS)
            {
                Serial.printf("[Health] ⚠️  WiFi disconnected for %ld seconds, aborting transaction\n",
                              disconnectDuration / 1000);
                // Signal transaction abort (implementation depends on OCPP setup)
                onTransactionEnded();
            }
        }

        // Check for hardware faults
        if (checkHardwareFault())
        {
            Serial.println("[Health] ⚠️  Hardware fault detected!");
        }

        // Periodic status
        Serial.printf("[Health] Uptime: %ld sec, WiFi: %s, TX Active: %s\n",
                      getUptimeSeconds(),
                      g_wifiManager.isConnected() ? "✅" : "❌",
                      transactionInProgress ? "Yes" : "No");
    }

    void HealthMonitor::onTransactionStarted()
    {
        transactionInProgress = true;
        transactionStartTime = millis();
        Serial.println("[Health] 🚗 Transaction started");
    }

    void HealthMonitor::onTransactionEnded()
    {
        if (!transactionInProgress)
            return;
        uint32_t duration = millis() - transactionStartTime;
        Serial.printf("[Health] 🛑 Transaction ended (duration: %lu ms)\n", duration);
        transactionInProgress = false;
    }

    bool HealthMonitor::isWiFiDisconnectTimeout() const
    {
        if (!transactionInProgress || g_wifiManager.isConnected())
        {
            return false;
        }
        return (millis() - lastWiFiConnectTime) > WIFI_DISCONNECT_TIMEOUT_MS;
    }

    uint32_t HealthMonitor::getUptimeSeconds() const
    {
        return millis() / 1000;
    }

    uint32_t HealthMonitor::getTransactionDurationSeconds() const
    {
        if (!transactionInProgress)
            return 0;
        return (millis() - transactionStartTime) / 1000;
    }

    bool HealthMonitor::checkHardwareFault()
    {
        // Check temperature (if available)
        // Check overcurrent (if available)
        // Check undervoltage (if available)
        // Return true if any fault detected

        // Example: Check charger temperature from existing globals
        // extern float chargerTemp;
        // if (chargerTemp > 80.0f) return true; // Overheat

        return false; // No fault for now
    }

    HealthMonitor g_healthMonitor;

} // namespace prod
