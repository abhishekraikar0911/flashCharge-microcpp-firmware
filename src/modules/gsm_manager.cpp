/**
 * @file gsm_manager.cpp
 * @brief GSM A7670C modem manager implementation
 * 
 * Drives the SIM A7670C through its lifecycle using TinyGSM.
 * Provides hardware reset, soft reset, signal quality monitoring,
 * and a TCP client for higher-level WebSocket/OCPP use.
 * 
 * @author Rivot Motors
 * @date 2026
 */

#include "../../include/modules/gsm_manager.h"
#include "../../include/config/hardware.h"
#include "../../include/health_monitor.h"
#include <Arduino.h>

namespace prod {

// ── Static storage for TinyGSM objects (placement new) ──
// TinyGsm and TinyGsmClient can't be statically constructed because
// they need a reference to Serial2 which isn't ready at global init time.
alignas(TinyGsm) uint8_t GSMManager::_modemBuf[sizeof(TinyGsm)];
alignas(TinyGsmClient) uint8_t GSMManager::_clientBuf[sizeof(TinyGsmClient)];

// ── Human-readable state names ──
const char* gsmStateToString(GSMState state) {
    switch (state) {
        case GSMState::MODEM_OFF:           return "MODEM_OFF";
        case GSMState::MODEM_BOOT:          return "MODEM_BOOT";
        case GSMState::MODEM_READY:         return "MODEM_READY";
        case GSMState::SIM_READY:           return "SIM_READY";
        case GSMState::NETWORK_REGISTERED:  return "NETWORK_REGISTERED";
        case GSMState::DATA_ATTACHED:       return "DATA_ATTACHED";
        case GSMState::IP_READY:            return "IP_READY";
        case GSMState::CONNECTED:           return "CONNECTED";
        case GSMState::ERROR:               return "ERROR";
        default:                            return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════
//  INIT
// ═══════════════════════════════════════════════════════════
void GSMManager::init() {
    if (_initialized) return;

    Serial.println("[GSM] 📡 Initializing GSM Manager...");

    // Configure RESET pin (Active HIGH)
    pinMode(GSM_RESET_PIN, OUTPUT);
    digitalWrite(GSM_RESET_PIN, LOW);  // Explicitly LOW
    delay(10);
    digitalWrite(GSM_RESET_PIN, LOW);  // Double ensure
    Serial.printf("[GSM] RESET pin (GPIO %d) initialized LOW\n", GSM_RESET_PIN);

    // Initialize UART2 for modem communication
    GSM_SERIAL.begin(GSM_BAUD_RATE, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    delay(100);

    // Construct TinyGSM objects using placement new
    new (_modemBuf) TinyGsm(GSM_SERIAL);
    new (_clientBuf) TinyGsmClient(_modem);

    _initialized = true;
    _state = GSMState::MODEM_OFF;

    Serial.printf("[GSM] ✅ GSM Manager initialized (TX=%d, RX=%d, RST=%d, Baud=%d)\n",
                  GSM_TX_PIN, GSM_RX_PIN, GSM_RESET_PIN, GSM_BAUD_RATE);
}

// ═══════════════════════════════════════════════════════════
//  CONNECT (Full State Machine)
// ═══════════════════════════════════════════════════════════
bool GSMManager::connect() {
    if (!_initialized) {
        Serial.println("[GSM] ❌ Not initialized. Call init() first.");
        return false;
    }

    Serial.println("[GSM] 🔄 Starting modem connection sequence...");

    // CRITICAL FIX: If we were previously connected or in error,
    // tear down old GPRS session to ensure clean PDP context.
    if (_state >= GSMState::DATA_ATTACHED || _state == GSMState::ERROR) {
        Serial.println("[GSM] 🧹 Cleaning up old GPRS session before reconnect...");
        _modem.gprsDisconnect();
        delay(1000); // Allow modem to settle
    }

    // Step through each state sequentially
    if (!stepBoot()) return false;
    if (!stepModemReady()) return false;
    if (!stepSimReady()) return false;
    if (!stepNetworkRegistered()) return false;
    if (!stepDataAttached()) return false;
    if (!stepIpReady()) return false;

    setState(GSMState::CONNECTED);
    Serial.println("[GSM] ✅ Modem fully connected and ready for TCP/WebSocket!");

    return true;
}

// ═══════════════════════════════════════════════════════════
//  STATE MACHINE STEPS
// ═══════════════════════════════════════════════════════════

bool GSMManager::stepBoot() {
    setState(GSMState::MODEM_BOOT);
    Serial.printf("[GSM] 🔌 Step 1/6: Booting modem... (Heap: %u)\n", ESP.getFreeHeap());

    // Strategy: Try AT first. If it responds, it's already on.
    // If not, try a short hardware pulse.
    if (waitForAT(2000)) {
        Serial.println("[GSM] ✅ Modem already powered and responding");
        return true;
    }

    Serial.println("[GSM] 🔄 Modem silent, performing hardware RESET...");
    hardReset();

    // Wait for modem to respond to AT (Increased to 30s)
    if (!waitForAT(30000)) {
        Serial.println("[GSM] ❌ Modem did not respond after hard reset");
        setState(GSMState::ERROR);
        return false;
    }

    Serial.println("[GSM] ✅ Modem responded to AT commands");
    return true;
}

bool GSMManager::stepModemReady() {
    setState(GSMState::MODEM_READY);
    Serial.println("[GSM] 🔧 Step 2/6: Configuring modem...");

    // Initialize modem (sets up echo, error reporting, etc.)
    if (!_modem.init()) {
        Serial.println("[GSM] ⚠️  modem.init() failed, trying restart...");
        if (!_modem.restart()) {
            Serial.println("[GSM] ❌ Modem restart failed");
            setState(GSMState::ERROR);
            return false;
        }
    }

    // Disable echo for cleaner AT parsing
    _modem.sendAT("+ATE0");
    _modem.waitResponse();

    // Get modem info for diagnostics
    String modemName = _modem.getModemName();
    String modemInfo = _modem.getModemInfo();
    Serial.printf("[GSM] 📋 Modem: %s\n", modemName.c_str());
    Serial.printf("[GSM] 📋 Info:  %s\n", modemInfo.c_str());

    Serial.println("[GSM] ✅ Modem configured");
    return true;
}

bool GSMManager::stepSimReady() {
    setState(GSMState::SIM_READY);
    Serial.println("[GSM] 💳 Step 3/6: Checking SIM card...");

    // Check SIM status
    SimStatus simStatus = _modem.getSimStatus();
    if (simStatus != SIM_READY) {
        Serial.printf("[GSM] ❌ SIM not ready (status=%d)\n", simStatus);
        Serial.println("[GSM]   Check: SIM inserted? SIM locked with PIN?");
        setState(GSMState::ERROR);
        return false;
    }

    // Read IMSI for diagnostics
    String imsi = _modem.getIMSI();
    Serial.printf("[GSM] 📋 IMSI: %s\n", imsi.c_str());

    Serial.println("[GSM] ✅ SIM card ready");
    return true;
}

bool GSMManager::stepNetworkRegistered() {
    setState(GSMState::NETWORK_REGISTERED);
    Serial.println("[GSM] 📶 Step 4/6: Waiting for network registration...");

    // Wait for network registration (timeout from config)
    uint32_t startTime = millis();
    bool registered = false;

    while (millis() - startTime < GSM_CONNECT_TIMEOUT_MS) {
        // g_healthMonitor.feed(); // Feed watchdog during long registration wait - DISABLED
        RegStatus regStatus = _modem.getRegistrationStatus();
        if (regStatus == REG_OK_HOME || regStatus == REG_OK_ROAMING) {
            registered = true;
            break;
        }
        Serial.printf("[GSM]   Registration status: %d (waiting...)\n", regStatus);
        delay(2000);
    }

    if (!registered) {
        Serial.println("[GSM] ❌ Network registration timeout");
        setState(GSMState::ERROR);
        return false;
    }

    // Get operator info
    String op = _modem.getOperator();
    strncpy(_operatorName, op.c_str(), sizeof(_operatorName) - 1);
    Serial.printf("[GSM] 📋 Operator: %s\n", _operatorName);

    // Get signal quality
    _lastCSQ = _modem.getSignalQuality();
    Serial.printf("[GSM] 📶 Signal: CSQ=%d (%s)\n", _lastCSQ,
                  _lastCSQ < 10 ? "Poor" : _lastCSQ < 15 ? "Fair" : _lastCSQ < 20 ? "Good" : "Excellent");

    Serial.println("[GSM] ✅ Network registered");
    return true;
}

bool GSMManager::stepDataAttached() {
    setState(GSMState::DATA_ATTACHED);
    Serial.println("[GSM] 🌐 Step 5/6: Attaching GPRS/LTE data...");

    // Connect to GPRS/LTE data with APN
    if (!_modem.gprsConnect(GSM_APN, GSM_APN_USER, GSM_APN_PASS)) {
        Serial.printf("[GSM] ❌ GPRS connect failed (APN: %s)\n", GSM_APN);
        setState(GSMState::ERROR);
        return false;
    }

    Serial.printf("[GSM] ✅ Data attached (APN: %s)\n", GSM_APN);
    return true;
}

bool GSMManager::stepIpReady() {
    setState(GSMState::IP_READY);
    Serial.println("[GSM] 🔢 Step 6/6: Getting IP address...");

    // Get local IP
    String ip = _modem.getLocalIP();
    if (ip.length() == 0 || ip == "0.0.0.0") {
        Serial.println("[GSM] ❌ No IP address assigned");
        setState(GSMState::ERROR);
        return false;
    }

    strncpy(_localIP, ip.c_str(), sizeof(_localIP) - 1);
    Serial.printf("[GSM] ✅ IP assigned: %s\n", _localIP);

    return true;
}

// ═══════════════════════════════════════════════════════════
//  DISCONNECT
// ═══════════════════════════════════════════════════════════
void GSMManager::disconnect() {
    Serial.println("[GSM] 🔌 Disconnecting modem...");

    if (_initialized && _state >= GSMState::DATA_ATTACHED) {
        _modem.gprsDisconnect();
    }

    setState(GSMState::MODEM_OFF);
    Serial.println("[GSM] ✅ Modem disconnected");
}

// ═══════════════════════════════════════════════════════════
//  POLL (Health Monitoring)
// ═══════════════════════════════════════════════════════════
void GSMManager::poll() {
    if (!_initialized || _state < GSMState::CONNECTED) return;

    uint32_t now = millis();

    // ── AT Watchdog: Check modem is still alive ──
    if (now - _lastATCheck >= GSM_CIPSTATUS_INTERVAL) {
        _lastATCheck = now;

        if (!_modem.testAT()) {
            _missedHeartbeats++;
            Serial.printf("[GSM] ⚠️  AT watchdog: Modem not responding! (Missed: %d/3)\n", _missedHeartbeats);
            
            if (_missedHeartbeats >= 3) {
                Serial.println("[GSM] ❌ CRITICAL: 3 AT heartbeats missed. Forcing HARD RESET.");
                _missedHeartbeats = 0;
                hardReset();
                setState(GSMState::ERROR); // network_manager will catch this and try reconnect
            }
            return;
        }
        
        // Reset heartbeat counter on success
        _missedHeartbeats = 0;

        // Check if still registered
        if (!_modem.isNetworkConnected()) {
            Serial.println("[GSM] ⚠️  Network connection lost!");
            setState(GSMState::ERROR);
            return;
        }

        // Check if GPRS still connected
        if (!_modem.isGprsConnected()) {
            Serial.println("[GSM] ⚠️  GPRS connection lost!");
            setState(GSMState::ERROR);
            return;
        }
    }

    // ── Signal Quality Check (every 60s) ──
    if (now - _lastSignalCheck >= 60000) {
        _lastSignalCheck = now;
        _lastCSQ = _modem.getSignalQuality();
        float voltage = getSupplyVoltage();

        if (_lastCSQ < 10 && _lastCSQ != 99) {
            Serial.printf("[GSM] ⚠️  Weak signal: CSQ=%d | Voltage: %.2fV\n", _lastCSQ, voltage);
        }

        if (voltage > 0.1f && voltage < 3.4f) {
            Serial.printf("[GSM] ❌ CRITICAL: Low supply voltage: %.2fV (Modem needs 3.4V-4.4V)\n", voltage);
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  RESET FUNCTIONS
// ═══════════════════════════════════════════════════════════

void GSMManager::hardReset() {
    // A7670C Power-ON/RESET: Active HIGH
    // For many A7670C breakout boards, a 100ms-500ms pulse on RESET triggers power-up.
    // 2.5s might be triggering a "Force Shutdown" or brownout.
    Serial.println("[GSM] 🔄 Sending Power-ON pulse (400ms)...");

    digitalWrite(GSM_RESET_PIN, HIGH);
    delay(400); 
    digitalWrite(GSM_RESET_PIN, LOW);
    
    // g_healthMonitor.feed(); // DISABLED

    // Wait for power stabilization and boot
    Serial.println("[GSM] ⏳ Waiting 10s for modem stabilization...");
    for (int i=0; i<10; i++) {
        delay(1000);
        // g_healthMonitor.feed(); // DISABLED
    }
}

bool GSMManager::softReset() {
    Serial.println("[GSM] 🔄 Soft reset via AT command...");

    _modem.sendAT("+CRESET");
    int res = _modem.waitResponse(10000);

    if (res != 1) {
        Serial.println("[GSM] ⚠️  Soft reset command failed");
        return false;
    }

    delay(5000);

    if (!waitForAT(15000)) {
        Serial.println("[GSM] ❌ Modem not responding after soft reset");
        return false;
    }

    Serial.println("[GSM] ✅ Soft reset successful");
    return true;
}

// ═══════════════════════════════════════════════════════════
//  DIAGNOSTICS
// ═══════════════════════════════════════════════════════════

int GSMManager::getSignalQuality() {
    if (!_initialized || _state < GSMState::MODEM_READY) return 99;
    _lastCSQ = _modem.getSignalQuality();
    return _lastCSQ;
}

const char* GSMManager::getOperator() {
    return _operatorName;
}

const char* GSMManager::getLocalIP() {
    return _localIP;
}

float GSMManager::getSupplyVoltage() {
    if (!_initialized || _state < GSMState::MODEM_READY) return 0.0f;

    // AT+CBC returns battery charge status (also works for supply voltage)
    _modem.sendAT("+CBC");
    String response;
    if (_modem.waitResponse(5000, response) == 1) {
        // Parse +CBC: <bcs>,<bcl>,<voltage>
        int lastComma = response.lastIndexOf(',');
        if (lastComma > 0) {
            float mv = response.substring(lastComma + 1).toFloat();
            return mv / 1000.0f;  // Convert mV to V
        }
    }
    return 0.0f;
}

// ═══════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════

bool GSMManager::waitForAT(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        // g_healthMonitor.feed(); // Feed watchdog during AT wait - DISABLED
        if (_modem.testAT()) {
            return true;
        }
        delay(200); // Check more frequently
        // g_healthMonitor.feed(); // DISABLED
    }
    return false;
}

void GSMManager::setState(GSMState newState) {
    if (_state != newState) {
        Serial.printf("[GSM] State: %s → %s\n", gsmStateToString(_state), gsmStateToString(newState));
        _state = newState;
    }
}

// ── Global Instance ──
GSMManager g_gsmManager;

} // namespace prod
