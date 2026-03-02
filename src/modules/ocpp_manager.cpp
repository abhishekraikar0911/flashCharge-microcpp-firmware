// OCPP Manager: All OCPP-related logic isolated for easy debugging
#include <Arduino.h>
#include <WiFi.h>
#include <MicroOcpp.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Model/Transactions/Transaction.h>
#include "../../include/config/hardware.h"

#include "../../include/ocpp/ocpp_client.h"
#include "../../include/production_config.h"
#include "../../include/secrets.h"
#include "../../include/header.h"
#include "../../include/modules/ota_manager.h"
#include "../../include/ocpp_state_machine.h"
#include <MicroOcpp/Core/Context.h>
#include <MicroOcpp/Model/Model.h>
#include <MicroOcpp/Model/Boot/BootService.h>
#include <MicroOcpp/Model/FirmwareManagement/FirmwareService.h>
#include <freertos/semphr.h>

// External globals from main firmware
extern bool gunPhysicallyConnected;
extern bool chargingEnabled;
extern float energyWh;
extern float terminalVolt;  // FIXED: Use terminal values (real measurements)
extern float terminalCurr;  // FIXED: Use terminal values (real measurements)
extern bool batteryConnected;
extern float batteryAh;
extern float BMS_Imax;
extern float chargerTemp;
extern float socPercent;    // SOC percentage
extern float rangeKm;       // Range in km
extern uint8_t vehicleModel;    // Vehicle model (1=Classic, 2=Pro, 3=Max)

// Charger health check
extern bool isChargerModuleHealthy();

using namespace prod;

namespace
{
    static SemaphoreHandle_t ocppMutex = nullptr;

    static void ensureOcppMutex()
    {
        if (ocppMutex == nullptr)
        {
            ocppMutex = xSemaphoreCreateRecursiveMutex();
            if (ocppMutex == nullptr)
            {
                Serial.println("[OCPP] ❌ Failed to create OCPP mutex");
            }
        }
    }
}

bool ocpp::lock(uint32_t timeout_ms)
{
    ensureOcppMutex();
    if (ocppMutex == nullptr)
    {
        return false;
    }
    return xSemaphoreTakeRecursive(ocppMutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void ocpp::unlock()
{
    if (ocppMutex != nullptr)
    {
        xSemaphoreGiveRecursive(ocppMutex);
    }
}

bool ocpp::beginTransactionSafe(const char *idTag, unsigned int connectorId)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return false;
    }
    return beginTransaction(idTag, connectorId);
}

bool ocpp::endTransactionSafe(const char *idTag, const char *reason, unsigned int connectorId)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return false;
    }
    return endTransaction(idTag, reason, connectorId);
}

bool ocpp::isTransactionActiveSafe(unsigned int connectorId)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return false;
    }
    return isTransactionActive(connectorId);
}

bool ocpp::isTransactionRunningSafe(unsigned int connectorId)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return false;
    }
    return isTransactionRunning(connectorId);
}

bool ocpp::ocppPermitsChargeSafe(unsigned int connectorId)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return false;
    }
    return ocppPermitsCharge(connectorId);
}

// Transaction tracking and lock
static bool transactionLocked = false;
static int localTransactionId = -1;
static unsigned long lastMeterValueSent = 0;
static unsigned long lastDataTransferSent = 0;
static bool firstMeterValueSent = false;

