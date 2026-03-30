#pragma once

/**
 * @file network_manager.h
 * @brief Network connection manager with GSM primary / WiFi fallback
 * 
 * Coordinates between GSMManager and WiFiManager:
 *   1. Try GSM first (primary)
 *   2. Fall back to WiFi if GSM fails after N retries
 *   3. Periodically recheck GSM while on WiFi
 * 
 * Provides a unified isConnected() interface for OCPP.
 * 
 * @author Rivot Motors
 * @date 2026
 */

#include <Arduino.h>
#include "GsmManager.h"

namespace prod {

    /**
     * @brief Active connection type
     */
    enum class ConnectionType : uint8_t {
        NONE = 0,
        GSM,
        WIFI
    };

    /**
     * @brief Network Manager state
     */
    enum class NetworkState : uint8_t {
        IDLE = 0,
        GSM_CONNECTING,
        GSM_CONNECTED,
        GSM_FAILED,
        WIFI_CONNECTING,
        WIFI_CONNECTED,
        GSM_RECHECK         // On WiFi, periodically trying GSM
    };

    const char* networkStateToString(NetworkState state);
    const char* connectionTypeToString(ConnectionType type);

    /**
     * @brief Unified network manager for GSM + WiFi failover
     */
    class NetworkManager {
    public:
        /**
         * @brief Initialize both GSM and WiFi subsystems.
         *        Does NOT start connection — call poll() to drive the state machine.
         */
        void init();

        /**
         * @brief Drive the connection state machine.
         *        Call this regularly from the network task.
         */
        void poll();

        /**
         * @brief Force a reconnection attempt (GSM first).
         */
        void reconnect();

        // ── Unified Status ──

        bool isConnected() const;
        ConnectionType getActiveConnection() const { return _activeConnection; }
        NetworkState getState() const { return _state; }

        /**
         * @brief Notify the manager of network activity (sent/received data).
         *        Used by the WebSocket watchdog to detect silent drops.
         */
        void notifyActivity();

        /**
         * @brief Check if NTP time has been synced
         */
        bool isTimeSynced() const { return _timeSynced; }

        /**
         * @brief Get diagnostic string for logging
         */
        void printStatus();

    private:
        // ── Connection Attempts ──
        GsmError attemptGSM();
        bool attemptWiFi();

        // ── GSM Recheck ──
        void checkGSMRevert();

        // ── NTP Sync ──
        void syncNTP();

        // ── State ──
        NetworkState _state = NetworkState::IDLE;
        ConnectionType _activeConnection = ConnectionType::NONE;
        bool _initialized = false;
        bool _timeSynced = false;

        // ── Retry Tracking ──
        uint8_t _gsmRetryCount = 0;
        uint32_t _lastGSMRecheckTime = 0;
        uint32_t _lastStatusLog = 0;
        uint32_t _lastActivityTime = 0;

        // ── M3: WiFi exponential backoff ──
        uint32_t _wifiBackoffMs   = 2000;   // Starts at 2s, doubles up to 60s cap
        uint32_t _wifiNextAttempt = 0;      // millis() timestamp for next allowed attempt
    };

    extern NetworkManager g_networkManager;

} // namespace prod
