#pragma once
/**
 * @file HttpOtaClient.h
 * @brief Custom HTTPS OTA downloader for MicroOcpp (replaces built-in FtpClient).
 *
 * MicroOcpp's default transport only supports ftp:// and ftps://.
 * This class subclasses FtpClient to support https:// URLs (S3/MinIO CDN),
 * dispatching to WiFi or GSM transport based on the active network connection.
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
#include <SSLClient.h>
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

// ─── GSM HTTPS Download Handle ───────────────────────────────────────────────
/**
 * Non-blocking HTTPS download over the A7670 cellular modem using SSLClient.
 * Uses the same TinyGsmClient+SSLClient pattern as the GSM WebSocket transport.
 * Manually parses HTTP/1.1 headers (HTTPClient cannot use TinyGsmClient directly).
 */
class GsmHttpDownload : public MicroOcpp::FtpDownload {
public:
    GsmHttpDownload(
        const char* url,
        std::function<size_t(unsigned char*, size_t)> writer,
        std::function<void(MO_FtpCloseReason)>        onClose,
        const char* caCert);

    ~GsmHttpDownload();

    void loop()     override;
    bool isActive() override { return _active; }

private:
    SSLClient*  _ssl          = nullptr;  // Only created for https://
    Client*     _activeClient = nullptr;  // Points to _ssl (https) or raw TinyGsmClient (http)
    std::function<size_t(unsigned char*, size_t)> _writer;
    std::function<void(MO_FtpCloseReason)>        _onClose;

    static constexpr size_t CHUNK = 512;
    uint8_t _buf[CHUNK];

    int  _contentLength = -1;
    int  _bytesReceived = 0;
    bool _active        = false;
    bool _headersParsed = false;
    uint32_t _lastWdtFeed = 0;   // Periodic WDT feed timer
    uint32_t _lastDataMs  = 0;   // Last time bytes were received (stall detection)

    String _line;                 // Header line accumulator — must be a member,
                                  // NOT static, so each instance starts clean.

    bool _parseHeaders();  // Non-blocking header parser; returns true when done
};

// ─── Unified HTTPS FtpClient ─────────────────────────────────────────────────
/**
 * Implements MicroOcpp's FtpClient interface using HTTPS.
 * Dispatches to WifiHttpDownload (WiFi) or GsmHttpDownload (GSM A7670)
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