bool ocpp::init()
{
    Serial.println("[OCPP] 🔌 Initializing OCPP...");
    Serial.printf("[OCPP] 📍 StationId: %s\n", SECRET_CHARGER_ID);
    Serial.printf("[OCPP] 🌐 Base URL: %s\n", SECRET_CSMS_URL);
    Serial.printf("[OCPP] 🔗 Full Connection URL: %s/%s\n", SECRET_CSMS_URL, SECRET_CHARGER_ID);

    // Require WiFi before initializing
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[OCPP] ⚠️  WiFi not connected - init deferred");
        return false;
    }
    Serial.println("[OCPP] ✅ WiFi connected");

    // Test network connectivity to server BEFORE initializing WebSocket
    Serial.println("[OCPP] 🔍 Testing TCP connectivity to server...");
    Serial.printf("[OCPP] 🎯 Target: %s:%d\\n", SECRET_CSMS_HOST, SECRET_CSMS_PORT);
    
    WiFiClient testClient;
    bool serverReachable = testClient.connect(SECRET_CSMS_HOST, SECRET_CSMS_PORT, 5000);
    
    if (serverReachable) {
        Serial.println("[OCPP] ✅ TCP connection successful - server is reachable");
        testClient.stop();
    } else {
        Serial.println("[OCPP] ❌ FAILED: Cannot reach server");
        Serial.println("[OCPP] ⚠️  Possible causes:");
        Serial.println("[OCPP]    - Firewall blocking outbound connections");
        Serial.println("[OCPP]    - Server not listening on port 8080");
        Serial.println("[OCPP]    - Network routing issue");
        Serial.println("[OCPP] 🔄 Will retry WebSocket connection anyway...");
        // Don't return false - let MicroOCPP try anyway (it has retry logic)
    }

    // NOW initialize MicroOCPP FIRST
    Serial.println("[OCPP] 🚀 Calling mocpp_initialize()...");
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        mocpp_initialize(
            SECRET_CSMS_URL,
            SECRET_CHARGER_ID,
            SECRET_CHARGER_MODEL,
            SECRET_CHARGER_VENDOR);
        Serial.println("[OCPP] ✅ mocpp_initialize() completed");
    }

    // CRITICAL: Configure all inputs AFTER mocpp_initialize()
    Serial.println("[OCPP] 📋 Registering input callbacks...");
    
    // Energy meter with validation - ALWAYS return non-negative
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        setEnergyMeterInput([]() {
            int energyInt = 0;
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                // Ensure energyWh is never negative
                if (energyWh < 0.0f) {
                    energyWh = 0.0f;
                }
                // Return as integer Wh (OCPP expects Wh, not kWh)
                energyInt = (int)energyWh;
                if (energyInt < 0) energyInt = 0;  // Double-check
                xSemaphoreGive(dataMutex);
            }
            Serial.printf("[METER_DEBUG] Energy=%dWh\n", energyInt);
            return energyInt;
        });
    }
    Serial.println("[OCPP]   ✓ Energy meter registered");

    // Power meter using terminal values
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        setPowerMeterInput([]() {
            int power = 0;
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                if (terminalVolt >= 56.0f && terminalVolt <= 85.5f &&
                    terminalCurr >= 0.0f && terminalCurr <= 300.0f) {
                    power = (int)(terminalVolt * terminalCurr);
                }
                xSemaphoreGive(dataMutex);
            }
            Serial.printf("[METER_DEBUG] Power=%dW (V=%.1f I=%.1f)\n", power, terminalVolt, terminalCurr);
            return power;
        });
    }
    Serial.println("[OCPP]   ✓ Power meter registered");

    // Plug detection - only check physical gun connection
    // Battery connection is checked separately via setEvReadyInput()
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        setConnectorPluggedInput([]() {
            // CRITICAL: Only check physical gun, not battery
            // This allows RemoteStart even when vehicle not yet connected
            return gunPhysicallyConnected;
        });
    }
    Serial.println("[OCPP]   ✓ Plug detection registered (physical gun only)");

    // Error code mapping for Faulted status
    {
        OcppLock lock;
        if (lock.ok())
        {
            // Set error code for Connector 1 (EVSE) only. 
            // Connector 0 is the ChargePoint itself - usually stay Available if MCU is alive.
            // CRITICAL: Return nullptr (not "NoError") when healthy to prevent isFaulted flag
            addErrorCodeInput([]() -> const char* {
                if (!isChargerModuleHealthy()) {
                    return "OtherError"; // Standard fault code (previously ConnectorLockFailure)
                }
                return nullptr; // nullptr = no error, prevents isFaulted flag
            }, 1);
            Serial.println("[OCPP]   ✓ Error code mapping registered for Connector 1");
        }
    }

    // EVSE ready (charger module healthy)
    {
        OcppLock lock;
        if (lock.ok())
        {
            // Set readiness for Connector 1 only.
            setEvseReadyInput([]() {
                bool healthy = isChargerModuleHealthy();
                static bool lastHealthy = true;
                if (healthy != lastHealthy) {
                    Serial.printf("[OCPP]   EVSE ready: %s\n", healthy ? "YES" : "NO");
                    lastHealthy = healthy;
                }
                return healthy;
            }, 1);
            Serial.printf("[OCPP]   ✓ EVSE readiness registered for Connector 1\n");
        }
    }
    Serial.printf("[OCPP]   ✓ EVSE ready status (initial: %s)\n", 
                  isChargerModuleHealthy() ? "HEALTHY" : "OFFLINE");

    // EV ready to charge - ALLOW RemoteStart even without vehicle
    // Vehicle can connect AFTER RemoteStart is accepted
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        setEvReadyInput([]() {
            // CRITICAL FIX: Always return true to allow RemoteStart
            // Vehicle connection is checked separately via setConnectorPluggedInput()
            // This prevents blocking RemoteStart when vehicle not yet connected
            return true;
        });
    }
    Serial.println("[OCPP]   ✓ EV ready registered (always ready for RemoteStart)");

    // MeterValues - OCPP 1.6 standard measurands with validation and debug logging
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        
        // SOC - read from global with validation
        addMeterValueInput([]() -> float { 
            float soc = 0.0f;
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                soc = socPercent;
                xSemaphoreGive(dataMutex);
            }
            Serial.printf("[METER_DEBUG] SOC=%.1f%%\n", soc);
            return soc;
        }, "SoC", "Percent", nullptr, nullptr, 1);
        
        // Voltage - read from global with validation
        addMeterValueInput([]() -> float { 
            float volt = 0.0f;
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                volt = terminalVolt;
                xSemaphoreGive(dataMutex);
            }
            Serial.printf("[METER_DEBUG] Voltage=%.1fV\n", volt);
            return volt;
        }, "Voltage", "V", nullptr, nullptr, 1);
        
        // Current - read from global with validation
        addMeterValueInput([]() -> float { 
            float curr = 0.0f;
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                curr = terminalCurr;
                xSemaphoreGive(dataMutex);
            }
            Serial.printf("[METER_DEBUG] Current=%.1fA\n", curr);
            return curr;
        }, "Current.Import", "A", nullptr, nullptr, 1);
        
        // Max Current - read from global with validation
        addMeterValueInput([]() -> float { 
            float maxCurr = 0.0f;
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                maxCurr = BMS_Imax;
                xSemaphoreGive(dataMutex);
            }
            Serial.printf("[METER_DEBUG] MaxCurrent=%.1fA\n", maxCurr);
            return maxCurr;
        }, "Current.Offered", "A", nullptr, nullptr, 1);
        
        // Temperature - read from global with validation
        addMeterValueInput([]() -> float { 
            float temp = 0.0f;
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                temp = chargerTemp;
                xSemaphoreGive(dataMutex);
            }
            Serial.printf("[METER_DEBUG] Temperature=%.1f°C\n", temp);
            return temp;
        }, "Temperature", "Celsius", nullptr, nullptr, 1);
    }
    Serial.println("[OCPP]   ✓ MeterValues registered with mutex protection and debug logging");

    // Configure intervals - Reduce message frequency for production
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        // TEST: Set MeterValue interval to 5s for debugging
        if (auto config = MicroOcpp::getConfigurationPublic("MeterValueSampleInterval")) {
            config->setInt(5);  // 5s for testing (change to 60s for production)
            Serial.println("[OCPP]   ✓ MeterValues interval: 5s (testing mode)");
        }
        
        if (auto config = MicroOcpp::getConfigurationPublic("ClockAlignedDataInterval")) {
            config->setInt(0);  // Disable clock alignment for immediate samples
            Serial.println("[OCPP]   ✓ Clock alignment: disabled (immediate samples)");
        }

        // PRODUCTION: Reduce measurands to essential only
        if (auto config = MicroOcpp::getConfigurationPublic("MeterValuesSampledData")) {
            config->setString("Energy.Active.Import.Register,Voltage,Current.Import,SoC");
            Serial.println("[OCPP]   ✓ Measurands: Essential only (reduced network load)");
        }

        if (auto config = MicroOcpp::getConfigurationPublic("HeartbeatInterval")) {
            config->setInt(300);  // Changed from 60s to 300s (5 minutes)
            Serial.println("[OCPP]   ✓ Heartbeat interval: 300s (production optimized)");
        }
        
        // Set TransactionMessageAttempts to 3 to allow retries for failed StopTransaction
        // This prevents transaction queue from filling up with unconfirmed transactions
        if (auto config = MicroOcpp::getConfigurationPublic("TransactionMessageAttempts")) {
            config->setInt(3);  // Allow 3 attempts before discarding
            Serial.println("[OCPP]   ✓ TransactionMessageAttempts: 3 (allow retries)");
        }
        
        // Increase retry interval to give WebSocket time to stabilize
        if (auto config = MicroOcpp::getConfigurationPublic("TransactionMessageRetryInterval")) {
            config->setInt(120);  // 2 minutes between retries
            Serial.println("[OCPP]   ✓ TransactionMessageRetryInterval: 120s");
        }
        
        // PRODUCTION: Limit message queue size to prevent overflow
        if (auto config = MicroOcpp::getConfigurationPublic("MessageQueueSizeThreshold")) {
            config->setInt(10);  // Limit queue to 10 messages
            Serial.println("[OCPP]   ✓ MessageQueueSizeThreshold: 10 (prevent overflow)");
        }
    }

    // Transaction notifications
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        setTxNotificationOutput([](MicroOcpp::Transaction *tx, TxNotification notification) {
            // *** CRITICAL DIAGNOSTIC: Log EVERY callback invocation ***
            Serial.printf("[OCPP_CALLBACK] 🔔 TxNotification fired: type=%d connectorId=%d txId=%s\n", 
                         notification, 
                         tx ? 1 : 0,  // Assuming connector 1
                         tx && tx->getTransactionId() >= 0 ? "valid" : "invalid");
            
            // Common debug snapshot
            int txIdReported = tx ? tx->getTransactionId() : -1;
            bool txRunning = ocpp::isTransactionRunningSafe(1);
            bool permitsCharge = ocpp::ocppPermitsChargeSafe(1);

            if (notification == TxNotification_RemoteStart) {
                // CRITICAL: Ensure clean slate for new RemoteStart
                activeTransactionId = -1;
                transactionActive = false;
                remoteStartAccepted = false;

                Serial.println("\n[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***");  // CRITICAL MARKER
                Serial.println("[OCPP] 📥 RemoteStart received");
                Serial.printf("[OCPP]   snapshot: txId=%d txRunning=%d permitsCharge=%d\n", txIdReported, txRunning, permitsCharge);
                Serial.printf("[OCPP]   flags: chargerHealthy=%d bmsSafe=%d batteryConnected=%d gunPhys=%d remoteStartAccepted=%d transactionLocked=%d transactionActive=%d activeTx=%d\n",
                    isChargerModuleHealthy(), bmsSafeToCharge, batteryConnected, gunPhysicallyConnected, remoteStartAccepted, transactionLocked, transactionActive, activeTransactionId);

#if ENABLE_TEST_MODE
                {  // Scoped block to avoid variable name conflicts
                    // ═══════════════════════════════════════════════════════════════
                    // TEST MODE BYPASS - Skip all safety validation
                    // ═══════════════════════════════════════════════════════════════
                    Serial.println("\\n╔═══════════════════════════════════════════════════════════════╗");
                    Serial.println("║  ⚠️  TEST MODE - Bypassing OCPP Safety Validation           ║");
                    Serial.println("╚═══════════════════════════════════════════════════════════════╝");
                    Serial.println("[TEST_MODE] 🔓 Skipping ALL safety checks:");
                    Serial.println("[TEST_MODE]    ✓ BMS safety check BYPASSED");
                    Serial.println("[TEST_MODE]    ✓ Voltage range check BYPASSED");
                    Serial.println("[TEST_MODE]    ✓ Temperature check BYPASSED");
                    Serial.println("[TEST_MODE]    ✓ Charger health check BYPASSED");
                    Serial.println("[TEST_MODE]    ✓ Fault lock check BYPASSED");
                    
                    // Delegate to State Machine (which will also bypass in test mode)
                    bool acceptedBySM = prod::g_ocppStateMachine.onRemoteStartTransaction(tx ? tx->getIdTag() : "Remote", 1);
                    
                    if (!acceptedBySM) {
                        Serial.println("[OCPP] ❌ REJECTED by State Machine logic");
                        remoteStartAccepted = false;
                        return;
                    }

                    Serial.println("[OCPP] ✅ RemoteStart accepted (TEST MODE - no safety validation)");
                    Serial.println("╚═══════════════════════════════════════════════════════════════╝\\n");
                    remoteStartAccepted = true;
                    return;  // Skip all production safety checks below
                }
#endif

                // ═══════════════════════════════════════════════════════════════
                // PRE-TRANSACTION SAFETY VALIDATION (Production-Grade)
                // ═══════════════════════════════════════════════════════════════
                Serial.println("[OCPP] 🔍 Pre-transaction safety validation...");
                
                // Check 1: BMS Safety
                if (!bmsSafeToCharge) {
                    Serial.println("[OCPP] ❌ REJECTED: BMS not ready (byte4 != 0x00)");
                    ocpp::sendChargerStatus(false, "BMS not ready - vehicle not safe to charge");
                    remoteStartAccepted = false;
                    return;
                }
                
                // Check 2: Voltage Range
                if (terminalVolt > ALERT_VOLTAGE_MAX_V || terminalVolt < ALERT_VOLTAGE_MIN_V) {
                    Serial.printf("[OCPP] ❌ REJECTED: Voltage out of range (%.1fV, limits: %.0f-%.0fV)\n", 
                                  terminalVolt, ALERT_VOLTAGE_MIN_V, ALERT_VOLTAGE_MAX_V);
                    char reason[128];
                    snprintf(reason, sizeof(reason), "Voltage out of safe range: %.1fV (limits: %.0f-%.0fV)", 
                             terminalVolt, ALERT_VOLTAGE_MIN_V, ALERT_VOLTAGE_MAX_V);
                    ocpp::sendChargerStatus(false, reason);
                    remoteStartAccepted = false;
                    return;
                }
                
                // Check 3: Temperature
                if (chargerTemp > ALERT_TEMP_CRITICAL_C) {
                    Serial.printf("[OCPP] ❌ REJECTED: Temperature too high (%.1f°C, limit: %.0f°C)\n", 
                                  chargerTemp, ALERT_TEMP_CRITICAL_C);
                    char reason[128];
                    snprintf(reason, sizeof(reason), "Temperature too high: %.1f°C (limit: %.0f°C)", 
                             chargerTemp, ALERT_TEMP_CRITICAL_C);
                    ocpp::sendChargerStatus(false, reason);
                    remoteStartAccepted = false;
                    return;
                }
                
                // Check 4: Charger Module Health (SOFT SAFETY)
                // We allow RemoteStart even if there's a momentary CAN dip, provided 
                // the charger was healthy within the last 30 seconds.
                static unsigned long lastKnownGoodHealth = 0;
                if (isChargerModuleHealthy()) {
                    lastKnownGoodHealth = millis();
                }

                if (!isChargerModuleHealthy() && (millis() - lastKnownGoodHealth > 30000)) {
                    Serial.printf("[OCPP] ❌ REJECTED: Charger module offline (CAN timeout > 30s, last health %lu ms ago)\n", 
                                  millis() - lastKnownGoodHealth);
                    ocpp::sendChargerStatus(false, "Charger module offline - check CAN bus connection");
                    remoteStartAccepted = false;
                    return;
                } else if (!isChargerModuleHealthy()) {
                    Serial.printf("[OCPP] ⚠️  Charger module offline, but accepting due to grace period (%lu ms since last health)\n",
                                  millis() - lastKnownGoodHealth);
                }
                
                // Check 5: Fault Stabilization Lock
                if (faultLockActive) {
                    unsigned long timeSinceFault = millis() - faultLockTime;
                    if (timeSinceFault < FAULT_STABILIZATION_PERIOD_MS) {
                        Serial.printf("[OCPP] ❌ REJECTED: Fault recovery in progress (%lu/%u ms)\n", 
                                      timeSinceFault, (unsigned int)FAULT_STABILIZATION_PERIOD_MS);
                        ocpp::sendChargerStatus(false, "Fault recovery in progress - please wait");
                        remoteStartAccepted = false;
                        return;
                    } else {
                        // Stabilization period passed - verify conditions are now safe
                        if (bmsSafeToCharge && 
                            terminalVolt >= ALERT_VOLTAGE_MIN_V && terminalVolt <= ALERT_VOLTAGE_MAX_V &&
                            chargerTemp <= ALERT_TEMP_CRITICAL_C) {
                            Serial.println("[OCPP] ✅ Fault stabilized - clearing lock");
                            faultLockActive = false;
                        } else {
                            Serial.println("[OCPP] ❌ REJECTED: Conditions still unsafe after stabilization");
                            ocpp::sendChargerStatus(false, "Fault conditions persist - not safe to charge");
                            remoteStartAccepted = false;
                            return;
                        }
                    }
                }
                
                Serial.println("[OCPP] ✅ All safety checks passed");
                // ═══════════════════════════════════════════════════════════════

                // Delegate to State Machine for decision
                bool acceptedBySM = prod::g_ocppStateMachine.onRemoteStartTransaction(tx ? tx->getIdTag() : "Remote", 1);
                
                if (!acceptedBySM) {
                    Serial.println("[OCPP] ❌ REJECTED by State Machine logic");
                    remoteStartAccepted = false;
                    return;
                }

                Serial.println("[OCPP] ✅ RemoteStart accepted (latching)");
                remoteStartAccepted = true;

            } else if (notification == TxNotification_StartTx) {
                Serial.println("\n[OCPP] 📥 StartTransaction notification");
                Serial.printf("[OCPP]   tx reported id=%d (tx param=%d)\n", txIdReported, tx ? tx->getTransactionId() : -1);
                Serial.printf("[OCPP]   Pre-start flags: chargerHealthy=%d bmsSafe=%d batteryConnected=%d gunPhys=%d\n",
                    isChargerModuleHealthy(), bmsSafeToCharge, batteryConnected, gunPhysicallyConnected);

                if (!isChargerModuleHealthy()) {
                    Serial.println("[OCPP] ❌ Transaction started but charger OFFLINE - not enabling charging");
                    return;
                }

                int txId = tx ? tx->getTransactionId() : -1;
                localTransactionId = txId;
                activeTransactionId = txId;
                transactionActive = true;
                transactionLocked = true;
                chargingEnabled = true;
                txStartTime = millis();
                lastMeterValueSent = 0;
                lastDataTransferSent = 0;
                firstMeterValueSent = false;

                if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    energyWh = 0.0f;
                    xSemaphoreGive(dataMutex);
                }

                Serial.println("\n>>> CONTACTOR ON <<<");
                Serial.printf("[OCPP] ▶️  Transaction STARTED - Charging ENABLED (txId=%d)\n", txId);
                Serial.println("[GATE] ✅ HARD GATE OPEN\n");
                Serial.println("[OCPP] 📊 First MeterValue at 5s, then every 10s");

                prod::g_ocppStateMachine.onTransactionStarted(1, "RemoteStart", txId);

            } else if (notification == TxNotification_RemoteStop) {
                Serial.println("\n[OCPP] 📥 RemoteStop received");
                Serial.printf("[OCPP]   snapshot before stop: txActive=%d activeTx=%d txRunning=%d\n", transactionActive, activeTransactionId, txRunning);

                // CRITICAL FIX: Accept RemoteStop even if transaction ID doesn't match
                // Server may have a different ID than ESP32 due to StartTransaction.conf timeout
                if (!transactionActive && activeTransactionId <= 0 && !txRunning) {
                    Serial.println("[OCPP] ⚠️  RemoteStop received but NO ACTIVE TRANSACTION!");
                    return;
                }
                
                // Get the transaction ID from the server's RemoteStop request
                int serverTxId = tx ? tx->getTransactionId() : -1;
                if (serverTxId > 0 && serverTxId != activeTransactionId) {
                    Serial.printf("[OCPP] ⚠️  Transaction ID mismatch: local=%d server=%d\n", activeTransactionId, serverTxId);
                    Serial.println("[OCPP] 🔄 Syncing to server's transaction ID...");
                    activeTransactionId = serverTxId;
                    localTransactionId = serverTxId;
                }

                // Delegate to State Machine for decision
                bool stopAccepted = prod::g_ocppStateMachine.onRemoteStopTransaction(activeTransactionId);

                if (!stopAccepted) {
                    Serial.println("[OCPP] ❌ RemoteStop REJECTED by State Machine");
                    return;
                }

                // 1. Set flag to prevent restart
                chargingEnabled = false;

                // 2. IMMEDIATELY send CAN command to stop physical charger
                sendImmediateChargerStop();

                Serial.println("[OCPP] ⏹️  Charging disabled - hardware stop command sent");

                // 3. CRITICAL: End transaction so StopTransaction is sent to CSMS
                if (transactionActive || txRunning) {
                    Serial.printf("[OCPP] 🛑 RemoteStop → ending transaction (txId=%d)\n", activeTransactionId);
                    bool ended = ocpp::endTransactionSafe(nullptr, "Remote", 1);
                    if (ended) {
                        Serial.println("[OCPP] ✅ StopTransaction queued to server");
                    } else {
                        Serial.println("[OCPP] ❌ endTransactionSafe failed or timed out");
                    }
                } else {
                    Serial.printf("[OCPP] ℹ️  No active transaction (txActive=%d activeTx=%d running=%d)\n", transactionActive, activeTransactionId, txRunning);
                }

            } else if (notification == TxNotification_StopTx) {
                Serial.println("\n[OCPP] 📥 StopTransaction received");
                
                // Record stop time for post-transaction summaries
                txStopTime = millis();
                
                // Clear all transaction state
                transactionLocked = false;
                localTransactionId = -1;
                activeTransactionId = -1;
                transactionActive = false;
                remoteStartAccepted = false;
                chargingEnabled = false;
                
                // CRITICAL: Force persistence cleanup to prevent re-hydration loops ("Zombie" IDs)
                prod::g_persistence.clearTransaction();
                
                Serial.println("[OCPP] ⏹️  Transaction stopped - all flags cleared");
                Serial.println("[OCPP] 📊 VehicleInfo will continue every 10s until gun unplugged");

                // Clear state machine
                prod::g_ocppStateMachine.onTransactionStopped(localTransactionId);
            }
        });
    }
    Serial.println("[OCPP]   ✓ Transaction callbacks registered");
    
    // *** DIAGNOSTIC: Add message-level sniffer for debugging ***
    Serial.println("[OCPP]   📡 Message-level diagnostics enabled:");
    Serial.println("[OCPP]      - Will log ALL incoming operations via MicroOcpp verbose");
    Serial.println("[OCPP]      - MicroOcpp registered operations: RemoteStart/Stop");
    Serial.println("[OCPP]      - If RemoteStart arrives, will show in [MO] logs");
    
    // *** DIAGNOSTIC: Confirm callback structure is ready ***
    Serial.println("[OCPP]   🔍 Registered operations including: RemoteStartTransaction, RemoteStopTransaction");
    Serial.println("[OCPP]   📡 Waiting for RemoteStart command from CSMS...");

    // Configure OTA firmware updates
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        if (auto fwService = getOcppContext()->getModel().getFirmwareService())
        {
            fwService->setDownloadFileWriter(
                prod::OTAManager::onFirmwareData,
                [](MO_FtpCloseReason reason) {
                    prod::OTAManager::onDownloadComplete((int)reason);
                }
            );
            Serial.println("[OCPP]   ✓ OTA firmware update registered");
        }
        else
        {
            Serial.println("[OCPP]   ⚠️  FirmwareService not available");
        }
    }

    // Check if operative (will be false until BootNotification accepted)
    {
        OcppLock lock;
        if (lock.ok())
        {
            bool operative = isOperative();
            Serial.printf("[OCPP] 🔍 isOperative() = %s (will become TRUE after BootNotification)\n", 
                          operative ? "TRUE" : "FALSE");
        }
    }

    Serial.println("[OCPP] ✅ OCPP initialization complete");
    Serial.println("[OCPP] ⏳ Waiting for WebSocket connection and BootNotification...");
    
    // CRITICAL: Set flag to allow loop() to access connector 1
    ocppInitialized = true;

    return true;
}

