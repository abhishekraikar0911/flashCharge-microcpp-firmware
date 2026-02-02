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

// Transaction tracking and lock
static unsigned long txStartTime = 0;
static bool transactionLocked = false;
static int localTransactionId = -1;

void ocpp::init()
{
    Serial.println("[OCPP] 🔌 Initializing OCPP...");

    // Wait for WiFi
    uint32_t wifiWaitStart = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (millis() - wifiWaitStart > 30000)
        {
            Serial.println("[OCPP] ❌ WiFi timeout!");
            return;
        }
    }
    Serial.println("[OCPP] ✅ WiFi connected");

    // NOW initialize MicroOCPP FIRST
    Serial.println("[OCPP] 🚀 Calling mocpp_initialize()...");
    mocpp_initialize(
        SECRET_CSMS_URL,
        SECRET_CHARGER_ID,
        SECRET_CHARGER_MODEL,
        SECRET_CHARGER_VENDOR);
    Serial.println("[OCPP] ✅ mocpp_initialize() completed");

    // CRITICAL: Configure all inputs AFTER mocpp_initialize()
    Serial.println("[OCPP] 📋 Registering input callbacks...");
    
    // Energy meter with validation - ALWAYS return non-negative
    setEnergyMeterInput([]() {
        int energyInt = 0;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
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
    Serial.println("[OCPP]   ✓ Energy meter registered");

    // Power meter using terminal values
    setPowerMeterInput([]() {
        if (terminalVolt < 56.0f || terminalVolt > 85.5f) return 0;
        if (terminalCurr < 0.0f || terminalCurr > 300.0f) return 0;
        return (int)(terminalVolt * terminalCurr);
    });
    Serial.println("[OCPP]   ✓ Power meter registered");

    // Plug detection with detailed logging
    setConnectorPluggedInput([]() {
        bool plugged = gunPhysicallyConnected && batteryConnected;
        static bool lastPlugged = false;
        static bool lastGun = false;
        static bool lastBattery = false;
        
        // Log individual component changes
        if (gunPhysicallyConnected != lastGun) {
            Serial.printf("[OCPP]   Gun physical: %s\n", gunPhysicallyConnected ? "CONNECTED" : "DISCONNECTED");
            lastGun = gunPhysicallyConnected;
        }
        if (batteryConnected != lastBattery) {
            Serial.printf("[OCPP]   Battery: %s\n", batteryConnected ? "CONNECTED" : "DISCONNECTED");
            lastBattery = batteryConnected;
        }
        
        // Log combined state change
        if (plugged != lastPlugged) {
            Serial.printf("[OCPP]   ⚡ Plug state: %s (gun=%d, battery=%d)\n", 
                plugged ? "CONNECTED" : "DISCONNECTED",
                gunPhysicallyConnected, batteryConnected);
            lastPlugged = plugged;
        }
        return plugged;
    });
    Serial.println("[OCPP]   ✓ Plug detection registered");

    // EVSE ready (charger module healthy)
    setEvseReadyInput([]() {
        bool healthy = isChargerModuleHealthy();
        static bool lastHealthy = true;
        if (healthy != lastHealthy) {
            Serial.printf("[OCPP]   EVSE ready: %s\n", healthy ? "YES" : "NO");
            lastHealthy = healthy;
        }
        return healthy;
    });
    Serial.printf("[OCPP]   ✓ EVSE ready registered (initial: %s)\n", 
                  isChargerModuleHealthy() ? "HEALTHY" : "OFFLINE");

    // EV ready to charge with detailed logging
    setEvReadyInput([]() {
        bool ready = batteryConnected && terminalVolt > 56.0f;
        static bool lastReady = false;
        static float lastVolt = 0.0f;
        
        // Log voltage changes
        if (abs(terminalVolt - lastVolt) > 5.0f) {
            Serial.printf("[OCPP]   Terminal voltage: %.1fV\n", terminalVolt);
            lastVolt = terminalVolt;
        }
        
        // Log ready state changes
        if (ready != lastReady) {
            Serial.printf("[OCPP]   ⚡ EV ready: %s (battery=%d, V=%.1fV)\n", 
                ready ? "YES" : "NO", batteryConnected, terminalVolt);
            lastReady = ready;
        }
        return ready;
    });
    Serial.println("[OCPP]   ✓ EV ready registered");

    // MeterValues - OCPP 1.6 standard measurands only
    addMeterValueInput([]() -> float { return socPercent; }, "SoC", "Percent", nullptr, nullptr, 1);
    addMeterValueInput([]() -> float { return terminalVolt; }, "Voltage", "V", nullptr, nullptr, 1);
    addMeterValueInput([]() -> float { return terminalCurr; }, "Current.Import", "A", nullptr, nullptr, 1);
    addMeterValueInput([]() -> float { return BMS_Imax; }, "Current.Offered", "A", nullptr, nullptr, 1);
    addMeterValueInput([]() -> float { return chargerTemp; }, "Temperature", "Celsius", nullptr, nullptr, 1);
    Serial.println("[OCPP]   ✓ MeterValues registered (standard measurands)");

    // Configure intervals - Clock-aligned sampling for immediate first sample
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

    // Transaction notifications
    static bool sessionSummarySent = false;
    setTxNotificationOutput([](MicroOcpp::Transaction *tx, TxNotification notification) {
        if (notification == TxNotification_RemoteStart) {
            Serial.println("\n[OCPP] 📥 RemoteStart received");
            
            if (!isChargerModuleHealthy()) {
                Serial.println("[OCPP] ❌ REJECTING: Charger module OFFLINE");
                return;
            }
            
            // Check if already in transaction
            if (transactionLocked) {
                Serial.println("[OCPP] ⚠️  RemoteStart rejected - transaction already active");
                return;
            }
            
            Serial.println("[OCPP] ✅ RemoteStart accepted");
            remoteStartAccepted = true;
        } else if (notification == TxNotification_StartTx) {
            if (!isChargerModuleHealthy()) {
                Serial.println("[OCPP] ❌ Transaction started but charger OFFLINE - not enabling charging");
                return;
            }
            
            // UNCONDITIONAL: Enable charging when StartTransaction accepted
            // txId is metadata only - not required for authorization
            int txId = tx ? tx->getTransactionId() : -1;
            localTransactionId = txId;
            activeTransactionId = txId;
            transactionActive = true;
            transactionLocked = true;
            chargingEnabled = true;
            txStartTime = millis();
            sessionSummarySent = false;
            
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
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
            chargingEnabled = false;
            Serial.println("[OCPP] ⏹️  Charging disabled");
        } else if (notification == TxNotification_StopTx) {
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
    Serial.println("[OCPP]   ✓ Transaction callbacks registered");

    // Configure OTA firmware updates
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

    // Check if operative (will be false until BootNotification accepted)
    bool operative = isOperative();
    Serial.printf("[OCPP] 🔍 isOperative() = %s (will become TRUE after BootNotification)\n", 
                  operative ? "TRUE" : "FALSE");

    Serial.println("[OCPP] ✅ OCPP initialization complete");
    Serial.println("[OCPP] ⏳ Waiting for WebSocket connection and BootNotification...");
    
    // CRITICAL: Set flag to allow loop() to access connector 1
    ocppInitialized = true;
}

void ocpp::poll()
{
    mocpp_loop();
    
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
    bool operative = isOperative();
    
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
    bool operative = isOperative();
    
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

    Serial.printf("\n[OCPP] 📤 Sending VehicleInfo:\n");
    Serial.printf("  SOC=%.1f%% | Model=%s | Range=%.1fkm | MaxI=%.1fA\n", soc, modelName, range, maxCurrent);

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
            
            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(768));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"] = "RivotMotors";
            payload["messageId"] = "VehicleInfo";
            payload["data"] = dataStr;
            return doc;
        },
        [](JsonObject response) {
            const char* status = response["status"] | "Unknown";
            Serial.printf("[OCPP] ✅ VehicleInfo response: %s\n\n", status);
        }
    );
}

void ocpp::sendSessionSummary(float finalSoc, float energyDelivered, float duration)
{
    if (!isOperative()) {
        return;
    }

    Serial.printf("\n[OCPP] 📊 Sending SessionSummary:\n");
    Serial.printf("  FinalSOC=%.1f%% | Energy=%.2fWh | Duration=%.1fmin\n\n", 
                  finalSoc, energyDelivered, duration);

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
    if (!isOperative()) {
        return;
    }

    Serial.printf("[OCPP] 🚨 Sending BMSAlert: %s - %s\n", alertType, message);

    sendRequest("DataTransfer",
        [alertType, message]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            MicroOcpp::JsonDoc dataDoc(256);
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
