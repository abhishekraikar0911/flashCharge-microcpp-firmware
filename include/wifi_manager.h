#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

/**
 * @file wifi_manager.h
 * @brief WiFi connection manager with auto-reconnect capability
 *
 * Automatically reconnects to WiFi if connection is lost.
 * Implements exponential backoff and health monitoring.
 */

namespace prod
{

    class WiFiManager
    {
    private:
        static const uint32_t CONNECT_TIMEOUT_MS = 20000;
        static const uint32_t RECONNECT_CHECK_INTERVAL = 5000;
        static const uint32_t MAX_RECONNECT_ATTEMPTS = 5;
        static const uint32_t RECONNECT_BACKOFF_MS = 5000;

        uint32_t lastReconnectAttempt = 0;
        uint32_t reconnectAttempts = 0;
        bool wifiFailureReported = false;

    public:
        /**
         * Initialize WiFi connection
         */
        bool begin(const char *ssid, const char *password);

        /**
         * Poll WiFi status and attempt reconnection if needed
         * Call this regularly from main loop
         */
        void poll();

        /**
         * Check if currently connected
         */
        bool isConnected() const;

        /**
         * Force reconnection attempt
         */
        void reconnect();

        /**
         * Get connection status string
         */
        const char *getStatusString() const;

        /**
         * Get signal strength in dBm
         */
        int32_t getSignalStrength() const;

        /**
         * Get reconnection attempt count
         */
        uint32_t getAttemptCount() const { return reconnectAttempts; }
    };

    extern WiFiManager g_wifiManager;

} // namespace prod

#endif // WIFI_MANAGER_H