void ocpp::poll()
{
    // ═══════════════════════════════════════════════════════════════
    // IMMEDIATE SENDS DURING ACTIVE TRANSACTION
    // ═══════════════════════════════════════════════════════════════
    
    // Send first MeterValue at 5s after transaction start, then scale dynamically
    static unsigned long lastScaledElapsed = 0; // Track last elapsed for scaling
    static int currentMvInterval = 5;          // Track current interval, reset each session

    if (transactionActive) {
        unsigned long elapsed = millis() - txStartTime;
        
        // 1. Handle first MeterValue trigger at 5s
        if (!firstMeterValueSent && elapsed >= 5000) {
            Serial.println("[OCPP] 📊 Triggering FIRST MeterValue (5s after start)");
            firstMeterValueSent = true;
            lastMeterValueSent = millis();
            lastScaledElapsed = 0; // Reset scaling tracker
            currentMvInterval = 5; // Start at 5s
        }

        // 2. Dynamic Interval Scaling
        // 0-30s: 5s | 30-120s: 10s | 120-300s: 20s | >300s: 30s
        int nextInterval;
        if      (elapsed < 30000)  nextInterval = 5;
        else if (elapsed < 120000) nextInterval = 10;
        else if (elapsed < 300000) nextInterval = 20;
        else                       nextInterval = 30;

        if (nextInterval != currentMvInterval) {
            OcppLock lock;
            if (lock.ok()) {
                if (auto config = MicroOcpp::getConfigurationPublic("MeterValueSampleInterval")) {
                    config->setInt(nextInterval);
                    Serial.printf("[OCPP] 📊 Scaling MeterValue interval: %ds -> %ds (elapsed: %lus)\n", 
                                  currentMvInterval, nextInterval, elapsed/1000);
                    currentMvInterval = nextInterval;
                }
            }
        }
    } else {
        // Not in a transaction — reset tracking state for next session
        if (currentMvInterval != 5) {
            OcppLock lock;
            if (lock.ok()) {
                if (auto config = MicroOcpp::getConfigurationPublic("MeterValueSampleInterval")) {
                    config->setInt(5);
                    currentMvInterval = 5;
                    Serial.println("[OCPP] 📊 MeterValue interval reset to 5s for next session");
                }
            }
        }
        firstMeterValueSent = false; // Reset for next session
    }
    
    // ═══════════════════════════════════════════════════════════════
    // DIAGNOSTICS & HEALTH (ALWAYS RUN)
    // ═══════════════════════════════════════════════════════════════
    
    // Monitor charger health (availability updated automatically via setEvseReadyInput)
    static unsigned long lastHealthPoll = 0;
    bool healthy = isChargerModuleHealthy();
    
    if (millis() - lastHealthPoll >= 5000) {
        Serial.printf("[OCPP_HEALTH] Uptime: %lu ms | Healthy: %s | SM State: %s\n", 
                      millis(), 
                      healthy ? "YES" : "NO",
                      prod::g_ocppStateMachine.getStateName());
        lastHealthPoll = millis();
    }
    
    static bool lastHealthyValue = true;
    if (healthy != lastHealthyValue) {
        Serial.printf("[OCPP] Charger module health changed: %s\n",
            healthy ? "ONLINE" : "OFFLINE");
        lastHealthyValue = healthy;
    }

    // Periodic diagnostic (every 10s)
    static unsigned long lastDiagnosticLog = 0;
    if (millis() - lastDiagnosticLog > 10000) {
        OcppLock lock;
        if (lock.ok()) {
            auto tx = getTransaction(1);
            bool libTxActive = (tx && tx->isActive());
            auto currentSMState __attribute__((unused)) = prod::g_ocppStateMachine.getState();

            Serial.printf("[OCPP] Status: SM=%s | Tx=%s | Charging=%s | Operative=%d\n",
                         prod::g_ocppStateMachine.getStateName(),
                         libTxActive ? "Active" : "Idle",
                         chargingEnabled ? "ON" : "OFF",
                         isOperative());
        }
        lastDiagnosticLog = millis();
    }

    // ═══════════════════════════════════════════════════════════════
    // MAIN OCPP LOOP & SYNC (REQUIRES LOCK)
    // ═══════════════════════════════════════════════════════════════
    static bool oldTransactionCleaned = false;
    static bool staleStateCleaned = false;
    static bool cleanupInProgress = false;

    {
        OcppLock lock;
        if (!lock.ok())
        {
            return;
        }
        mocpp_loop();

        // CLEANUP: Force-stop old transactions after BootNotification
        if (!oldTransactionCleaned && isOperative()) {
            auto tx = getTransaction(1);
            if (tx && tx->isActive()) {
                int oldTxId = tx->getTransactionId();
                Serial.printf("[OCPP] 🧹 Cleaning up old transaction (txId=%d)\n", oldTxId);
                
                cleanupInProgress = true;  // Block sync
                
                // Force stop the old transaction
                chargingEnabled = false;
                endTransaction(nullptr, "PowerLoss", 1);
                
                // Clear global state
                transactionActive = false;
                transactionLocked = false;
                activeTransactionId = -1;
                localTransactionId = -1;
                remoteStartAccepted = false;
                
                // CRITICAL: Force state machine to Available
                prod::g_ocppStateMachine.forceState(prod::ConnectorState::Available);
                
                Serial.println("[OCPP] ✅ Old transaction cleaned - ready for new RemoteStart");
                
                cleanupInProgress = false;  // Re-enable sync
            }
            oldTransactionCleaned = true;
        }
        
        // CRITICAL FIX: Clear stale global state if library has no transaction
        if (!staleStateCleaned && isOperative()) {
            auto tx = getTransaction(1);
            if (!tx && (transactionActive || activeTransactionId > 0 || remoteStartAccepted)) {
                Serial.println("\n╔═══════════════════════════════════════════════════════════════╗");
                Serial.println("║  🧹 CLEARING STALE TRANSACTION STATE                         ║");
                Serial.println("╚═══════════════════════════════════════════════════════════════╝");
                Serial.printf("[OCPP] 🔍 Stale state detected: txActive=%d activeTxId=%d remoteStart=%d\n",
                             transactionActive, activeTransactionId, remoteStartAccepted);
                Serial.println("[OCPP] 🔍 Library has NO active transaction - clearing global flags");
                
                transactionActive = false;
                transactionLocked = false;
                activeTransactionId = -1;
                localTransactionId = -1;
                remoteStartAccepted = false;
                chargingEnabled = false;
                
                // FIX: Force state machine to sync with reality
                if (gunPhysicallyConnected && batteryConnected) {
                    Serial.println("[OCPP] 🔄 Vehicle connected - forcing state to Preparing");
                    prod::g_ocppStateMachine.forceState(prod::ConnectorState::Preparing);
                } else {
                    Serial.println("[OCPP] 🔄 No vehicle - forcing state to Available");
                    prod::g_ocppStateMachine.forceState(prod::ConnectorState::Available);
                }
                
                Serial.println("[OCPP] ✅ Stale state cleared - ready for new RemoteStart");
                Serial.println("╚═══════════════════════════════════════════════════════════════╝\n");
            }
            staleStateCleaned = true;
        }

        // SKIP SYNC if cleanup is in progress
        if (cleanupInProgress) {
            return;
        }

        // *** ROBUST SYNC TRANSACTION ID ***
        // CRITICAL: Don't sync until AFTER stale cleanup completes
        if (!staleStateCleaned) {
            return;
        }
        
        auto tx = getTransaction(1); // Check connector 1
        if (tx) {
            
            // 1. Sync active flag if library has an active transaction
            if (!transactionActive) {
                Serial.println("[OCPP] 🔄 Sync: Library has active tx, updating global flags");
                transactionActive = true;
            }
            
            // 2. Sync Transaction ID if it differs
            int currentId = tx->getTransactionId();
            if (activeTransactionId != currentId) {
                if (currentId > 0) {
                    Serial.printf("[OCPP] 🔄 ID Sync: %d -> %d\n", activeTransactionId, currentId);
                    activeTransactionId = currentId;
                    localTransactionId = currentId;
                    
                    // CRITICAL FIX: Don't call onTransactionStarted during sync
                    // State machine transition happens ONLY in TxNotification_StartTx callback
                    // Just update persistence here without changing state
                    char txnIdStr[32];
                    snprintf(txnIdStr, sizeof(txnIdStr), "%d", currentId);
                    prod::g_persistence.saveTransaction(txnIdStr, "RestoreSync");
                } else if (currentId < 0 && activeTransactionId > 0) {
                    // CRITICAL: Force library to resume the persisted ID instead of waiting for a new one
                    Serial.printf("[OCPP] ⚠️  ID Mismatch: local=%d, lib=%d. FORCING RE-HYDRATION.\n", activeTransactionId, currentId);
                    tx->setTransactionId(activeTransactionId);
                    tx->setAuthorized();
                    tx->getStartSync().confirm(); // Force confirmed/running state
                    Serial.println("[OCPP]   ✅ Library re-hydrated directly. RemoteStop will now work.");
                }
            }
        } else {
            // 3. Sync: If library has NO active transaction, BUT we have a persisted transaction,
            // DO NOT clear global flags here. Instead, let the state machine or poll handle re-hydration on next cycle
            // or if it was definitely ended.
            
            if (activeTransactionId > 0) {
                // If library has no task, check if state machine is actually IDLE
                auto smState = prod::g_ocppStateMachine.getState();
                if (smState == prod::ConnectorState::Available || smState == prod::ConnectorState::Faulted) {
                    Serial.printf("[OCPP] 🔄 Sync: Library idle and state is %s, clearing zombie transaction #%d\n", 
                                  prod::g_ocppStateMachine.getStateName(), activeTransactionId);
                    transactionActive = false;
                    transactionLocked = false;
                    activeTransactionId = -1;
                    localTransactionId = -1;
                    remoteStartAccepted = false;
                    chargingEnabled = false;
                    prod::g_persistence.clearTransaction();
                } else {
                    static unsigned long lastRehydrationAttempt = 0;
                    if (millis() - lastRehydrationAttempt > 30000) { // Every 30s
                        Serial.printf("[OCPP] ⚠️  Rehydration sync: Global txActive=%d, id=%d, but library tx=NULL\n", 
                                      transactionActive, activeTransactionId);
                        lastRehydrationAttempt = millis();
                    }
                }
            } else if (transactionActive || transactionLocked || activeTransactionId != -1) {
                Serial.println("[OCPP] 🔄 Sync: Library has NO active tx and no persisted ID, clearing global state");
                transactionActive = false;
                transactionLocked = false;
                activeTransactionId = -1;
                localTransactionId = -1;
                remoteStartAccepted = false;
                chargingEnabled = false;
                prod::g_persistence.clearTransaction();
            }
        }
    }
    
    // Check if connection status changed
    static bool lastOperative = false;
    bool operative = isOperative();
    
    if (operative != lastOperative) {
        Serial.printf("[OCPP] Connection status changed: %s\n", 
                      operative ? "CONNECTED" : "DISCONNECTED");
        
        // CRITICAL: If just reconnected, force BootNotification to re-bind session on CSMS
        if (operative) {
            Serial.println("[OCPP] 🔄 RECONNECTED! Forcing BootNotification sync...");
            OcppLock lock;
            if (lock.ok()) {
                auto context = getOcppContext();
                if (context) {
                    auto bs = context->getModel().getBootService();
                    if (bs) {
                        bs->notifyRegistrationStatus(MicroOcpp::RegistrationStatus::Pending);
                        Serial.println("[OCPP]   ✅ BootNotification queued for sync");
                    }
                }
            }
        }
        lastOperative = operative;
    }
}

