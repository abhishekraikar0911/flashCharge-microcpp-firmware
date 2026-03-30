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

#include "system/NetworkManager.h"
#include "system/GsmManager.h"
#include "system/WifiManager.h"
#include "config/hardware.h"
#include "system/HealthMonitor.h"
#include "system/SystemState.h"
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

        case NetworkState::GSM_CONNECTING: {
            GsmError err = attemptGSM();
            bool charging = SystemState::instance().snapshot().transactionActive;

            if (err == GsmError::SUCCESS) {
                _state = NetworkState::GSM_CONNECTED;
                _activeConnection = ConnectionType::GSM;
                _gsmRetryCount = 0;
                _lastActivityTime = millis();  // FIX: Reset idle watchdog — WS handshake needs time
                syncNTP();
                // FIX C: Kill WiFi radio when GSM is stable to prevent Ghost WiFi
                if (WiFi.isConnected()) {
                    WiFi.disconnect(true);
                    Serial.println("[NET] 📵 WiFi disabled — GSM is primary (radio killed)");
                }
                Serial.println("[NET] ✅ Connected via GSM (Primary)");
            } else {
                _gsmRetryCount++;
                
                // Industrial Context-Aware Failover
                int maxRetries = charging ? GSM_CHARGING_MAX_RETRIES : GSM_MAX_RETRIES;

                Serial.printf("[NET] ❌ GSM attempt %d/%d failed (Err=%d, Charging=%d)\n", 
                              _gsmRetryCount, maxRetries, (int)err, charging);

                // RAPID FAILOVER: If SIM is missing during charge, don't even retry. Switch to WiFi NOW.
                bool fatalSimError = (err == GsmError::FAIL_FATAL_NO_SIM);
                if ((fatalSimError && charging) || (_gsmRetryCount >= maxRetries)) {
                    if (fatalSimError && charging) {
                        Serial.println("[NET] 🚨 FATAL: SIM missing during Charge — Instant WiFi Fallback!");
                    } else if (charging) {
                        Serial.println("[NET] 🚀 Industrial Failover: Fast-tracking WiFi switch due to active transaction");
                    } else {
                        Serial.println("[NET] 🔄 GSM failed — falling back to WiFi...");
                    }
                    _state = NetworkState::GSM_FAILED;
                }
            }
            break;
        }

        case NetworkState::GSM_FAILED:
            // Fall through to WiFi
            _state = NetworkState::WIFI_CONNECTING;
            break;

        case NetworkState::WIFI_CONNECTING:
            // M3 FIX: Exponential backoff — don't attempt WiFi until backoff window expires
            if ((int32_t)(now - _wifiNextAttempt) < 0) {
                Serial.printf("[NET] \xe2\x8f\xb3 WiFi backoff: waiting %u ms before next attempt\n",
                              (unsigned)(_wifiNextAttempt - now));
                break;
            }
            if (attemptWiFi()) {
                _state = NetworkState::WIFI_CONNECTED;
                _activeConnection = ConnectionType::WIFI;
                _lastActivityTime = millis();
                _wifiBackoffMs = 2000;   // Reset backoff on success
                _wifiNextAttempt = 0;
                Serial.println("[NET] \xe2\x9c\x85 Connected via WiFi (Fallback)");
            } else {
                // Exponential backoff: double up to 60s cap
                _wifiBackoffMs = (_wifiBackoffMs < 60000) ? (_wifiBackoffMs * 2) : 60000;
                _wifiNextAttempt = millis() + _wifiBackoffMs;
                Serial.printf("[NET] \xe2\x9d\x8c WiFi failed \xe2\x80\x94 next attempt in %u s (retrying GSM)\n",
                              (unsigned)(_wifiBackoffMs / 1000));
                _gsmRetryCount = 0;
                _state = NetworkState::GSM_CONNECTING;
            }
            break;

        case NetworkState::GSM_CONNECTED:
            // Monitor GSM health
            g_gsmManager.poll();

            if (!g_gsmManager.isConnected()) {
                bool charging = SystemState::instance().snapshot().transactionActive;
                Serial.printf("[NET] ⚠️  GSM connection lost! (Charging=%d)\n", charging);
                _activeConnection = ConnectionType::NONE;
                _gsmRetryCount = 0;
                
                // RAPID FALLBACK: Skip 5s wait if we are actively charging
                if (!charging) {
                    Serial.println("[NET] ⏳ Waiting 5s before recovery...");
                    vTaskDelay(pdMS_TO_TICKS(5000));
                } else {
                    Serial.println("[NET] ⚡ Charging active — Skipping recovery delay");
                }
                
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
            if (attemptGSM() == GsmError::SUCCESS) {
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
    now = millis();
    if (isConnected() && SystemState::instance().getOcppInitialized() && now - _lastActivityTime >= GSM_WS_IDLE_TIMEOUT_MS) {
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

GsmError NetworkManager::attemptGSM() {
    Serial.println("[NET] 📡 Attempting GSM connection...");

    // FIX B: Forcibly drop any stale GPRS/PDP session before connecting.
    if (g_gsmManager.isConnected()) {
        Serial.println("[NET] 🧹 Cleaning stale GPRS session before reconnect...");
        g_gsmManager.disconnect();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // If modem is in ERROR state, try recovery first
    if (g_gsmManager.getState() == GSMState::ERROR) {
        // RAPID FAILOVER: Check if SIM is physically missing to avoid 20s soft-reset penalty
        if (g_gsmManager.getModem().testAT(1000)) {
            if (g_gsmManager.getModem().getSimStatus() != SIM_READY) {
                Serial.println("[NET] 🚨 FATAL: SIM is missing. Bypassing hardware recovery.");
                return GsmError::FAIL_FATAL_NO_SIM;
            }
        }

        Serial.println("[NET] GSM in error state, attempting recovery...");

        // Tiered recovery
        if (!g_gsmManager.softReset()) {
            g_gsmManager.hardReset();
        }
    }

    // Industrial Context-Aware Timeout
    bool charging = SystemState::instance().snapshot().transactionActive;
    uint32_t timeout = charging ? GSM_CHARGING_CONNECT_TIMEOUT_MS : GSM_CONNECT_TIMEOUT_MS;

    return g_gsmManager.connect(timeout);
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
        vTaskDelay(pdMS_TO_TICKS(500));  // H1 FIX: Never block scheduler; CAN/safety tasks must keep running
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
    // FIX D: Allow 2s for OCPP message queue to drain stale responses
    // before we tear down and rebuild the connection. Without this, old
    // Heartbeat responses arrive after reconnect and corrupt the queue state.
    Serial.println("[NET] ⏳ 2s OCPP queue drain...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    _activeConnection = ConnectionType::NONE;
    _gsmRetryCount = 0;
    _lastActivityTime = millis();  // FIX 4: Reset idle watchdog to prevent re-firing during reconnect
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
