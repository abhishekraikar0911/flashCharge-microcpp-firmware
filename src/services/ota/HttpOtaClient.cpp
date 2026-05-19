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

HttpOtaClient.cpp (The "Delivery Man")
What's in it: Logic for both WifiHttpDownload and GsmHttpDownload. It contains the HTTP GET request code and TLS (SSL) security setup.
Why it was written: Standard OTA libraries for ESP32 usually only work on WiFi. We wrote this custom file because your charger needs to be able to update over a GSM Modem (A7670).
Key Logic: It includes a "GSM Teardown" feature. When a download starts over GSM, it "kills" the OCPP connection temporarily to free up memory so the download doesn't crash the chip.

 */

#include "services/ota/HttpOtaClient.h"
#include "services/network/NetworkManager.h"
#include "services/network/GsmManager.h"
#include "services/safety/HealthMonitor.h"
#include "config/certificates.h"
#include "config/hardware.h"
#include "services/ocpp/OcppConnectionHelper.h"
#include <esp_task_wdt.h>
#include <Arduino.h>
#include "system/state/SystemState.h"

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
// NativeGsmHttpDownload — HTTPS download via SSLClient + raw HTTP Range requests
// =============================================================================
//
// WHY THIS APPROACH:
//   The A7670 Classic HTTP AT engine (AT+HTTP...) has a hard 64KB buffer limit
//   and no working Range header support (BREAK/BREAKEND not in V11.0.01,
//   USERDATA ignored by server). Classic HTTP is dead for 1.3MB OTA.
//
// THIS APPROACH (same as the WebSocket uses):
//   SSLClient wrapping TinyGsmClient → raw HTTP/1.1 GET with Range header.
//   The WebSocket ALREADY proves this stack works. We just repurpose it for OTA.
//
// Flow per 60KB range slice:
//   1. Tear down WebSocket (frees the shared TinyGsmClient channel)
//   2. SSLClient ssl(&g_gsmManager.getClient()); ssl.setCACert(...);
//   3. ssl.connect(host, 443)
//   4. ssl.print("GET /path HTTP/1.1\r\nRange: bytes=X-Y\r\n...")
//   5. Read HTTP response headers → parse status + Content-Range
//   6. Stream body bytes directly to flash in 512B chunks (no modem buffer)
//   7. ssl.stop() → close TCP, free TinyGsmClient channel
//   8. Repeat until all bytes written