bool ocpp::isConnected()
{
    // Check if MicroOcpp is operative (WebSocket connected + initialized)
    bool operative = false;
    {
        OcppLock lock;
        if (lock.ok())
        {
            operative = isOperative();
        }
    }
    
    // Debug: Log once when status changes
    static bool lastOperative = false;
    if (operative != lastOperative) {
        Serial.printf("[OCPP] Connection status changed: %s\n", operative ? "CONNECTED" : "DISCONNECTED");
        lastOperative = operative;
    }
    
    return operative;
}

void ocpp::sendVehicleInfo(float soc, float maxCurrent, float voltage, float current, float temperature, uint8_t model, float range, const char* vin)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return;
    }
    if (!isOperative()) {
        return;
    }

    // Validate critical data only (allow SOC=0)
    if (voltage <= 0.0f || maxCurrent <= 0.0f) {
        return;
    }

    const char* modelName = "Unknown";
    if (model == 1) modelName = "Classic";
    else if (model == 2) modelName = "Pro";
    else if (model == 3) modelName = "Max";

    // VehicleInfo logging
    Serial.printf("\n[OCPP] 📤 Sending VehicleInfo (Pre-Tx):\n");
    Serial.printf("  SOC=%.1f%% | Model=%s | Range=%.1fkm | MaxI=%.1fA | VIN=%s\n", soc, modelName, range, maxCurrent, vin);

    sendRequest("DataTransfer",
        [soc, maxCurrent, model, range, modelName, vin]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            MicroOcpp::JsonDoc dataDoc(256);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["soc"] = soc;
            dataObj["maxCurrent"] = maxCurrent;
            dataObj["model"] = modelName;
            dataObj["range"] = range;
            dataObj["vin"] = vin;
            
            String dataStr;
            serializeJson(dataObj, dataStr);
            
            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(512));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"] = "RivotMotors";
            payload["messageId"] = "VehicleInfo";
            payload["data"] = dataStr;
            return doc;
        },
        [](JsonObject response) {
            // [DISABLED] VehicleInfo response logging - removed for cleaner console
            // const char* status = response["status"] | "Unknown";
            // Serial.printf("[OCPP] ✅ VehicleInfo response: %s\n\n", status);
        }
    );
}

