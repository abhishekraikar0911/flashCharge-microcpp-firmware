#include "system/WifiManager.h"
#include "config/secure_config.h"
#include "system/SafeString.h"
#include <Arduino.h>
#include "system/HealthMonitor.h"

namespace prod
{

    bool WiFiManager::begin(const char *ssid, const char *password)
    {
        // SECURITY FIX: Load credentials from encrypted storage instead of hardcoded values
        Serial.println("[WiFi] 🔐 Loading WiFi credentials from secure storage...");
        
        // Load up to 3 priority WiFi networks from secure storage
        numCredentials = 0;
        for (int i = 0; i < 3; i++)
        {
            char ssid[33], pass[64];
            if (SecureConfig::getWiFiCredentials(ssid, pass, sizeof(ssid), sizeof(pass), i + 1))
            {
                SafeString::copy(credentials[i].ssid, ssid, sizeof(credentials[i].ssid));
                SafeString::copy(credentials[i].password, pass, sizeof(credentials[i].password));
                Serial.printf("[WiFi] ✓ Priority %d: %s (password hidden)\n", i + 1, ssid);
                numCredentials++;
            }
            else
            {
                // If we at least found 1 network, we can proceed
                if (i > 0) {
                    Serial.printf("[WiFi] (No more credentials in storage)\n");
                    break;
                } else {
                    Serial.println("[WiFi] ❌ No primary WiFi credentials found in secure storage!");
                    return false;
                }
            }
        }
        
        isInitiated = true;
        
        Serial.println("[WiFi] ✅ Secure WiFi credentials loaded successfully");
        
        // Start connection trying highest priority first
        for (int i = 0; i < numCredentials; i++) {
            if (attemptConnection(i)) {
                return true;
            }
        }
        
        Serial.println("[WiFi] ❌ All WiFi fallback priorities failed.");
        return false;
    }

    bool WiFiManager::attemptConnection(int index) {
        if (index >= numCredentials) return false;

        const char* ssid = credentials[index].ssid;
        const char* pass = credentials[index].password;

        Serial.printf("[WiFi] Attempting Priority %d: %s \n", index + 1, ssid);
        
        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);

        uint32_t startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < CONNECT_TIMEOUT_MS)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            Serial.print(".");
            g_healthMonitor.feed();
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\n[WiFi] ✅ Connected to Priority %d: %s (IP: %s, RSSI: %d dBm)\n",
                          index + 1, ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
            currentPriorityIndex = index;
            lastReconnectAttempt = millis();
            reconnectAttempts = 0;
            
            // NTP Time Sync — REQUIRED for TLS certificate validation
            syncNTP();
            
            return true;
        } else {
            Serial.printf("\n[WiFi] ❌ Priority %d failed\n", index + 1);
            return false;
        }
    }

    void WiFiManager::syncNTP() {
        Serial.println("[NTP] 🕐 Syncing time (required for WSS/TLS)...");
        
        // IST offset = 19800s (5h30m), no DST
        configTime(19800, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
        
        // Threshold: 1735689600 = Jan 1, 2026.
        // Any time value below this means the ESP32 clock is at epoch 0 or stale
        // and MbedTLS will reject the server certificate's validity window.
        unsigned long ntpStart = millis();
        time_t now = time(nullptr);
        while (now < 1735689600L && millis() - ntpStart < 10000) {
            delay(250);
            now = time(nullptr);
        }
        
        if (now > 1735689600L) {
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S IST", &timeinfo);
            Serial.printf("[NTP] ✅ Time synced: %s\n", timeStr);
        } else {
            Serial.println("[NTP] ⚠️  Time sync failed — TLS will reject certificates!");
        }
    }

    void WiFiManager::poll()
    {
        uint32_t now = millis();

        if (WiFi.status() == WL_CONNECTED)
        {
            reconnectAttempts = 0;
            wifiFailureReported = false;

            // If we are NOT on Priority 1, periodically check if we can switch back
            if (currentPriorityIndex > 0 && (now - lastPriorityCheck > PRIORITY_SWITCH_CHECK_INTERVAL)) {
                Serial.println("[WiFi] 🔃 Periodically checking for higher priority network...");
                lastPriorityCheck = now;
                
                // Scan to see if P1 or P2 is visible before dropping current connection
                int n = WiFi.scanNetworks();
                for (int i = 0; i < currentPriorityIndex; i++) {
                    for (int j = 0; j < n; j++) {
                        if (WiFi.SSID(j) == credentials[i].ssid) {
                            Serial.printf("[WiFi] 🌟 Priority %d (%s) is visible. Switching back...\n", i+1, credentials[i].ssid);
                            attemptConnection(i);
                            return;
                        }
                    }
                }
                Serial.println("[WiFi] No higher priority network found in scan.");
            }
            return;
        }

        // WiFi disconnected - attempt reconnection
        if (now - lastReconnectAttempt < RECONNECT_CHECK_INTERVAL)
        {
            return;
        }

        if (!wifiFailureReported)
        {
            Serial.println("[WiFi] ⚠️  Connection lost");
            wifiFailureReported = true;
        }

        reconnectAttempts++;
        lastReconnectAttempt = now;

        // Try to reconnect to CURRENT priority first
        Serial.printf("[WiFi] 🔄 Reconnecting to Priority %d (%s) [Attempt %u]...\n", 
                      currentPriorityIndex + 1, credentials[currentPriorityIndex].ssid, reconnectAttempts);
        
        WiFi.reconnect();
        
        // If reconnect fails after few attempts, cycle through ALL priorities
        if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
            Serial.println("[WiFi] Reconnection failed. Cycling through all priorities...");
            reconnectAttempts = 0;
            
            for (int i = 0; i < numCredentials; i++) {
                if (attemptConnection(i)) {
                    return; 
                }
            }
            Serial.println("[WiFi] ❌ All WiFi networks failed. Retrying in cycle...");
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
