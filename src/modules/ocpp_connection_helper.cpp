/**
 * @file ocpp_connection_helper.cpp
 * @brief Unified MicroOcpp Connection wrapper implementation
 * 
 * Drives WiFi via WebSockets library and GSM via manual WebSocket logic + SSLClient.
 * 
 * @author Rivot Motors
 * @date 2026
 */

#include "../../include/modules/ocpp_connection_helper.h"
#include "../../include/secrets.h"
#include <Arduino.h>

namespace prod {

    UnifiedConnection::UnifiedConnection() {
        // Initialize WiFi WebSocket
        _wifiWS = new WebSocketsClient();
        
        // Wrap it in MicroOcpp's standard WSClient
        _wifiConn = std::unique_ptr<MicroOcpp::Connection>(new MicroOcpp::EspWiFi::WSClient(_wifiWS));
    }

    UnifiedConnection::~UnifiedConnection() {
        delete _wifiWS;
        if (_sslClient) delete _sslClient;
    }

    void UnifiedConnection::setReceiveTXTcallback(MicroOcpp::ReceiveTXTcallback &callback) {
        _receiveCallback = callback;
        _wifiConn->setReceiveTXTcallback(callback);
    }

    void UnifiedConnection::loop() {
        static uint32_t lastLoopLog = 0;
        if (millis() - lastLoopLog >= 10000) {
            lastLoopLog = millis();
            Serial.printf("[CON_HB] loop() called, netState=%d, gsmConn=%d, wsConn=%d\n", 
                         (int)g_networkManager.getActiveConnection(), 
                         (int)g_gsmManager.isConnected(), 
                         (int)_gsmWsConnected);
        }

        if (g_networkManager.getActiveConnection() == ConnectionType::WIFI) {
            _wifiConn->loop();
        } else if (g_networkManager.getActiveConnection() == ConnectionType::GSM) {
            loopGSM();
        }
    }

    bool UnifiedConnection::sendTXT(const char *msg, size_t length) {
        g_networkManager.notifyActivity();

        if (g_networkManager.getActiveConnection() == ConnectionType::WIFI) {
            return _wifiConn->sendTXT(msg, length);
        } else if (g_networkManager.getActiveConnection() == ConnectionType::GSM) {
            return sendGsmTXT(msg, length);
        }
        return false;
    }

    unsigned long UnifiedConnection::getLastRecv() {
        if (g_networkManager.getActiveConnection() == ConnectionType::WIFI) {
            return _wifiConn->getLastRecv();
        }
        return _lastRecv;
    }

    unsigned long UnifiedConnection::getLastConnected() {
        if (g_networkManager.getActiveConnection() == ConnectionType::WIFI) {
            return _wifiConn->getLastConnected();
        }
        return _lastConnected;
    }

    bool UnifiedConnection::isConnected() {
        if (g_networkManager.getActiveConnection() == ConnectionType::WIFI) {
            return _wifiConn->isConnected();
        }
        return _gsmWsConnected;
    }

    // ═══════════════════════════════════════════════════════════
    //  GSM CUSTOM WEBSOCKET LOGIC (with SSL via SSLClient)
    // ═══════════════════════════════════════════════════════════

    void UnifiedConnection::loopGSM() {
        if (!g_gsmManager.isConnected()) {
            if (_gsmWsConnected) {
                Serial.println("[WS_GSM] ⚠️ GSM disconnected — tearing down SSL");
            }
            _gsmWsConnected = false;
            // CRITICAL FIX: Destroy SSLClient so TLS state is fresh on reconnect
            if (_sslClient) {
                _sslClient->stop();
                delete _sslClient;
                _sslClient = nullptr;
            }
            return;
        }

        // Create a FRESH SSLClient for every new connection attempt
        if (!_sslClient) {
            _sslClient = new SSLClient(&g_gsmManager.getClient());
            Serial.println("[WS_GSM] 🔒 SSLClient created (fresh TLS state)");
        }

        // ── 1. Connect and Handshake ──
        if (!_gsmWsConnected) {
            Serial.printf("[WS_GSM] 🔌 Connecting to %s:%d (GSM+TLS)...\n", SECRET_CSMS_HOST, SECRET_CSMS_PORT);
            
            bool tlsOk = _sslClient->connect(SECRET_CSMS_HOST, SECRET_CSMS_PORT);
            
            if (tlsOk) {
                if (gsmHandshake()) {
                    _gsmWsConnected = true;
                    _lastConnected = millis();
                    _lastRecv = millis();
                    Serial.println("[WS_GSM] ✅ WebSocket connected");
                } else {
                    Serial.println("[WS_GSM] ❌ Handshake failed — destroying SSLClient");
                    _sslClient->stop();
                    delete _sslClient;
                    _sslClient = nullptr;
                }
            } else {
                Serial.println("[WS_GSM] ❌ TLS connection failed — destroying SSLClient");
                delete _sslClient;
                _sslClient = nullptr;
            }
            return;
        }

        // ── 2. Check TCP connection health ──
        if (!_sslClient->connected()) {
            Serial.println("[WS_GSM] ⚠️ TCP connection lost — destroying SSLClient for clean reconnect");
            _gsmWsConnected = false;
            _sslClient->stop();
            delete _sslClient;
            _sslClient = nullptr;
            return;
        }

        // ── 3. Read Incoming Frames ──
        if (_sslClient->available()) {
            uint8_t header = _sslClient->read();
            uint8_t lenByte = _sslClient->read();
            uint8_t opcode = header & 0x0F;
            size_t len = lenByte & 0x7F;

            if (len == 126) {
                len = (_sslClient->read() << 8) | _sslClient->read();
            } else if (len == 127) {
                _sslClient->stop();
                _gsmWsConnected = false;
                return;
            }

            if (opcode == 0x01) { // Text frame
                std::vector<char> buffer(len + 1);
                _sslClient->readBytes(buffer.data(), len);
                buffer[len] = '\0';
                
                _lastRecv = millis();
                g_networkManager.notifyActivity();

                if (_receiveCallback) {
                    _receiveCallback(buffer.data(), len);
                }
            } else if (opcode == 0x08) { // Close frame — server closing connection
                Serial.println("[WS_GSM] 🔌 Server sent Close frame — disconnecting & destroying SSLClient");
                // Send masked Close response
                sendGsmFrame(0x08, nullptr, 0);
                _sslClient->stop();
                delete _sslClient;
                _sslClient = nullptr;
                _gsmWsConnected = false;
                return;
            } else if (opcode == 0x09) { // Ping → Pong
                sendGsmFrame(0x0A, nullptr, 0);
                _lastRecv = millis();
            } else if (opcode == 0x0A) { // Pong
                _lastRecv = millis();
            }
        }

        // ── 4. Manual Heartbeat (Ping every 20s — within proxy timeout window) ──
        if (millis() - _lastGsmPing >= 20000) {
            _lastGsmPing = millis();
            if (!sendGsmFrame(0x09, nullptr, 0)) {
                Serial.println("[WS_GSM] ⚠️ Ping write failed — destroying SSLClient");
                _gsmWsConnected = false;
                _sslClient->stop();
                delete _sslClient;
                _sslClient = nullptr;
                return;
            }
        }

    }

