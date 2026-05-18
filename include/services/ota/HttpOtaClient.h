#pragma once
/**
 * @file HttpOtaClient.h
 * @brief Custom HTTPS OTA downloader for MicroOcpp (replaces built-in FtpClient).
 *
 * MicroOcpp's default transport only supports ftp:// and ftps://.
 * This class subclasses FtpClient to support https:// URLs (S3/MinIO CDN),
 * dispatching to WiFi or native GSM HTTPS transport based on the active connection.
 *
 * GSM Transport: Uses A7670 modem's built-in HTTPS AT commands (AT+SH...).
 * This keeps TLS encryption entirely inside the modem — the ESP32 UART only
 * carries plain decrypted bytes, eliminating the UART buffer overflow /
 * MBEDTLS_ERR_SSL_INVALID_MAC error seen with the old SSLClient approach.
 *
 * Integration: injected via getOcppContext()->setFtpClient(...)
 *              before setDownloadFileWriter() is called.
 *
 * @author Rivot Motors
 * @date 2026
 */

#ifndef HTTP_OTA_CLIENT_H
#define HTTP_OTA_CLIENT_H

#include <MicroOcpp/Core/Ftp.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <functional>
#include <memory>

namespace prod {

// ─── WiFi HTTPS Download Handle ─────────────────────────────────────────────
/**
 * Non-blocking HTTPS download over WiFiClientSecure.
 * loop() is called each mocpp_loop() tick — reads one chunk per call,
 * ensuring the OCPP heartbeat loop is never blocked during download.
 */
class WifiHttpDownload : public MicroOcpp::FtpDownload {
public:
    WifiHttpDownload(
        const char* url,
        std::function<size_t(unsigned char*, size_t)> writer,
        std::function<void(MO_FtpCloseReason)>        onClose,
        const char* caCert);

    ~WifiHttpDownload();

    void loop()     override;
    bool isActive() override { return _active; }

private:
    HTTPClient       _http;
    WiFiClientSecure _client;
    std::function<size_t(unsigned char*, size_t)> _writer;
    std::function<void(MO_FtpCloseReason)>        _onClose;

    static constexpr size_t CHUNK = 512;
    uint8_t _buf[CHUNK];

    int  _contentLength = -1;
    int  _bytesReceived = 0;
    bool _active        = false;
};

// ─── Native GSM HTTPS Download Handle ───────────────────────────────────────
/**
 * Non-blocking HTTPS download using the A7670 modem's built-in HTTPS engine
 * (AT+SHSSL / AT+SHCONF / AT+SHCONN / AT+SHREQ / AT+SHREAD / AT+SHDISC).
 *
 * Key advantage over the old SSLClient approach:
 *   - TLS runs INSIDE the modem. The UART only carries plain binary bytes.
 *   - No UART buffer overflow → No MBEDTLS_ERR_SSL_INVALID_MAC errors.
 *   - OCPP WebSocket SSLClient does NOT need to be torn down during download.
 *
 * States (driven by loop() each mocpp_loop() tick):
 *   SETUP  → Send AT commands to configure, connect, and issue GET.
 *   STREAM → Read body chunks via AT+SHREAD, pass to writer(), feed WDT.
 *   DONE   → Trigger onClose callback (success or failure).
 */
class NativeGsmHttpDownload : public MicroOcpp::FtpDownload {
public:
    NativeGsmHttpDownload(
        const char* url,
        std::function<size_t(unsigned char*, size_t)> writer,
        std::function<void(MO_FtpCloseReason)>        onClose,
        const char* caCert);

    ~NativeGsmHttpDownload();

    void loop()     override;
    bool isActive() override { return _active; }

private:
    // ── Download state machine ──
    enum class State { SETUP, STREAMING, DONE };
    State _state = State::SETUP;

    // ── AT command helpers ──
    bool _sendATBlocking(const char* cmd, const char* expected,
                         uint32_t timeoutMs = 10000);
    bool _sendATRaw(const char* cmd);
    bool _httpInit();         // Init modem HTTP engine (called once per range)
    bool _setup();            // One-time init: WebSocket teardown + get file size
    bool _downloadRange();    // Download one RANGE_SIZE slice and write to flash
    void _stream();           // Called each loop() tick — triggers one range download
    void _finish(MO_FtpCloseReason reason);

    std::function<size_t(unsigned char*, size_t)> _writer;
    std::function<void(MO_FtpCloseReason)>        _onClose;

    // ── Parsed URL fields ──
    char _host[128]  = {};
    char _path[512]  = {};
    char _url[640]   = {};
    uint16_t _port   = 443;
    bool _isHttps    = true;

    // ── Progress tracking ──
    int  _contentLength  = -1;
    int  _bytesReceived  = 0;
    int  _rangeStart     = 0;   // Byte offset for the next range request
    bool _active         = false;

    // ── Timing ──
    uint32_t _lastWdtFeed = 0;
    uint32_t _lastDataMs  = 0;
    static constexpr uint32_t STALL_TIMEOUT_MS  = 120000;  // 2 min
    static constexpr uint32_t WDT_FEED_INTERVAL = 5000;

    // ── Chunk / Range sizes ──
    static constexpr int CHUNK      = 512;    // Bytes read per HTTPREAD call
    static constexpr int RANGE_SIZE = 61440;  // 60 KB per range request (safe for modem buffer)
    uint8_t _buf[CHUNK];

    // ── Modem channel ──
    static constexpr int MODEM_CH = 1;

    // ── CA certificate for TLS ──
    const char* _caCert = nullptr;
};

// ─── Unified HTTPS FtpClient ─────────────────────────────────────────────────
/**
 * Implements MicroOcpp's FtpClient interface using HTTPS.
 * Dispatches to WifiHttpDownload (WiFi) or NativeGsmHttpDownload (GSM A7670)
 * based on the active network connection at the time of download.
 */
class HttpOtaClient : public MicroOcpp::FtpClient {
public:
    explicit HttpOtaClient(const char* caCert) : _caCert(caCert) {}

    std::unique_ptr<MicroOcpp::FtpDownload> getFile(
        const char* url,
        std::function<size_t(unsigned char*, size_t)> writer,
        std::function<void(MO_FtpCloseReason)>        onClose,
        const char* ca_cert = nullptr) override;

    // Upload not needed for OTA receive path
    std::unique_ptr<MicroOcpp::FtpUpload> postFile(
        const char*,
        std::function<size_t(unsigned char*, size_t)>,
        std::function<void(MO_FtpCloseReason)>,
        const char*) override
    {
        return nullptr;
    }

private:
    const char* _caCert;
};

} // namespace prod

#endif // HTTP_OTA_CLIENT_H