void ocpp::sendSessionSummary(float finalSoc, float energyDelivered, float duration)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return;
    }
    if (!isOperative()) {
        return;
    }

    // [DISABLED] SessionSummary logging - removed for cleaner console
    // Serial.printf("\n[OCPP] 📊 Sending SessionSummary:\n");
    // Serial.printf("  FinalSOC=%.1f%% | Energy=%.2fWh | Duration=%.1fmin\n\n", 
    //               finalSoc, energyDelivered, duration);

    sendRequest("DataTransfer",
        [finalSoc, energyDelivered, duration]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            MicroOcpp::JsonDoc dataDoc(256);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["finalSoc"] = finalSoc;
            dataObj["energyDelivered"] = energyDelivered;
            dataObj["durationMinutes"] = duration;
            
            String dataStr;
            serializeJson(dataObj, dataStr);
            
            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(512));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"] = "RivotMotors";
            payload["messageId"] = "SessionSummary";
            payload["data"] = dataStr;
            return doc;
        },
        [](JsonObject response) {
            const char* status = response["status"] | "Unknown";
            Serial.printf("[OCPP] ✅ SessionSummary response: %s\n\n", status);
        }
    );
}

void ocpp::sendBMSAlert(const char* alertType, const char* message)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return;
    }
    if (!isOperative()) {
        return;
    }

    Serial.printf("[OCPP] 🚨 Sending BMSAlert: %s - %s\n", alertType, message);

    sendRequest("DataTransfer",
        [alertType, message]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            MicroOcpp::JsonDoc dataDoc(128);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["alertType"] = alertType;
            dataObj["message"] = message;
            dataObj["timestamp"] = millis();
            
            String dataStr;
            serializeJson(dataObj, dataStr);
            
            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(512));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"] = "RivotMotors";
            payload["messageId"] = "BMSAlert";
            payload["data"] = dataStr;
            return doc;
        },
        [](JsonObject response) {
            Serial.printf("[OCPP] ✅ BMSAlert acknowledged\n");
        }
    );
}

