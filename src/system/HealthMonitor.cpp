#include "system/HealthMonitor.h"
#include "system/NetworkManager.h"
#include <Arduino.h>

namespace prod
{

    void HealthMonitor::init()
    {
        if (watchdogInitialized)
            return;

        Serial.printf("[Health] 🛡️  Initializing Watchdog (%u seconds)...\n", WATCHDOG_TIMEOUT_SECONDS);

        // Initialize ESP32 Task Watchdog Timer (TWDT)
        esp_err_t err = esp_task_wdt_init(WATCHDOG_TIMEOUT_SECONDS, true); // Panic on timeout
        if (err != ESP_OK) {
            Serial.printf("[Health] ❌ Watchdog init failed: %s\n", esp_err_to_name(err));
            return;
        }

        watchdogInitialized = true;
        lastWiFiConnectTime = millis();
        lastHealthCheck = millis();
        Serial.println("[Health] ✅ Watchdog active (Panic level)");
    }
    
    void HealthMonitor::addTaskToWatchdog(TaskHandle_t task, const char* taskName)
    {
        if (!watchdogInitialized) return;

        esp_err_t err = esp_task_wdt_add(task);
        if (err == ESP_OK) {
            Serial.printf("[Health] 🔗 Task '%s' added to watchdog\n", taskName);
        } else {
            Serial.printf("[Health] ❌ Failed to add task '%s': %s\n", taskName, esp_err_to_name(err));
        }
    }

    void HealthMonitor::feed()
    {
        if (!watchdogInitialized) return;
        esp_task_wdt_reset();
    }

    void HealthMonitor::poll()
    {
        uint32_t now = millis();
        if (now - lastHealthCheck < HEALTH_CHECK_INTERVAL_MS)
        {
            return;
        }
        lastHealthCheck = now;

        // Use NetworkManager for unified connection check
        if (g_networkManager.isConnected())
        {
            lastWiFiConnectTime = now;
        }
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
        Serial.printf("[Health] 🛑 Transaction ended (duration: %u ms)\n", duration);
        transactionInProgress = false;
    }

    bool HealthMonitor::isWiFiDisconnectTimeout() const
    {
        // Suppress timeout if transaction not in progress or network connected
        if (!transactionInProgress || g_networkManager.isConnected())
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
        return false;
    }

    HealthMonitor g_healthMonitor;

} // namespace prod
