#include "../include/wifi_manager.h"
#include <Arduino.h>

namespace prod
{

    bool WiFiManager::begin(const char *ssid, const char *password)
    {
        Serial.printf("[WiFi] Connecting to %s...\n", ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);

        uint32_t startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < CONNECT_TIMEOUT_MS)
        {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("\n[WiFi] ❌ Initial connection failed");
            return false;
        }

        Serial.printf("\n[WiFi] ✅ Connected: %s (IP: %s, RSSI: %d dBm)\n",
                      ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
        lastReconnectAttempt = millis();
        reconnectAttempts = 0;
        return true;
    }

    void WiFiManager::poll()
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            reconnectAttempts = 0;
            wifiFailureReported = false;
            return; // Still connected
        }

        // WiFi disconnected - attempt reconnection
        uint32_t now = millis();
        if (now - lastReconnectAttempt < RECONNECT_CHECK_INTERVAL)
        {
            return; // Not yet time to retry
        }

        if (!wifiFailureReported)
        {
            Serial.printf("[WiFi] ⚠️  Connection lost (RSSI was %d dBm)\n", WiFi.RSSI());
            wifiFailureReported = true;
        }

        if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS)
        {
            Serial.printf("[WiFi] ❌ Max reconnection attempts reached (%u)\n", reconnectAttempts);
            // Wait longer before next attempt cycle
            if (now - lastReconnectAttempt > 60000)
            {
                reconnectAttempts = 0;
            }
            return;
        }

        uint32_t backoffMs = RECONNECT_BACKOFF_MS * (1 << reconnectAttempts); // Exponential backoff
        if (now - lastReconnectAttempt >= backoffMs)
        {
            reconnectAttempts++;
            Serial.printf("[WiFi] 🔄 Reconnection attempt %u...\n", reconnectAttempts);
            WiFi.reconnect();
            lastReconnectAttempt = now;
        }
    }

    bool WiFiManager::isConnected() const
    {
        return WiFi.status() == WL_CONNECTED;
    }

    void WiFiManager::reconnect()
    {
        Serial.println("[WiFi] 🔄 Manual reconnection initiated");
        reconnectAttempts = 0;
        lastReconnectAttempt = millis() - RECONNECT_CHECK_INTERVAL;
    }

    const char *WiFiManager::getStatusString() const
    {
        switch (WiFi.status())
        {
        case WL_CONNECTED:
            return "Connected";
        case WL_IDLE_STATUS:
            return "Idle";
        case WL_NO_SSID_AVAIL:
            return "SSID not found";
        case WL_SCAN_COMPLETED:
            return "Scan completed";
        case WL_CONNECT_FAILED:
            return "Connection failed";
        case WL_CONNECTION_LOST:
            return "Connection lost";
        case WL_DISCONNECTED:
            return "Disconnected";
        case WL_NO_SHIELD:
            return "No WiFi shield";
        default:
            return "Unknown";
        }
    }

    int32_t WiFiManager::getSignalStrength() const
    {
        if (!isConnected())
            return 0;
        return WiFi.RSSI();
    }

    WiFiManager g_wifiManager;

} // namespace prod
