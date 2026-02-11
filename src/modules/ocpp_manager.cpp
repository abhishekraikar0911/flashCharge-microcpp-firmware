// OCPP Manager: All OCPP-related logic isolated for easy debugging
#include <Arduino.h>
#include <WiFi.h>
#include <MicroOcpp.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Model/Transactions/Transaction.h>

#include "../../include/ocpp/ocpp_client.h"
#include "../../include/secrets.h"
#include "../../include/header.h"
#include "../../include/modules/ota_manager.h"
#include "../../include/ocpp_state_machine.h"
#include <MicroOcpp/Core/Context.h>
#include <MicroOcpp/Model/Model.h>
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
static unsigned long txStartTime = 0;
static bool transactionLocked = false;
static int localTransactionId = -1;

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
            if (terminalVolt < 56.0f || terminalVolt > 85.5f) return 0;
            if (terminalCurr < 0.0f || terminalCurr > 300.0f) return 0;
            return (int)(terminalVolt * terminalCurr);
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

    // EVSE ready (charger module healthy)
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        setEvseReadyInput([]() {
            bool healthy = isChargerModuleHealthy();
            static bool lastHealthy = true;
            if (healthy != lastHealthy) {
                Serial.printf("[OCPP]   EVSE ready: %s\n", healthy ? "YES" : "NO");
                lastHealthy = healthy;
            }
            return healthy;
        });
    }
    Serial.printf("[OCPP]   ✓ EVSE ready registered (initial: %s)\n", 
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

    // MeterValues - OCPP 1.6 standard measurands only
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        addMeterValueInput([]() -> float { return socPercent; }, "SoC", "Percent", nullptr, nullptr, 1);
        addMeterValueInput([]() -> float { return terminalVolt; }, "Voltage", "V", nullptr, nullptr, 1);
        addMeterValueInput([]() -> float { return terminalCurr; }, "Current.Import", "A", nullptr, nullptr, 1);
        addMeterValueInput([]() -> float { return BMS_Imax; }, "Current.Offered", "A", nullptr, nullptr, 1);
        addMeterValueInput([]() -> float { return chargerTemp; }, "Temperature", "Celsius", nullptr, nullptr, 1);
    }
    Serial.println("[OCPP]   ✓ MeterValues registered (standard measurands)");

    // Configure intervals - Clock-aligned sampling for immediate first sample
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        if (auto config = MicroOcpp::getConfigurationPublic("MeterValueSampleInterval")) {
            config->setInt(5);
            Serial.println("[OCPP]   ✓ MeterValues interval: 5s");
        }
        
        if (auto config = MicroOcpp::getConfigurationPublic("ClockAlignedDataInterval")) {
            config->setInt(0);  // Disable clock alignment for immediate samples
            Serial.println("[OCPP]   ✓ Clock alignment: disabled (immediate samples)");
        }

        if (auto config = MicroOcpp::getConfigurationPublic("MeterValuesSampledData")) {
            config->setString("Energy.Active.Import.Register,Power.Active.Import,Voltage,Current.Import,Current.Offered,SoC,Temperature");
            Serial.println("[OCPP]   ✓ Measurands configured (OCPP 1.6 standard)");
        }

        if (auto config = MicroOcpp::getConfigurationPublic("HeartbeatInterval")) {
            config->setInt(60);
            Serial.println("[OCPP]   ✓ Heartbeat interval: 60s");
        }
    }

    // Transaction notifications
    static bool sessionSummarySent = false;
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
                Serial.println("\n[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***");  // CRITICAL MARKER
                Serial.println("[OCPP] 📥 RemoteStart received");
                Serial.printf("[OCPP]   snapshot: txId=%d txRunning=%d permitsCharge=%d\n", txIdReported, txRunning, permitsCharge);
                Serial.printf("[OCPP]   flags: chargerHealthy=%d bmsSafe=%d batteryConnected=%d gunPhys=%d remoteStartAccepted=%d transactionLocked=%d transactionActive=%d activeTx=%d\n",
                    isChargerModuleHealthy(), bmsSafeToCharge, batteryConnected, gunPhysicallyConnected, remoteStartAccepted, transactionLocked, transactionActive, activeTransactionId);

                if (!isChargerModuleHealthy()) {
                    Serial.println("[OCPP] ❌ REJECTING: Charger module OFFLINE");
                    return;
                }

                if (!bmsSafeToCharge) {
                    Serial.println("[OCPP] ❌ REJECTING: BMS disallows charging (bmsSafeToCharge=false)");
                    ocpp::sendBMSAlert("REMOTE_START_REJECTED", "BMS denied charging");
                    return;
                }

                // Check if already in transaction
                if (transactionLocked || transactionActive) {
                    Serial.println("[OCPP] ⚠️  RemoteStart rejected - transaction already active or locked");
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
                sessionSummarySent = false;

                if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    energyWh = 0.0f;
                    xSemaphoreGive(dataMutex);
                }

                Serial.println("\n>>> CONTACTOR ON <<<");
                Serial.printf("[OCPP] ▶️  Transaction STARTED - Charging ENABLED (txId=%d)\n", txId);
                Serial.println("[GATE] ✅ HARD GATE OPEN\n");
                Serial.println("[OCPP] 📊 MeterValues will be sent automatically every 5s");

                prod::g_ocppStateMachine.onTransactionStarted(1, "RemoteStart", txId);

            } else if (notification == TxNotification_RemoteStop) {
                Serial.println("\n[OCPP] 📥 RemoteStop received");
                Serial.printf("[OCPP]   snapshot before stop: txActive=%d activeTx=%d txRunning=%d\n", transactionActive, activeTransactionId, txRunning);

                // *** DIAGNOSTIC: Check if transaction really exists ***
                if (!transactionActive && activeTransactionId <= 0) {
                    Serial.println("[OCPP] ⚠️  RemoteStop received but NO ACTIVE TRANSACTION!");
                    Serial.println("[OCPP] ℹ️  This can happen if:");
                    Serial.println("[OCPP]      - Device rebooted between RemoteStart and RemoteStop");
                    Serial.println("[OCPP]      - RemoteStart was rejected");
                    Serial.println("[OCPP]      - Transaction already stopped");
                    // Don't try to stop non-existent transaction
                    return;
                }

                // 1. Set flag to prevent restart
                chargingEnabled = false;

                // 2. IMMEDIATELY send CAN command to stop physical charger
                // This bypasses the 300-500ms polling delay for safety-critical stops
                sendImmediateChargerStop();

                Serial.println("[OCPP] ⏹️  Charging disabled - hardware stop command sent");

                // 3. CRITICAL: End transaction so StopTransaction is sent to CSMS
                if (transactionActive && activeTransactionId > 0 && ocpp::isTransactionRunningSafe(1)) {
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
                Serial.printf("[OCPP]   snapshot: localTx=%d activeTx=%d txRunning=%d sessionSummarySent=%d\n",
                    localTransactionId, activeTransactionId, txRunning, sessionSummarySent);

                if (!sessionSummarySent && transactionLocked) {
                    float duration = (millis() - txStartTime) / 60000.0f;
                    ocpp::sendSessionSummary(socPercent, energyWh, duration);
                    sessionSummarySent = true;
                }
                transactionLocked = false;
                localTransactionId = -1;
                activeTransactionId = -1;
                transactionActive = false;
                remoteStartAccepted = false;
                chargingEnabled = false;
                Serial.println("[OCPP] ⏹️  Transaction STOPPED and UNLOCKED");
                Serial.println("[GATE] 🔒 HARD GATE CLOSED\n");

                // Notify state machine
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
    {
        OcppLock lock;
        if (!lock.ok())
        {
            return;
        }
        mocpp_loop();

        // *** ROBUST SYNC TRANSACTION ID ***
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
                    
                    // Notify state machine to update persistence with the finalized ID
                    prod::g_ocppStateMachine.onTransactionStarted(1, "RestoreSync", currentId);
                } else if (currentId < 0 && activeTransactionId > 0) {
                    // CRITICAL: Force library to resume the persisted ID instead of waiting for a new one
                    Serial.printf("[OCPP] ⚠️  ID Mismatch: local=%d, lib=%d. FORCING RE-HYDRATION.\n", activeTransactionId, currentId);
                    tx->setTransactionId(activeTransactionId);
                    tx->setAuthorized();
                    tx->getStartSync().confirm(); // Force confirmed/running state
                    Serial.println("[OCPP]   ✅ Library re-hydrated directly. RemoteStop will now work.");
                }
            }
        }
    }
    
    // Periodic diagnostic: Confirm callback is listening (every 10s for debugging)
    static unsigned long lastDiagnosticLog = 0;
    if (millis() - lastDiagnosticLog > 10000) {
        Serial.printf("[OCPP_DIAG] ✅ Status (remoteStartAccepted=%d, txActive=%d, activeTxId=%d, libTx=%s)\n",
                     remoteStartAccepted, transactionActive, activeTransactionId, 
                     getTransaction(1) ? "PRESENT" : "MISSING");
        lastDiagnosticLog = millis();
    }
    
    // Monitor charger health (availability updated automatically via setEvseReadyInput)
    static bool lastHealthy = true;
    bool healthy = isChargerModuleHealthy();
    
    if (healthy != lastHealthy) {
        Serial.printf("[OCPP] Charger %s - Availability will update automatically\n",
            healthy ? "ONLINE" : "OFFLINE");
        lastHealthy = healthy;
    }
    
    // Check if connection status changed
    static bool lastOperative = false;
    bool operative = false;
    {
        OcppLock lock;
        if (lock.ok())
        {
            operative = isOperative();
        }
    }
    
    if (operative != lastOperative) {
        Serial.printf("[OCPP] Connection status changed: %s\n", 
                      operative ? "CONNECTED" : "DISCONNECTED");
        lastOperative = operative;
        
        if (operative) {
            Serial.printf("[OCPP] Charger health at connection: %s\n", 
                          healthy ? "ONLINE" : "OFFLINE");
        }
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

void ocpp::sendVehicleInfo(float soc, float maxCurrent, float voltage, float current, float temperature, uint8_t model, float range)
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

    // [DISABLED] VehicleInfo logging spam - removed for cleaner console
    // Serial.printf("\n[OCPP] 📤 Sending VehicleInfo:\n");
    // Serial.printf("  SOC=%.1f%% | Model=%s | Range=%.1fkm | MaxI=%.1fA\n", soc, modelName, range, maxCurrent);

    sendRequest("DataTransfer",
        [soc, maxCurrent, model, range, modelName]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            MicroOcpp::JsonDoc dataDoc(256);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["soc"] = soc;
            dataObj["maxCurrent"] = maxCurrent;
            dataObj["model"] = modelName;
            dataObj["range"] = range;
            
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