void ocpp::sendSystemAlert(const char* alertType, const char* message, const char* severity)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return;
    }
    if (!isOperative()) {
        return;
    }

    Serial.printf("[OCPP] 🚨 Alert [%s]: %s - %s\n", severity, alertType, message);

    sendRequest("DataTransfer",
        [alertType, message, severity]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            MicroOcpp::JsonDoc dataDoc(128);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["alertType"] = alertType;
            dataObj["message"] = message;
            dataObj["severity"] = severity;
            dataObj["timestamp"] = millis();
            
            String dataStr;
            serializeJson(dataObj, dataStr);
            
            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(512));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"] = "RivotMotors";
            payload["messageId"] = "SystemAlert";
            payload["data"] = dataStr;
            return doc;
        },
        [](JsonObject response) {
            Serial.printf("[OCPP] ✅ SystemAlert acknowledged\n");
        }
    );
}

void ocpp::sendChargerStatus(bool ready, const char* reason)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return;
    }
    if (!isOperative()) {
        return;
    }

    Serial.printf("[OCPP] 📊 Sending ChargerStatus: %s - %s\n", ready ? "READY" : "NOT READY", reason);

    sendRequest("DataTransfer",
        [ready, reason]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            MicroOcpp::JsonDoc dataDoc(256);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["ready"] = ready;
            dataObj["reason"] = reason;
            dataObj["timestamp"] = millis();
            
            String dataStr;
            serializeJson(dataObj, dataStr);
            
            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(512));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"] = "RivotMotors";
            payload["messageId"] = "ChargerStatus";
            payload["data"] = dataStr;
            return doc;
        },
        [](JsonObject response) {
            Serial.printf("[OCPP] ✅ ChargerStatus acknowledged\n");
        }
    );
}
