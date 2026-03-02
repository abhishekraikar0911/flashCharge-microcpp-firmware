#include "../include/wifi_manager.h"
#include <Arduino.h>

namespace prod
{

    bool WiFiManager::begin(const char *ssid, const char *password)
    {
        // For simplicity, we assume this is called once from main.cpp
        // We will populate our internal list from secrets.h directly to ensure 1>2>3 priority
        #include "../include/secrets.h"
        
        credentials[0] = {WIFI_SSID_1, WIFI_PASS_1};
        credentials[1] = {WIFI_SSID_2, WIFI_PASS_2};
        credentials[2] = {WIFI_SSID_3, WIFI_PASS_3};
        numCredentials = 3;
        isInitiated = true;

        Serial.println("[WiFi] 🛡️  Multi-WiFi Priority System Initialized (1 > 2 > 3)");
        
        // Start connection from highest priority
        currentPriorityIndex = 0;
        Serial.printf("[WiFi] Priority 1: %s\n", credentials[0].ssid);
        Serial.printf("[WiFi] Priority 2: %s\n", credentials[1].ssid);
        Serial.printf("[WiFi] Priority 3: %s\n", credentials[2].ssid);

        return attemptConnection(currentPriorityIndex);
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
            delay(500);
            Serial.print(".");
            // Feed watchdog if necessary - assuming main loop feeds it
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\n[WiFi] ✅ Connected to Priority %d: %s (IP: %s, RSSI: %d dBm)\n",
                          index + 1, ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
            currentPriorityIndex = index;
            lastReconnectAttempt = millis();
            reconnectAttempts = 0;
            return true;
        } else {
            Serial.printf("\n[WiFi] ❌ Priority %d failed\n", index + 1);
            return false;
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
