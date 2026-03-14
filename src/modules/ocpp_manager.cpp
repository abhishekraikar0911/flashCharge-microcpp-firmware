// OCPP Manager: All OCPP-related logic isolated for easy debugging
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
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
#include "../../include/config/certificates.h"
#include "../../include/modules/network_manager.h"
#include "../../include/modules/ocpp_connection_helper.h"
#include <MicroOcpp/Core/Context.h>
#include <MicroOcpp/Model/Model.h>
#include <MicroOcpp/Model/Boot/BootService.h>
#include <MicroOcpp/Model/FirmwareManagement/FirmwareService.h>
#include <freertos/semphr.h>
#include "../../include/modules/ocpp_transaction_manager.h"
#include "../../include/modules/ocpp_meter_service.h"
#include "../../include/modules/system_state.h"

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

int ocpp::getTransactionIdSafe(unsigned int connectorId)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return -1;
    }
    auto tx = getTransaction(connectorId);
    if (tx)
    {
        return tx->getTransactionId();
    }
    return -1;
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
static unsigned long lastDataTransferSent = 0;

// Unified connection (GSM + WiFi + Watchdog)
static prod::UnifiedConnection g_ocppConnection;

bool ocpp::init()
{
    Serial.println("[OCPP] 🔌 Initializing OCPP...");
    Serial.printf("[OCPP] 📍 StationId: %s\n", SECRET_CHARGER_ID);
    Serial.printf("[OCPP] 🌐 Base URL: %s\n", SECRET_CSMS_URL);
    Serial.printf("[OCPP] 🔗 Full Connection URL: %s/%s\n", SECRET_CSMS_URL, SECRET_CHARGER_ID);

    // Require network (GSM or WiFi) before initializing
    if (!prod::g_networkManager.isConnected())
    {
        Serial.println("[OCPP] ⚠️  Network not connected - init deferred");
        return false;
    }
    Serial.printf("[OCPP] ✅ Network connected via %s\n",
                  prod::connectionTypeToString(prod::g_networkManager.getActiveConnection()));

    // Test network connectivity to server BEFORE initializing WebSocket
    // NOTE: WiFiClientSecure uses WiFi DNS — skip when connected via GSM
    if (prod::g_networkManager.getActiveConnection() == prod::ConnectionType::WIFI) {
        Serial.println("[OCPP] 🔍 Testing TCP connectivity to server...");
        Serial.printf("[OCPP] 🎯 Target: %s:%d\n", SECRET_CSMS_HOST, SECRET_CSMS_PORT);
        
        WiFiClientSecure testClient;
        testClient.setCACert(ISRG_ROOT_X1_CERT);
        bool serverReachable = testClient.connect(SECRET_CSMS_HOST, SECRET_CSMS_PORT, 10000);
        
        if (serverReachable) {
            Serial.println("[OCPP] ✅ TLS connection successful - server is reachable");
            testClient.stop();
        } else {
            Serial.println("[OCPP] ❌ FAILED: Cannot reach server via TLS");
            Serial.println("[OCPP] ⚠️  Possible causes:");
            Serial.println("[OCPP]    - DNS resolution failed for domain");
            Serial.println("[OCPP]    - Firewall blocking port 443");
            Serial.println("[OCPP]    - NTP time not synced (cert validation fails)");
            Serial.println("[OCPP]    - Server SSL certificate issue");
            Serial.println("[OCPP] 🔄 Will retry WebSocket connection anyway...");
        }
    } else {
        Serial.println("[OCPP] ✅ Connected via GSM — skipping WiFi TCP test (DNS not available)");
    }

    // Heap monitoring for TLS impact
    Serial.printf("[OCPP] 📊 Free heap BEFORE mocpp_initialize: %u bytes\n", ESP.getFreeHeap());

    // NOW initialize MicroOCPP with WSS + Root CA
    Serial.println("[OCPP] 🔐 Calling mocpp_initialize() with WSS/TLS...");
    {
        OcppLock lock;
        if (!lock.ok())
        {
            Serial.println("[OCPP] ❌ Mutex timeout - aborting init");
            return false;
        }
        // NOTE: Pass nullptr for CA cert so WebSocket uses setInsecure() fallback.
        // The ISRG Root X1 cert caused -8576 (X509 invalid format) errors in MbedTLS
        // when NTP time is synced. The direct test connection above still validates 
        // the cert for diagnostics. For production, enable proper cert pinning via
        // the security_manager once the cert chain is fully verified.
        // Use the UnifiedConnection which handles GSM/WiFi and the idle watchdog.
        // It wraps the standard WebSocketsClient for WiFi and uses a custom
        // transport for GSM.
        mocpp_initialize(
            g_ocppConnection,
            ChargerCredentials(SECRET_CHARGER_MODEL, SECRET_CHARGER_VENDOR),
            MicroOcpp::makeDefaultFilesystemAdapter(MicroOcpp::FilesystemOpt::Use),
            true); // autoRecover
        Serial.println("[OCPP] ✅ mocpp_initialize() completed");
        
        // STABILITY: Set stable default intervals (1min heartbeat, 30s metervalues)
        // These can be overridden by the CSMS via ChangeConfiguration
        auto heartbeatInterval = MicroOcpp::declareConfiguration<int>("HeartbeatInterval", 60);
        auto meterInterval = MicroOcpp::declareConfiguration<int>("MeterValueSampleInterval", 30);
        
        // CRITICAL: Specify which measurands to include in the periodic MeterValues message.
        // If this list is empty, no data will be sent to the server.
        auto meterValuesSampledData = MicroOcpp::declareConfiguration<const char*>(
            "MeterValuesSampledData", 
            "SoC,Voltage,Current.Import,Current.Offered,Temperature,Energy.Active.Import.Register,Power.Active.Import"
        );
        
        Serial.println("[OCPP]   ✓ Default stability intervals set (HB:60s, MV:30s)");
        Serial.println("[OCPP]   ✓ MeterValuesSampledData configured");
    }

    Serial.printf("[OCPP] 📊 Free heap AFTER mocpp_initialize: %u bytes\n", ESP.getFreeHeap());

    // CRITICAL: Configure all inputs AFTER mocpp_initialize()
    // Register Input Callbacks and MeterValues via Services
    g_meterService.begin();

    // Register transaction management via Service
    g_transactionManager.begin();
    g_transactionManager.registerConnectorInputs();

    // Transaction notifications
    {
        OcppLock lock;
        if (lock.ok())
        {
            setTxNotificationOutput([](MicroOcpp::Transaction *tx, TxNotification notification) {
                // *** CRITICAL DIAGNOSTIC: Log EVERY callback invocation ***
                Serial.printf("[OCPP_CALLBACK] 🔔 TxNotification fired: type=%d txId=%d\n", 
                             notification, tx ? tx->getTransactionId() : -1);
                
                switch (notification) {
                    case TxNotification_RemoteStart:
                        g_transactionManager.handleRemoteStart(tx);
                        break;
                    case TxNotification_StartTx:
                        g_transactionManager.handleStartTx(tx);
                        g_meterService.resetScaling(SystemState::instance().getTxStartTime());
                        break;
                    case TxNotification_RemoteStop:
                        g_transactionManager.handleRemoteStop(tx);
                        break;
                    case TxNotification_StopTx:
                        g_transactionManager.handleStopTx(tx);
                        break;
                    default:
                        break;
                }
            });
        }
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
    // MAIN OCPP LOOP & SYNC (REQUIRES LOCK)
    // ═══════════════════════════════════════════════════════════════
    {
        OcppLock lock;
        if (lock.ok()) {
            mocpp_loop();
        }
    }

    // Modular poll loops
    g_meterService.poll();
    g_transactionManager.syncTransactionState();

    auto& state = SystemState::instance();

    // ═══════════════════════════════════════════════════════════════
    // DIAGNOSTICS & HEALTH (EVERY 5S/10S)
    // ═══════════════════════════════════════════════════════════════
    static unsigned long lastHealthPoll = 0;
    if (millis() - lastHealthPoll >= 5000) {
        lastHealthPoll = millis();
        bool healthy = isChargerModuleHealthy();
        Serial.printf("[OCPP_HEALTH] Uptime: %lu ms | Healthy: %s | State: %s\n", 
                      millis(), healthy ? "YES" : "NO", prod::g_ocppStateMachine.getStateName());
    }

    static unsigned long lastStatusLog = 0;
    if (millis() - lastStatusLog >= 10000) {
        lastStatusLog = millis();
        auto snap = state.snapshot();
        Serial.printf("[OCPP] Status: SM=%s | Tx=%s | Charging=%s | Operative=%d\n",
                     prod::g_ocppStateMachine.getStateName(),
                     snap.transactionActive ? "Active" : "Idle",
                     snap.chargingEnabled ? "ON" : "OFF",
                     isOperative());
    }

    // ═══════════════════════════════════════════════════════════════
    // DATA TRANSFER - Send periodic vehicle info
    // ═══════════════════════════════════════════════════════════════
    // REMOVED: Redundant 10s loop removed. 
    // HardwareService::pollVehicleInfo() handles this with smarter rate-limiting.
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

// RATE-LIMIT: Track whether a VehicleInfo DataTransfer is still pending in the queue
static bool vehicleInfoPending = false;

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

    // RATE-LIMIT: Don't queue if previous VehicleInfo hasn't received a response yet
    if (vehicleInfoPending) {
        Serial.println("[OCPP] ⏳ VehicleInfo skipped — previous still pending in queue");
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

    vehicleInfoPending = true;  // Mark as pending BEFORE queuing

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
            vehicleInfoPending = false;  // Clear pending flag — ready for next send
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
