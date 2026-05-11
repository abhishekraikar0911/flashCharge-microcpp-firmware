/**
 * @file HttpOtaClient.cpp
 * @brief Custom HTTPS OTA downloader implementation.
 *
 * Replaces MicroOcpp's built-in FTP client (which only handles ftp:// and
 * ftps://) with an HTTPS-capable transport that supports S3/MinIO pre-signed
 * URLs.
 *
 * Key design decisions:
 *  - Non-blocking: loop() reads one CHUNK per call, never stalling mocpp_loop()
 *  - Dispatches to WiFi or GSM transport based on active connection type
 *  - Uses ISRG_ROOT_X1_CERT for TLS validation (same CA as OCPP WebSocket)
 *  - 90-second stall timeout to detect and recover from TLS stream deadlock
 *
 * @author Rivot Motors
 * @date 2026
 */

#include "system/HttpOtaClient.h"
#include "system/NetworkManager.h"
#include "system/GsmManager.h"
#include "system/HealthMonitor.h"
#include "config/certificates.h"
#include "services/OcppConnectionHelper.h"
#include <Arduino.h>

namespace prod {

// =============================================================================
// WifiHttpDownload — Non-blocking HTTPS download over WiFiClientSecure
// =============================================================================

WifiHttpDownload::WifiHttpDownload(
    const char* url,
    std::function<size_t(unsigned char*, size_t)> writer,
    std::function<void(MO_FtpCloseReason)>        onClose,
    const char* caCert)
    : _writer(writer), _onClose(onClose)
{
    Serial.printf("[OTA_HTTP] Connecting HTTPS (WiFi): %s\n", url);

    _client.setCACert(caCert ? caCert : ISRG_ROOT_X1_CERT);

    if (!_http.begin(_client, url)) {
        Serial.println("[OTA_HTTP] ERROR: HTTPClient.begin() failed");
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    _http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    _http.setTimeout(30000);
    _http.addHeader("Accept", "application/octet-stream");

    int code = _http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[OTA_HTTP] ERROR: HTTP GET failed: %d\n", code);
        _http.end();
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    _contentLength = _http.getSize();
    Serial.printf("[OTA_HTTP] WiFi OK. Content-Length: %d bytes\n", _contentLength);
    _active = true;
}

WifiHttpDownload::~WifiHttpDownload() {
    _http.end();
}

void WifiHttpDownload::loop() {
    if (!_active) return;

    WiFiClient* stream = _http.getStreamPtr();
    if (!stream) {
        Serial.println("[OTA_HTTP] ERROR: Null stream (WiFi)");
        _active = false;
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    if (!stream->connected() && stream->available() == 0) {
        if (_contentLength < 0 || _bytesReceived >= _contentLength) {
            Serial.printf("[OTA_HTTP] WiFi download complete: %d bytes\n", _bytesReceived);
            _active = false;
            if (_onClose) _onClose(MO_FtpCloseReason_Success);
        } else {
            Serial.printf("[OTA_HTTP] ERROR: WiFi lost at %d/%d bytes\n",
                          _bytesReceived, _contentLength);
            _active = false;
            if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        }
        return;
    }

    int avail = stream->available();
    if (avail <= 0) return;

    size_t toRead = min((int)CHUNK, avail);
    size_t read   = stream->readBytes(_buf, toRead);
    if (read == 0) return;

    size_t consumed = _writer(_buf, read);
    if (consumed == 0) {
        Serial.println("[OTA_HTTP] ERROR: OTA writer aborted (WiFi)");
        _active = false;
        _http.end();
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    _bytesReceived += (int)read;

    if (_contentLength > 0 && _bytesReceived >= _contentLength) {
        Serial.printf("[OTA_HTTP] WiFi download complete: %d bytes\n", _bytesReceived);
        _active = false;
        if (_onClose) _onClose(MO_FtpCloseReason_Success);
    }
}

// =============================================================================
// GsmHttpDownload — Non-blocking HTTPS download over A7670 + SSLClient
// =============================================================================

// How long (ms) to wait without receiving any bytes before giving up.
// A7670 LTE link should always deliver within this window if the server
// has data. 90 seconds gives plenty of margin for slow cellular conditions.
static constexpr uint32_t OTA_STALL_TIMEOUT_MS = 90000;

GsmHttpDownload::GsmHttpDownload(
    const char* url,
    std::function<size_t(unsigned char*, size_t)> writer,
    std::function<void(MO_FtpCloseReason)>        onClose,
    const char* caCert)
    : _writer(writer), _onClose(onClose)
{
    Serial.printf("[OTA_HTTP] Starting GSM download: %s\n", url);

    if (!g_gsmManager.isConnected()) {
        Serial.println("[OTA_HTTP] ERROR: GSM modem not connected");
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    // OTA GUARD: Prevent WebSocket reconnect and Idle Watchdog from stealing
    // the modem's single TCP slot while we are downloading.
    g_networkManager.setOtaActive(true);  // also resets _lastActivityTime
    _lastWdtFeed = millis();
    Serial.println("[OTA_HTTP] Modem slot reserved (WS reconnect + idle watchdog suppressed)");

    // CRITICAL: Explicitly teardown the WebSocket SSLClient if it exists.
    // This is required because the mbedTLS state in the WS SSLClient
    // conflicts with the new SSLClient we are about to create.
    if (g_unifiedConnectionPtr) {
        g_unifiedConnectionPtr->teardownGsmWebSocket();
    }

    // Force-close any existing TCP connection on the modem slot.

    // loopGSM() runs BEFORE getFile() in the same mocpp_loop() tick, so the
    // WebSocket SSLClient may still be alive. Calling stop() here evicts it
    // so our OTA SSLClient gets a clean connection.
    g_gsmManager.getClient().stop();
    delay(300);
    Serial.println("[OTA_HTTP] Modem TCP slot cleared");

    // -- Parse URL: extract scheme, host, port, path --
    char host[128] = {};
    char path[512] = {};
    uint16_t port = 443;
    bool isHttps  = true;

    const char* src = url;
    if      (strncmp(src, "https://", 8) == 0) { src += 8; isHttps = true;  port = 443; }
    else if (strncmp(src, "http://",  7) == 0) { src += 7; isHttps = false; port = 80;  }
    else {
        Serial.println("[OTA_HTTP] ERROR: Unsupported URL scheme (not http/https)");
        g_networkManager.setOtaActive(false);
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    const char* slash = strchr(src, '/');
    size_t hostLen = slash ? (size_t)(slash - src) : strlen(src);
    strncpy(host, src, min(hostLen, sizeof(host) - 1));

    // Parse explicit port from host (e.g. host:9000)
    char* colon = strchr(host, ':');
    if (colon) {
        port = (uint16_t)atoi(colon + 1);
        *colon = '\0';
    }

    strncpy(path, slash ? slash : "/", sizeof(path) - 1);

    Serial.printf("[OTA_HTTP]    Host: %s  Port: %d  HTTPS: %d\n", host, port, (int)isHttps);

    // -- Choose transport --
    if (isHttps) {
        _ssl = new SSLClient(&g_gsmManager.getClient());
        _ssl->setCACert(caCert ? caCert : ISRG_ROOT_X1_CERT);
        _activeClient = _ssl;
        Serial.println("[OTA_HTTP]    Transport: SSLClient (TLS)");
    } else {
        _ssl = nullptr;
        _activeClient = &g_gsmManager.getClient();
        Serial.println("[OTA_HTTP]    Transport: TinyGsmClient (plain HTTP)");
    }

    // -- Connect --
    Serial.printf("[OTA_HTTP]    Connecting to %s:%d...\n", host, port);
    if (!_activeClient->connect(host, port)) {
        Serial.println("[OTA_HTTP] ERROR: TCP/TLS connect failed");
        if (_ssl) { delete _ssl; _ssl = nullptr; }
        _activeClient = nullptr;
        g_networkManager.setOtaActive(false);
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }
    Serial.println("[OTA_HTTP]    Connected OK");

    // -- Send HTTP GET request --
    _activeClient->printf("GET %s HTTP/1.1\r\n", path);
    _activeClient->printf("Host: %s\r\n", host);
    _activeClient->print("Accept: application/octet-stream\r\n");
    _activeClient->print("Connection: close\r\n");
    _activeClient->print("\r\n");

    // -- Read status line: blocking, 10 s max --
    // (Only the status line is read here; all remaining headers are parsed
    //  non-blocking in loop() via _parseHeaders().)
    uint32_t deadline = millis() + 10000;
    String statusLine;
    while (millis() < deadline) {
        if (_activeClient->available()) {
            char c = _activeClient->read();
            if (c == '\n') break;
            if (c != '\r') statusLine += c;
        }
        delay(1);
    }
    Serial.printf("[OTA_HTTP]    Status: %s\n", statusLine.c_str());

    if (statusLine.indexOf("200") < 0 && statusLine.indexOf("206") < 0) {
        Serial.printf("[OTA_HTTP] ERROR: Server returned: %s\n", statusLine.c_str());
        _activeClient->stop();
        if (_ssl) { delete _ssl; _ssl = nullptr; }
        _activeClient = nullptr;
        g_networkManager.setOtaActive(false);
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    _lastDataMs = millis();  // Start stall timer from connection OK
    _active     = true;
    Serial.println("[OTA_HTTP] HTTP OK -- streaming body via GSM...");
}

GsmHttpDownload::~GsmHttpDownload() {
    g_networkManager.setOtaActive(false);  // also resets _lastActivityTime
    Serial.println("[OTA_HTTP] Modem slot released (WS reconnect resumed)");

    if (_ssl) {
        _ssl->stop();
        delete _ssl;
        _ssl = nullptr;
    } else if (_activeClient) {
        _activeClient->stop();
    }
    _activeClient = nullptr;
}

// ---------------------------------------------------------------------------
// _parseHeaders()
// Non-blocking: reads one character at a time using the member _line buffer.
// Returns true when the blank line (end-of-headers) is found.
// BUG FIX: was a static local — now a member variable so each instance is clean.
// ---------------------------------------------------------------------------
bool GsmHttpDownload::_parseHeaders() {
    if (!_activeClient) return false;

    while (_activeClient->available()) {
        char c = _activeClient->read();
        if (c == '\n') {
            // Strip trailing \r
            if (_line.length() > 0 && _line[_line.length()-1] == '\r') {
                _line.remove(_line.length()-1);
            }
            if (_line.length() == 0) {
                // Blank line = end of headers
                Serial.printf("[OTA_HTTP] Headers parsed. Content-Length: %d bytes\n",
                              _contentLength);
                _headersParsed = true;
                return true;
            }
            // Extract Content-Length
            if (_line.startsWith("Content-Length:") ||
                _line.startsWith("content-length:")) {
                _contentLength = _line.substring(_line.indexOf(':') + 1).toInt();
            }
            _line = "";  // Ready for next header line
        } else if (c != '\r') {
            _line += c;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// loop() — called every mocpp_loop() tick while download is in progress.
// ---------------------------------------------------------------------------
void GsmHttpDownload::loop() {
    if (!_active) return;

    if (!_activeClient) {
        Serial.println("[OTA_HTTP] ERROR: Client lost mid-download");
        _active = false;
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    uint32_t now = millis();

    // -- Periodic WDT feed (every 5 s) --
    // Hardware WDT is 60 s; feeding every 5 s gives ample headroom even if
    // the GSM stream stalls between TLS records.
    if (now - _lastWdtFeed >= 5000) {
        g_healthMonitor.feed();
        _lastWdtFeed = now;
    }

    // -- Stall timeout --
    // If no data has arrived for OTA_STALL_TIMEOUT_MS, the TLS stream is
    // irrecoverably stuck. Fail cleanly so MicroOcpp can retry.
    if (now - _lastDataMs >= OTA_STALL_TIMEOUT_MS) {
        Serial.printf("[OTA_HTTP] ERROR: GSM stall — no data for %lu s. Aborting.\n",
                      (unsigned long)(now - _lastDataMs) / 1000);
        _active = false;
        _activeClient->stop();
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    // -- Phase 1: Parse remaining HTTP headers (non-blocking) --
    if (!_headersParsed) {
        _parseHeaders();   // Reads all chars currently available
        // Reset stall timer — receiving headers counts as activity
        if (_activeClient->available() || g_gsmManager.getClient().available()) {
            _lastDataMs = now;
        }
        return;
    }

    // -- Phase 2: Drain and detect connection close --
    // IMPORTANT: always drain available bytes BEFORE checking connected().
    // SSLClient may report !connected() after the server sends FIN, but there
    // may still be buffered plaintext to read.

    // -- Phase 3: Read body data --
    // SSLClient::available() can return 0 even when ciphertext is waiting in
    // the underlying TinyGsmClient, because it only reports DECRYPTED bytes.
    // If SSLClient says 0 but TinyGsmClient has raw bytes, force a read to
    // pump the TLS decryptor.
    int avail = _activeClient->available();
    if (avail == 0 && _ssl) {
        // Check raw modem buffer
        int rawAvail = g_gsmManager.getClient().available();
        if (rawAvail > 0) {
            // Force SSLClient to decrypt the pending TLS record.
            // readBytes(1) will consume the full TLS record internally
            // and make the decrypted bytes available on the next call.
            uint8_t tmp;
            int got = _activeClient->readBytes(&tmp, 1);
            if (got > 0) {
                // Got first byte — pass it to the writer and continue
                _lastDataMs = now;
                g_healthMonitor.feed();
                _lastWdtFeed = now;
                size_t consumed = _writer(&tmp, 1);
                if (consumed == 0) {
                    Serial.println("[OTA_HTTP] ERROR: OTA writer aborted (GSM)");
                    _active = false;
                    _activeClient->stop();
                    if (_onClose) _onClose(MO_FtpCloseReason_Failure);
                    return;
                }
                _bytesReceived += 1;
            }
            // Re-query — decryptor may have buffered more plaintext now
            avail = _activeClient->available();
        }
    }

    if (avail <= 0) {
        // No decrypted data yet. Check for clean EOF.
        if (!_activeClient->connected() && _activeClient->available() == 0) {
            if (_contentLength < 0 || _bytesReceived >= _contentLength) {
                Serial.printf("[OTA_HTTP] GSM download complete: %d bytes\n",
                              _bytesReceived);
                _active = false;
                if (_onClose) _onClose(MO_FtpCloseReason_Success);
            } else {
                Serial.printf("[OTA_HTTP] ERROR: GSM lost at %d/%d bytes\n",
                              _bytesReceived, _contentLength);
                _active = false;
                if (_onClose) _onClose(MO_FtpCloseReason_Failure);
            }
        }
        return;
    }

    // Read one chunk of decrypted plaintext
    size_t toRead = min((size_t)avail, CHUNK);
    size_t read   = _activeClient->readBytes(_buf, toRead);
    if (read == 0) return;

    _lastDataMs = now;       // Data received — reset stall timer
    g_healthMonitor.feed();  // Feed WDT on every chunk
    _lastWdtFeed = now;

    size_t consumed = _writer(_buf, read);
    if (consumed == 0) {
        Serial.println("[OTA_HTTP] ERROR: OTA writer aborted (GSM)");
        _active = false;
        _activeClient->stop();
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    _bytesReceived += (int)read;

    // Progress log every 64 KB
    if ((_bytesReceived % 65536) < (int)read) {
        if (_contentLength > 0) {
            Serial.printf("[OTA_HTTP] GSM progress: %d / %d (%.1f%%)\n",
                          _bytesReceived, _contentLength,
                          100.0f * _bytesReceived / _contentLength);
        } else {
            Serial.printf("[OTA_HTTP] GSM progress: %d bytes\n", _bytesReceived);
        }
    }

    // Completion by Content-Length
    if (_contentLength > 0 && _bytesReceived >= _contentLength) {
        Serial.printf("[OTA_HTTP] GSM download complete: %d bytes\n", _bytesReceived);
        _active = false;
        if (_onClose) _onClose(MO_FtpCloseReason_Success);
    }
}

// =============================================================================
// HttpOtaClient — FtpClient dispatch (WiFi-first, GSM fallback)
// =============================================================================

std::unique_ptr<MicroOcpp::FtpDownload> HttpOtaClient::getFile(
    const char* url,
    std::function<size_t(unsigned char*, size_t)> writer,
    std::function<void(MO_FtpCloseReason)>        onClose,
    const char* /*ca_cert*/)
{
    // ── STRATEGY: Always try WiFi first ──────────────────────────────────────
    // The A7670 modem can only hold one TLS session reliably. If we try to run
    // OTA over GSM while the OCPP WebSocket is also using the modem, we get:
    //   MBEDTLS_ERR_SSL_INVALID_MAC
    // WiFi uses a completely independent TLS stack — zero collision possible.
    //
    // Wrap onClose so WiFi radio is released after both success AND failure.
    auto wrappedOnClose = [onClose](MO_FtpCloseReason reason) {
        g_networkManager.releaseWiFiAfterOta();
        if (onClose) onClose(reason);
    };

    if (g_networkManager.requestWiFiForOta()) {
        Serial.println("[OTA_HTTP] ✅ Using WiFi transport (collision-free)");
        return std::unique_ptr<WifiHttpDownload>(
            new WifiHttpDownload(url, writer, wrappedOnClose, _caCert));
    }

    // ── FALLBACK: GSM ─────────────────────────────────────────────────────────
    // WiFi unavailable (no credentials or out of range). Try GSM.
    // Note: This may still fail due to modem TLS limits, but we try anyway.
    Serial.println("[OTA_HTTP] ⚠️  WiFi unavailable — falling back to GSM transport");
    Serial.println("[OTA_HTTP]    (If this fails with MAC error, ensure WiFi credentials are provisioned)");
    return std::unique_ptr<GsmHttpDownload>(
        new GsmHttpDownload(url, writer, onClose, _caCert));
}

} // namespace prod

