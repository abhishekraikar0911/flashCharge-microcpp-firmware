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
#include <modules/gsm_manager.h>
#include <modules/network_manager.h>
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
    };

} // namespace prod
