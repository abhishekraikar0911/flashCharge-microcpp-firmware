#pragma once

/**
 * @file ocpp_connection_helper.h
 * @brief Unified MicroOcpp Connection wrapper for GSM and WiFi
 * 
 * Provides a single Connection interface for MicroOcpp that:
 *  1. Switches transport between WiFi (WebSocketsClient) and GSM (TinyGsm).
 *  2. Implements the 120s WebSocket Idle Watchdog.
 *  3. Handles GSM SSL via modem's internal stack.
 * 
 * @author Rivot Motors
 * @date 2026
 */

#include <MicroOcpp/Core/Connection.h>
#include <WebSocketsClient.h>
#include "services/network/GsmManager.h"
#include "services/network/NetworkManager.h"
#include <SSLClient.h>

namespace prod {

    /**
     * @brief A MicroOcpp Connection implementation that wraps both WiFi and GSM
     */
    class UnifiedConnection : public MicroOcpp::Connection {
    public:
        UnifiedConnection();
        virtual ~UnifiedConnection();

        // ── Dynamic Server Configuration ──
        void setServer(const char* host, uint16_t port, const char* chargerId);

        // ── MicroOcpp::Connection Interface ──
        void loop() override;
        bool sendTXT(const char *msg, size_t length) override;
        void setReceiveTXTcallback(MicroOcpp::ReceiveTXTcallback &callback) override;
        unsigned long getLastRecv() override;
        unsigned long getLastConnected() override;
        bool isConnected() override;

        /**
         * @brief Explicitly stop and delete the GSM WebSocket SSLClient.
         * Called by GsmHttpDownload before OTA takes over the modem slot.
         * Without this, the WebSocket SSLClient's live mbedTLS state shares
         * TinyGsmClient with the OTA SSLClient, causing MAC verification failures.
         */
        void teardownGsmWebSocket();

    private:
        // ── WiFi Transport (Links2004) ──
        WebSocketsClient* _wifiWS = nullptr;
        std::unique_ptr<MicroOcpp::Connection> _wifiConn;

        // ── GSM Transport (Manual WebSocket) ──
        bool sendGsmTXT(const char *msg, size_t length);
        bool sendGsmFrame(uint8_t opcode, const uint8_t *payload, size_t length);
        void loopGSM();
        bool gsmHandshake();

        // ── Shared State ──
        MicroOcpp::ReceiveTXTcallback _receiveCallback;
        unsigned long _lastRecv = 0;
        unsigned long _lastConnected = 0;
        
        // ── Dynamic Server Config ──
        char _serverHost[128] = {0};
        uint16_t _serverPort = 443;
        char _chargerId[32] = {0};
        bool _serverConfigured = false;

        // ── GSM Socket State ──
        bool _gsmWsConnected = false;
        uint32_t _lastGsmPing = 0;
        uint32_t _lastConnectAttempt = 0;
        SSLClient* _sslClient = nullptr;

        // ── Post-OTA Cooldown ──
        // After OTA download ends, the modem is exhausted.
        // We track when OTA mode cleared and enforce a 15s rest
        // before attempting TLS reconnect, preventing task WDT crash.
        bool _lastOtaActive = false;
        uint32_t _otaClearedAt = 0;
    };

} // namespace prod

// Global instance — declared in OcppService.cpp, used by GsmHttpDownload
namespace prod { extern UnifiedConnection* g_unifiedConnectionPtr; }