    bool UnifiedConnection::gsmHandshake() {
        if (!_sslClient) return false;

        // ── Build Handshake Request ──
        char handshake[512];
        const char* path = "/ocpp16";
        
        snprintf(handshake, sizeof(handshake),
            "GET %s/%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Protocol: ocpp1.6\r\n"
            "\r\n",
            path, SECRET_CHARGER_ID, 
            SECRET_CSMS_HOST);

        Serial.println("[WS_GSM] 📤 Sending handshake:");
        Serial.print(handshake);

        _sslClient->write((const uint8_t*)handshake, strlen(handshake));

        // ── Read Response ──
        uint32_t start = millis();
        bool gotResponse = false;
        while (millis() - start < 10000) {
            if (_sslClient->available()) {
                gotResponse = true;
                String line = _sslClient->readStringUntil('\n');
                line.trim();
                
                if (line.indexOf("101") != -1) {
                    while (_sslClient->available()) {
                        String hdr = _sslClient->readStringUntil('\n');
                        hdr.trim();
                        if (hdr.length() == 0) break;
                    }
                    return true;
                }
                
                if (line.startsWith("HTTP/") && line.indexOf("101") == -1) {
                    Serial.printf("[WS_GSM] ❌ HTTP Upgrade rejected: %s\n", line.c_str());
                    return false;
                }
            }
            delay(50);
        }
        
        if (!gotResponse) {
            Serial.println("[WS_GSM] ❌ No response (timeout)");
        }
        return false;
    }

    bool UnifiedConnection::sendGsmTXT(const char *msg, size_t length) {
        return sendGsmFrame(0x01, (const uint8_t*)msg, length);
    }

    bool UnifiedConnection::sendGsmFrame(uint8_t opcode, const uint8_t *payload, size_t length) {
        if (!_gsmWsConnected || !_sslClient) return false;

        // ── 1. Calculate Frame Size ──
        size_t headerSize = 2; // Opcode + Len byte
        if (length > 125) headerSize += 2; // 16-bit len
        headerSize += 4; // Mask key (mandatory for client -> server)
        
        size_t totalSize = headerSize + length;
        if (totalSize > 2048) {
            Serial.printf("[WS_GSM] ❌ Frame too large (%u bytes)\n", totalSize);
            return false;
        }

        std::vector<uint8_t> frame(totalSize);
        size_t pos = 0;

        // ── 2. Build Header ──
        frame[pos++] = (uint8_t)(0x80 | (opcode & 0x0F)); // FIN + Opcode

        if (length <= 125) {
            frame[pos++] = (uint8_t)(length | 0x80); // Mask bit + length
        } else {
            frame[pos++] = (uint8_t)(126 | 0x80); // Mask bit + 126
            frame[pos++] = (uint8_t)(length >> 8);
            frame[pos++] = (uint8_t)(length & 0xFF);
        }

        // ── 3. Mask Key ──
        uint8_t mask[4] = {0x13, 0x37, 0x77, 0x42};
        for (int i = 0; i < 4; i++) {
            frame[pos++] = mask[i];
        }

        // ── 4. Mask Payload ──
        for (size_t i = 0; i < length; i++) {
            frame[pos++] = payload[i] ^ mask[i % 4];
        }

        // ── 5. Send as a SINGLE TLS Record ──
        size_t sent = _sslClient->write(frame.data(), totalSize);
        
        if (sent != totalSize) {
            Serial.println("[WS_GSM] ❌ Failed to send full frame");
            return false;
        }

        return true;
    }

} // namespace prod
