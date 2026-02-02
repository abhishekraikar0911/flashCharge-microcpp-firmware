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

        watchdogInitialized = true;
        lastWiFiConnectTime = millis();
        lastHealthCheck = millis();
        Serial.println("[Health] ⚠️  Watchdog disabled (causing boot loops)");
    }
    
    void HealthMonitor::addTaskToWatchdog(TaskHandle_t task, const char* taskName)
    {
        Serial.printf("[Health] ⚠️  Watchdog disabled - %s not registered\n", taskName);
    }

    void HealthMonitor::feed()
    {
        // Watchdog disabled
    }

    void HealthMonitor::poll()
    {
        uint32_t now = millis();
        if (now - lastHealthCheck < HEALTH_CHECK_INTERVAL_MS)
        {
            return;
        }
        lastHealthCheck = now;

        if (g_wifiManager.isConnected())
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
        return false;
    }

    HealthMonitor g_healthMonitor;

} // namespace prod
