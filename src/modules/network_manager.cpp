/**
 * @file network_manager.cpp
 * @brief Network connection manager — GSM primary, WiFi fallback
 * 
 * State machine:
 *   IDLE → GSM_CONNECTING → GSM_CONNECTED
 *                         ↘ GSM_FAILED → WIFI_CONNECTING → WIFI_CONNECTED
 *                                                         ↗ GSM_RECHECK (periodic)
 * 
 * @author Rivot Motors
 * @date 2026
 */

#include "../../include/modules/network_manager.h"
#include "../../include/modules/gsm_manager.h"
#include "../../include/wifi_manager.h"
#include "../../include/config/hardware.h"
#include "../../include/health_monitor.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

namespace prod {

// ── Human-readable names ──
const char* networkStateToString(NetworkState state) {
    switch (state) {
        case NetworkState::IDLE:            return "IDLE";
        case NetworkState::GSM_CONNECTING:  return "GSM_CONNECTING";
        case NetworkState::GSM_CONNECTED:   return "GSM_CONNECTED";
        case NetworkState::GSM_FAILED:      return "GSM_FAILED";
        case NetworkState::WIFI_CONNECTING: return "WIFI_CONNECTING";
        case NetworkState::WIFI_CONNECTED:  return "WIFI_CONNECTED";
        case NetworkState::GSM_RECHECK:     return "GSM_RECHECK";
        default:                            return "UNKNOWN";
    }
}

const char* connectionTypeToString(ConnectionType type) {
    switch (type) {
        case ConnectionType::NONE: return "NONE";
        case ConnectionType::GSM:  return "GSM";
        case ConnectionType::WIFI: return "WiFi";
        default:                   return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════
//  INIT
// ═══════════════════════════════════════════════════════════
void NetworkManager::init() {
    if (_initialized) return;

    Serial.println("[NET] 🌐 Initializing Network Manager...");
    Serial.println("[NET]    Strategy: GSM Primary → WiFi Fallback");

    // Initialize GSM subsystem
    g_gsmManager.init();

    // WiFi is already initialized from g_wifiManager.begin() in main.cpp
    // We will control when WiFi actually connects

    _state = NetworkState::IDLE;
    _activeConnection = ConnectionType::NONE;
    _gsmRetryCount = 0;
    _lastActivityTime = millis();
    _initialized = true;

    Serial.println("[NET] ✅ Network Manager ready");
}

// ═══════════════════════════════════════════════════════════
//  POLL (Main State Machine)
// ═══════════════════════════════════════════════════════════
void NetworkManager::poll() {
    if (!_initialized) return;

    uint32_t now = millis();

    switch (_state) {
        case NetworkState::IDLE:
            // Start connection sequence — GSM first
            _state = NetworkState::GSM_CONNECTING;
            _gsmRetryCount = 0;
            Serial.println("[NET] 🚀 Starting connection: GSM first...");
            break;

        case NetworkState::GSM_CONNECTING:
            if (attemptGSM()) {
                _state = NetworkState::GSM_CONNECTED;
                _activeConnection = ConnectionType::GSM;
                _gsmRetryCount = 0;
                syncNTP();
                Serial.println("[NET] ✅ Connected via GSM (Primary)");
            } else {
                _gsmRetryCount++;
                Serial.printf("[NET] ❌ GSM attempt %d/%d failed\n", _gsmRetryCount, GSM_MAX_RETRIES);

                if (_gsmRetryCount >= GSM_MAX_RETRIES) {
                    Serial.println("[NET] 🔄 GSM failed — falling back to WiFi...");
                    _state = NetworkState::GSM_FAILED;
                }
            }
            break;

        case NetworkState::GSM_FAILED:
            // Fall through to WiFi
            _state = NetworkState::WIFI_CONNECTING;
            break;

        case NetworkState::WIFI_CONNECTING:
            if (attemptWiFi()) {
                _state = NetworkState::WIFI_CONNECTED;
                _activeConnection = ConnectionType::WIFI;
                Serial.println("[NET] ✅ Connected via WiFi (Fallback)");
            } else {
                // WiFi also failed — retry GSM
                Serial.println("[NET] ❌ WiFi also failed — retrying GSM...");
                _gsmRetryCount = 0;
                _state = NetworkState::GSM_CONNECTING;
            }
            break;

        case NetworkState::GSM_CONNECTED:
            // Monitor GSM health
            g_gsmManager.poll();

            if (!g_gsmManager.isConnected()) {
                Serial.println("[NET] ⚠️  GSM connection lost! Waiting 5s before recovery...");
                _activeConnection = ConnectionType::NONE;
                _gsmRetryCount = 0;
                // CRITICAL FIX: Allow modem to settle before reconnect
                vTaskDelay(pdMS_TO_TICKS(5000));
                _state = NetworkState::GSM_CONNECTING;
            }
            break;

        case NetworkState::WIFI_CONNECTED:
            // Monitor WiFi health
            g_wifiManager.poll();

            if (!g_wifiManager.isConnected()) {
                Serial.println("[NET] ⚠️  WiFi connection lost! Retrying GSM first...");
                _activeConnection = ConnectionType::NONE;
                _gsmRetryCount = 0;
                _state = NetworkState::GSM_CONNECTING;
            }

            // Periodically try to revert to GSM
            checkGSMRevert();
            break;

        case NetworkState::GSM_RECHECK:
            // Try GSM while WiFi is still active
            Serial.println("[NET] 🔃 Rechecking GSM availability...");
            if (attemptGSM()) {
                // GSM is back! Disconnect WiFi, switch to GSM
                Serial.println("[NET] 🌟 GSM recovered! Switching from WiFi to GSM...");
                WiFi.disconnect(true);
                _state = NetworkState::GSM_CONNECTED;
                _activeConnection = ConnectionType::GSM;
                syncNTP();
            } else {
                // GSM still not available, stay on WiFi
                Serial.println("[NET] GSM still unavailable, staying on WiFi");
                _lastGSMRecheckTime = now;
                _state = NetworkState::WIFI_CONNECTED;
            }
            break;
    }

    // ── WebSocket Idle Watchdog (Level 3) ──
    // g_healthMonitor.feed(); // Feed watchdog during every poll - DISABLED for testing
    if (isConnected() && now - _lastActivityTime >= GSM_WS_IDLE_TIMEOUT_MS) {
        Serial.printf("[NET] ⚠️  WebSocket Idle Watchdog: No activity for %d s! Reconnecting...\n",
                      (int)((now - _lastActivityTime) / 1000));
        _lastActivityTime = now; // Reset to avoid constant trigger during reconnect
        reconnect();
    }

    // ── Periodic Status Log ──
    if (now - _lastStatusLog >= 30000) {
        _lastStatusLog = now;
        printStatus();
    }
}

// ═══════════════════════════════════════════════════════════
//  CONNECTION ATTEMPTS
// ═══════════════════════════════════════════════════════════

bool NetworkManager::attemptGSM() {
    Serial.println("[NET] 📡 Attempting GSM connection...");

    // If modem is in ERROR state, try recovery first
    if (g_gsmManager.getState() == GSMState::ERROR) {
        Serial.println("[NET] GSM in error state, attempting recovery...");

        // Tiered recovery
        if (!g_gsmManager.softReset()) {
            g_gsmManager.hardReset();
        }
    }

    return g_gsmManager.connect();
}

bool NetworkManager::attemptWiFi() {
    Serial.println("[NET] 📶 Attempting WiFi connection...");

    // Disconnect GSM to free UART resources and avoid interference
    g_gsmManager.disconnect();

    // WiFiManager handles multi-SSID priority internally
    if (!g_wifiManager.isConnected()) {
        g_wifiManager.begin(nullptr, nullptr);
        WiFi.setSleep(false);
    }

    // Wait for connection (with timeout)
    uint32_t start = millis();
    while (!g_wifiManager.isConnected() && millis() - start < 30000) {
        delay(500);
    }

    if (g_wifiManager.isConnected()) {
        // WiFiManager handles NTP sync internally in begin()
        _timeSynced = true;
        return true;
    }

    return false;
}

// ═══════════════════════════════════════════════════════════
//  GSM REVERT CHECK
// ═══════════════════════════════════════════════════════════
void NetworkManager::checkGSMRevert() {
    uint32_t now = millis();

    if (now - _lastGSMRecheckTime >= GSM_RECHECK_INTERVAL_MS) {
        _lastGSMRecheckTime = now;
        _state = NetworkState::GSM_RECHECK;
    }
}

// ═══════════════════════════════════════════════════════════
//  NTP SYNC (For TLS Certificate Validation)
// ═══════════════════════════════════════════════════════════
void NetworkManager::syncNTP() {
    Serial.println("[NTP] 🕐 Syncing time via GSM modem...");

    // Use TinyGSM modem's network time
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    float tz = 0;

    if (g_gsmManager.getModem().getNetworkTime(&year, &month, &day, &hour, &min, &sec, &tz)) {
        // Set system time from modem
        struct tm tm;
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = min;
        tm.tm_sec = sec;
        tm.tm_isdst = 0;

        time_t t = mktime(&tm);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, nullptr);

        _timeSynced = true;

        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tm);
        Serial.printf("[NTP] ✅ Time synced via GSM: %s (TZ offset: %.1f)\n", timeStr, tz);
    } else {
        Serial.println("[NTP] ⚠️  Failed to get time from GSM modem");
        // Fallback: try NTP over GSM data connection
        configTime(19800, 0, "pool.ntp.org", "time.google.com");

        uint32_t start = millis();
        time_t now = time(nullptr);
        while (now < 1000000000L && millis() - start < 10000) {
            delay(250);
            now = time(nullptr);
        }

        if (now > 1000000000L) {
            _timeSynced = true;
            Serial.println("[NTP] ✅ Time synced via NTP over GSM");
        } else {
            Serial.println("[NTP] ❌ NTP sync failed — TLS certificates may be rejected");
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  UNIFIED STATUS
// ═══════════════════════════════════════════════════════════
void NetworkManager::notifyActivity() {
    _lastActivityTime = millis();
}

bool NetworkManager::isConnected() const {
    switch (_activeConnection) {
        case ConnectionType::GSM:
            return g_gsmManager.isConnected();
        case ConnectionType::WIFI:
            return g_wifiManager.isConnected();
        default:
            return false;
    }
}

void NetworkManager::reconnect() {
    Serial.println("[NET] 🔄 Manual reconnect requested");
    _activeConnection = ConnectionType::NONE;
    _gsmRetryCount = 0;
    _state = NetworkState::GSM_CONNECTING;
}

void NetworkManager::printStatus() {
    Serial.printf("[NET] 📊 Network: %s via %s | State: %s",
                  isConnected() ? "ONLINE" : "OFFLINE",
                  connectionTypeToString(_activeConnection),
                  networkStateToString(_state));

    if (_activeConnection == ConnectionType::GSM) {
        Serial.printf(" | CSQ=%d | IP=%s",
                      g_gsmManager.getSignalQuality(),
                      g_gsmManager.getLocalIP());
    } else if (_activeConnection == ConnectionType::WIFI) {
        Serial.printf(" | RSSI=%d | SSID=%s",
                      WiFi.RSSI(),
                      WiFi.SSID().c_str());
    }

    Serial.println();
}

// ── Global Instance ──
NetworkManager g_networkManager;

} // namespace prod