// ---------------------------------------------------------------------------
// Constructor — parse URL, store fields. Actual setup happens in _setup().
// ---------------------------------------------------------------------------
NativeGsmHttpDownload::NativeGsmHttpDownload(
    const char* url,
    std::function<size_t(unsigned char*, size_t)> writer,
    std::function<void(MO_FtpCloseReason)>        onClose,
    const char* caCert)
    : _writer(writer), _onClose(onClose), _caCert(caCert)
{
    Serial.printf("[OTA_GSM] Preparing native GSM HTTPS download: %s\n", url);

    if (!g_gsmManager.isConnected()) {
        Serial.println("[OTA_GSM] ERROR: GSM modem not connected");
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    strncpy(_url, url, sizeof(_url) - 1);

    const char* src = url;
    if      (strncmp(src, "https://", 8) == 0) { src += 8; _isHttps = true;  _port = 443; }
    else if (strncmp(src, "http://",  7) == 0) { src += 7; _isHttps = false; _port = 80;  }
    else {
        Serial.println("[OTA_GSM] ERROR: Unsupported URL scheme");
        if (_onClose) _onClose(MO_FtpCloseReason_Failure);
        return;
    }

    const char* slash = strchr(src, '/');
    size_t hostLen = slash ? (size_t)(slash - src) : strlen(src);
    strncpy(_host, src, min(hostLen, sizeof(_host) - 1));

    char* colon = strchr(_host, ':');
    if (colon) { _port = (uint16_t)atoi(colon + 1); *colon = '\0'; }

    strncpy(_path, slash ? slash : "/", sizeof(_path) - 1);

    Serial.printf("[OTA_GSM]   Host: %s  Port: %d  HTTPS: %d\n",
                  _host, _port, (int)_isHttps);

    g_networkManager.setOtaActive(true);
    _lastWdtFeed = millis();
    _lastDataMs  = millis();
    _active      = true;
    _state       = State::SETUP;
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
NativeGsmHttpDownload::~NativeGsmHttpDownload() {
    g_networkManager.setOtaActive(false);
    Serial.println("[OTA_GSM] Modem slot released");
}

// ---------------------------------------------------------------------------
// _finish() — Call onClose and mark download inactive
// ---------------------------------------------------------------------------
void NativeGsmHttpDownload::_finish(MO_FtpCloseReason reason) {
    _active = false;
    _state  = State::DONE;
    Serial.println("[OTA_GSM] Download session ended");
    if (_onClose) _onClose(reason);
}

// Stubs — AT helpers no longer used by the new SSLClient-based implementation
bool NativeGsmHttpDownload::_sendATRaw(const char*) { return true; }
bool NativeGsmHttpDownload::_sendATBlocking(const char*, const char*, uint32_t) { return true; }

// ---------------------------------------------------------------------------
// _setup() — One-time init: tear down WebSocket, reset state.
// ---------------------------------------------------------------------------
bool NativeGsmHttpDownload::_setup() {
    Serial.println("[OTA_GSM] === GSM OTA Setup (SSLClient Range Mode) ===");

    // Flush stale UART data
    while (GSM_SERIAL.available()) GSM_SERIAL.read();
    vTaskDelay(pdMS_TO_TICKS(100));
    while (GSM_SERIAL.available()) GSM_SERIAL.read();

    // Tear down WebSocket — frees the shared TinyGsmClient channel for OTA
    if (g_unifiedConnectionPtr) {
        Serial.println("[OTA_GSM] ⏳ Flushing OCPP queue (3 seconds) before taking over modem...");
        vTaskDelay(pdMS_TO_TICKS(3000)); // Let MicroOcpp send the 'Downloading' notification
        Serial.println("[OTA_GSM] Releasing OCPP WebSocket for OTA use...");
        g_unifiedConnectionPtr->teardownGsmWebSocket();
        vTaskDelay(pdMS_TO_TICKS(2000)); // Let modem fully close TCP
    }

    _contentLength = -1;
    _bytesReceived = 0;
    _rangeStart    = 0;
    _lastDataMs    = millis();

  //  Serial.println("[OTA_GSM] Setup OK — downloading in 60KB slices via SSLClient...");
    return true;
}

// ---------------------------------------------------------------------------
// _downloadRange() — Open a fresh TLS connection, send a Range GET, stream
//                    the body bytes directly to flash. No modem buffer involved.
// ---------------------------------------------------------------------------
bool NativeGsmHttpDownload::_downloadRange() {
    // ── CRITICAL: Prevent IDLE0 Starvation & TWDT Panic ────────────────────
    // mbedtls blocks inside ssl.read() waiting for a full TLS record.
    // Drop to priority 0 so yield() shares CPU with IDLE0, preventing TWDT panic.
    // Temporarily unsubscribe from TWDT while waiting for slow GSM packets.
    UBaseType_t originalPriority = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, 0);
    esp_task_wdt_delete(NULL);

    auto restorePriority = [originalPriority]() {
        esp_task_wdt_add(NULL);
        vTaskPrioritySet(NULL, originalPriority);
    };

    // Re-teardown WebSocket in case network manager reconnected between calls
    if (g_unifiedConnectionPtr) {
        g_unifiedConnectionPtr->teardownGsmWebSocket();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Flush any stale bytes from UART before starting TLS
    while (GSM_SERIAL.available()) GSM_SERIAL.read();

    // ── 1. Create TLS connection ─────────────────────────────────────────────
    SSLClient ssl(&g_gsmManager.getClient());
    ssl.setCACert(_caCert ? _caCert : ISRG_ROOT_X1_CERT);

    Serial.printf("[OTA_GSM] TLS connecting %s:%d...\n", _host, _port);
    if (!ssl.connect(_host, _port)) {
        Serial.println("[OTA_GSM] ERROR: TLS connect failed");
        restorePriority();
        return false;
    }
    Serial.println("[OTA_GSM] TLS connected");
    g_healthMonitor.feed();

    // ── 2. Send a SINGLE open-ended HTTP GET for the full remaining file ─────
    // RTS/CTS hardware flow control will automatically pause the modem during
    // flash sector erases, so we do NOT need to slice into 4KB/16KB chunks.
    // The modem's TX is hardware-gated by ESP32 GPIO14 (RTS pin).
    Serial.printf("[OTA_GSM] GET bytes=%d- (full stream, RTS/CTS active)...\n", _rangeStart);
    ssl.printf("GET %s HTTP/1.1\r\n", _path);
    ssl.printf("Host: %s\r\n", _host);
    ssl.printf("Range: bytes=%d-\r\n", _rangeStart);  // Open-ended: server sends everything
    ssl.printf("Connection: close\r\n");               // Single request — no keep-alive needed
    ssl.printf("Accept: application/octet-stream\r\n");
    ssl.printf("\r\n");

    // ── 3. Parse HTTP response headers ──────────────────────────────────────
    int httpStatus = 0;
    int bodyLength = -1;
    uint32_t hdrDeadline = millis() + 15000UL;

    while (millis() < hdrDeadline) {
        g_healthMonitor.feed();

        if (ssl.available()) {
            String line = ssl.readStringUntil('\n');
            line.trim();

            if (line.length() == 0) break; // Blank line = end of headers

            if (httpStatus == 0 && line.startsWith("HTTP/")) {
                int spaceIdx = line.indexOf(' ');
                if (spaceIdx > 0) httpStatus = line.substring(spaceIdx + 1, spaceIdx + 4).toInt();
            }
            if (line.startsWith("Content-Length:")) {
                bodyLength = line.substring(16).toInt();
            }
            if (line.startsWith("Content-Range:") && _contentLength < 0) {
                int slash = line.lastIndexOf('/');
                if (slash > 0) _contentLength = line.substring(slash + 1).toInt();
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (httpStatus != 200 && httpStatus != 206) {
        Serial.printf("[OTA_GSM] ERROR: Bad HTTP status %d\n", httpStatus);
        ssl.stop();
        restorePriority();
        return false;
    }
    if (bodyLength <= 0) {
        Serial.printf("[OTA_GSM] ERROR: No Content-Length in response\n");
        ssl.stop();
        restorePriority();
        return false;
    }

    Serial.printf("[OTA_GSM] Streaming %d bytes → flash (RTS/CTS hardware throttle active)...\n", bodyLength);

    // ── 4. Stream the full body directly to flash ────────────────────────────
    // RTS/CTS hardware pauses the modem automatically during flash erases.
    // No software slicing needed. Just read bytes as fast as they arrive.
    int totalRead = 0;
    int bufPos    = 0;
    // 5-minute deadline for the entire file (1.4MB at 11KB/s ≈ 2 min, give 3x margin)
    uint32_t bodyDeadline = millis() + 300000UL;

    while (totalRead < bodyLength && millis() < bodyDeadline) {
        g_healthMonitor.feed();

        if (ssl.available()) {
            _buf[bufPos++] = (uint8_t)ssl.read();
            totalRead++;

            if (bufPos == CHUNK || totalRead == bodyLength) {
                size_t consumed = _writer(_buf, bufPos);
                if (consumed == 0) {
                    Serial.println("[OTA_GSM] ERROR: OTA writer aborted");
                    ssl.stop();
                    restorePriority();
                    return false;
                }
                _bytesReceived += (int)consumed;
                _rangeStart    += (int)consumed;
                _lastDataMs     = millis();
                bufPos = 0;
                vTaskDelay(pdMS_TO_TICKS(5)); // Yield to FreeRTOS
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(5)); // Yield when stream momentarily idle
        }
    }

    if (totalRead < bodyLength) {
        Serial.printf("[OTA_GSM] ERROR: Stream ended early: %d/%d bytes\n", totalRead, bodyLength);
        ssl.stop();
        restorePriority();
        return false;
    }

    // Mark complete
    _contentLength = _bytesReceived;
    Serial.printf("[OTA_GSM] ✅ All %d bytes written to flash!\n", _bytesReceived);

    ssl.stop();
    restorePriority();
    return true;
}


// ---------------------------------------------------------------------------
// _stream() — Called every loop() tick in STREAMING state.
//             Downloads one range slice per call (blocking but yields internally).
// ---------------------------------------------------------------------------
void NativeGsmHttpDownload::_stream() {
    uint32_t now = millis();

    // Stall timeout (2 minutes of no data)
    if (now - _lastDataMs >= STALL_TIMEOUT_MS) {
        Serial.printf("[OTA_GSM] ERROR: Stall timeout (%lus). Aborting.\n",
                      (unsigned long)(now - _lastDataMs) / 1000);
        _finish(MO_FtpCloseReason_Failure);
        return;
    }

    // Check completion
    if (_contentLength > 0 && _bytesReceived >= _contentLength) {
        Serial.printf("[OTA_GSM] ✅ All %d bytes written to flash!\n", _bytesReceived);
        _finish(MO_FtpCloseReason_Success);
        return;
    }

    // Download the next range slice
    if (!_downloadRange()) {
        Serial.println("[OTA_GSM] ERROR: Range download failed");
        _finish(MO_FtpCloseReason_Failure);
        return;
    }

    if (_contentLength > 0) {
        Serial.printf("[OTA_GSM] Progress: %d / %d (%.1f%%)\n",
                      _bytesReceived, _contentLength,
                      100.0f * _bytesReceived / _contentLength);
    } else {
        Serial.printf("[OTA_GSM] Progress: %d bytes...\n", _bytesReceived);
    }
}

// ---------------------------------------------------------------------------
// loop() — called every mocpp_loop() tick
// ---------------------------------------------------------------------------
void NativeGsmHttpDownload::loop() {
    if (!_active) return;

    switch (_state) {
        case State::SETUP:
            if (_setup()) {
                _state = State::STREAMING;
            } else {
                Serial.println("[OTA_GSM] ERROR: Setup failed");
                _finish(MO_FtpCloseReason_Failure);
            }
            break;

        case State::STREAMING:
            _stream();
            break;

        case State::DONE:
            break;
    }
}
//
// =============================================================================
// HttpOtaClient — FtpClient dispatch (GSM primary, WiFi fallback)
// =============================================================================

std::unique_ptr<MicroOcpp::FtpDownload> HttpOtaClient::getFile(
    const char* url,
    std::function<size_t(unsigned char*, size_t)> writer,
    std::function<void(MO_FtpCloseReason)>        onClose,
    const char* /*ca_cert*/)
{
    // STAGE 1 FAILSAFE: Prevent Download During Active Charging
    if (SystemState::instance().getTransactionActive()) {
        Serial.println("[OTA_HTTP] ❌ OTA REJECTED: A charging session is currently active. Safety lock engaged.");
        if (onClose) onClose(MO_FtpCloseReason_Failure);
        return nullptr;
    }

    // ── STRATEGY 1: GSM Primary ───────────────────────────────────────────────
    // With RTS/CTS hardware flow control + 460800 baud, GSM OTA now completes
    // 1.4MB in ~90 seconds — faster and simpler than waking up the WiFi radio.
    // NativeGsmHttpDownload manages its own socket lifecycle internally.
    if (g_gsmManager.isConnected()) {
        Serial.println("[OTA_HTTP] ✅ GSM connected — using GSM transport (Primary)");
        return std::unique_ptr<NativeGsmHttpDownload>(
            new NativeGsmHttpDownload(url, writer, onClose, _caCert));
    }

    // ── STRATEGY 2: WiFi Fallback ─────────────────────────────────────────────
    // GSM is offline (device running entirely on WiFi fallback, or modem failure).
    // Attempt to bring up the WiFi radio specifically for this OTA download.
    Serial.println("[OTA_HTTP] ⚠️  GSM offline — attempting WiFi fallback for OTA...");

    auto wrappedOnClose = [onClose](MO_FtpCloseReason reason) {
        g_networkManager.releaseWiFiAfterOta();
        if (onClose) onClose(reason);
    };

    if (g_networkManager.requestWiFiForOta()) {
        Serial.println("[OTA_HTTP] ✅ WiFi fallback ready — using WiFi transport");

        // CRITICAL: ESP32 does not have enough RAM to run two software TLS stacks
        // simultaneously (WiFiClientSecure for OTA + SSLClient for GSM WebSocket).
        // Tear down the GSM WebSocket to free ~25KB before WiFi OTA starts.
        if (g_unifiedConnectionPtr) {
            g_unifiedConnectionPtr->teardownGsmWebSocket();
        }

        return std::unique_ptr<WifiHttpDownload>(
            new WifiHttpDownload(url, writer, wrappedOnClose, _caCert));
    }

    // ── No transport available ─────────────────────────────────────────────────
    Serial.println("[OTA_HTTP] ❌ No transport available (GSM offline, WiFi failed) — OTA aborted");
    if (onClose) onClose(MO_FtpCloseReason_Failure);
    return nullptr;
}

} // namespace prod

