#include <Arduino.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <MicroOcpp.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Model/Transactions/Transaction.h>
#include <MicroOcpp/Model/ConnectorBase/Connector.h>
#include "../include/secrets.h"
#include "../include/header.h"
#include "../include/ocpp/ocpp_client.h"
#include "../include/production_config.h"
#include "../include/wifi_manager.h"
#include "../include/health_monitor.h"
#include "../include/ocpp_state_machine.h"
#include "../include/security_manager.h"
#include "../include/modules/ota_manager.h"
#include "../include/drivers/can_twai_driver.h"
#include "../include/drivers/can_mcp2515_driver.h"
#include "../include/config/version.h"

using namespace prod;

// OCPP Task handle
static TaskHandle_t ocppTaskHandle = nullptr;

// OCPP task (runs on Core 0) - Uses ocpp_manager for all OCPP logic
void ocppTask(void *pvParameters)
{
    Serial.println("[OCPP] 🔌 OCPP Task started");

    // Initialize OCPP (waits for WiFi internally)
    ocpp::init();

    // Main OCPP loop
    for (;;)
    {
        ocpp::poll();
        g_healthMonitor.feed();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.printf("  ESP32 OCPP EVSE Controller - v%s\n", FIRMWARE_VERSION);
    Serial.println("  Production-Ready Edition");
    Serial.printf("  Build: %s\n", BUILD_TIMESTAMP);
    Serial.printf("  Charger ID: %s\n", SECRET_CHARGER_ID);
    Serial.println("========================================\n");

    // Initialize health monitor FIRST
    g_healthMonitor.init();

    // Initialize global variables and mutexes
    initGlobals();

    // CRITICAL FIX #1: Initialize NVS (flash storage) FIRST
    Serial.println("[System] 💾 Initializing NVS Flash...");
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        Serial.println("[System] ⚠️  NVS partition needs erasing...");
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }

    if (nvs_ret == ESP_OK)
    {
        Serial.println("[System] ✅ NVS Flash initialized");
    }
    else
    {
        Serial.printf("[System] ❌ NVS Flash init failed: 0x%X\n", nvs_ret);
    }

    // Record startup
    Serial.printf("[System] Reboot count: %u\n", g_persistence.getRebootCount());
    g_persistence.recordRebootCount();

    // Initialize CAN buses
    Serial.println("[System] 🚌 Initializing dual CAN buses...");
    
    // CAN1 - ISO1050 (TWAI) - Charger Module
    if (!CAN_TWAI::init())
    {
        Serial.println("[System] ❌ CAN1 (Charger) init failed!");
    }
    
    // CAN2 - MCP2515 (SPI) - Vehicle BMS
    if (!CAN_MCP2515::init())
    {
        Serial.println("[System] ❌ CAN2 (BMS) init failed!");
    }

    // Create CAN1 RX task (Charger) - HIGH PRIORITY (priority 8)
    TaskHandle_t can1RxHandle = nullptr;
    BaseType_t can1RxResult = xTaskCreatePinnedToCore(
        can1_rx_task,
        "CAN1_RX",
        6144,
        nullptr,
        8,
        &can1RxHandle,
        1);
    
    if (can1RxResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create CAN1_RX task!");
    }
    else
    {
        g_healthMonitor.addTaskToWatchdog(can1RxHandle, "CAN1_RX");
    }

    // Create CAN2 RX task (BMS) - HIGH PRIORITY (priority 8)
    TaskHandle_t can2RxHandle = nullptr;
    BaseType_t can2RxResult = xTaskCreatePinnedToCore(
        can2_rx_task,
        "CAN2_RX",
        6144,
        nullptr,
        8,
        &can2RxHandle,
        1);
    
    if (can2RxResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create CAN2_RX task!");
    }
    else
    {
        g_healthMonitor.addTaskToWatchdog(can2RxHandle, "CAN2_RX");
    }

    // Create charger communication task - HIGH PRIORITY (priority 7)
    TaskHandle_t chargerHandle = nullptr;
    BaseType_t chargerResult = xTaskCreatePinnedToCore(
        chargerCommTask,
        "CHARGER_COMM",
        6144, // Increased from 4096 to prevent stack overflow
        nullptr,
        7, // Increased from 4 - safety-critical
        &chargerHandle,
        1);
    
    if (chargerResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create CHARGER_COMM task!");
    }
    else
    {
        // SAFETY: Add to watchdog
        g_healthMonitor.addTaskToWatchdog(chargerHandle, "CHARGER_COMM");
    }

    // FIX #3: Create OCPP task on Core 0 - MEDIUM PRIORITY (priority 3)
    BaseType_t ocppResult = xTaskCreatePinnedToCore(
        ocppTask,
        "OCPP_LOOP",
        10240, // Increased from 8192 for WebSocket + TLS overhead
        nullptr,
        3, // Lower priority than CAN, but dedicated to avoid blocking
        &ocppTaskHandle,
        0); // Core 0 for OCPP
    
    if (ocppResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create OCPP_LOOP task!");
    }

    // Create UI task for serial menu - LOWEST PRIORITY
    BaseType_t uiResult = xTaskCreatePinnedToCore(
        [](void *arg)
        {
            static bool menuPrinted = false;
            while (true)
            {
                processSerialInput();
                if (!menuPrinted)
                {
                    printMenu();
                    menuPrinted = true;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        },
        "UI_TASK",
        4096,
        nullptr,
        2,
        nullptr,
        1);
    
    if (uiResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create UI_TASK!");
    }

    // Initialize WiFi with auto-reconnect
    Serial.println("[System] 📡 Initializing WiFi...");
    g_wifiManager.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);

    // Initialize security (TLS/WSS)
    Serial.println("[System] 🔒 Initializing security...");
    g_securityManager.init();

    // Initialize OTA manager
    Serial.println("[System] 🔄 Initializing OTA...");
    g_otaManager.init();

    // For production with valid SSL certificate, uncomment:
    // const char* ROOT_CA = "-----BEGIN CERTIFICATE-----\n..."; 
    // g_securityManager.loadRootCA(ROOT_CA);
    // g_securityManager.enableCertificateVerification();
    
    // For now, using setInsecure() to accept self-signed certificates
    Serial.println("[System] ⚠️  Using insecure mode for WSS (accepts any certificate)");

    // NOTE: OCPP initialization now happens in ocppTask after WiFi is ready
    // This prevents the race condition that was causing crashes
    // Connector plug detection is configured in ocpp_manager.cpp

    // Initialize OCPP state machine
    g_ocppStateMachine.init();

    Serial.println("[System] ✅ All systems initialized!\n");
}

void loop()
{
    // CRITICAL: Wait for OCPP initialization before accessing connector 1
    if (!ocppInitialized) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }
    
    // CRITICAL: Feed watchdog for loop task
    g_healthMonitor.feed();
    
    // FIX #5: Keep loop() lightweight - OCPP runs in its own task now
    // Poll WiFi (auto-reconnect if needed)
    g_wifiManager.poll();

    // Poll health monitor (check timeouts, etc.)
    g_healthMonitor.poll();

    // Poll OCPP state machine (deadlock prevention, timeout handling)
    g_ocppStateMachine.poll();

    // HYBRID PLUG DISCONNECT DETECTION (Option 4)
    static unsigned long lastPlugCheck = 0;
    static unsigned long zeroCurrentStart = 0;
    static float lastVoltageCheck = 0.0f;
    static unsigned long lastVoltageTime = 0;
    
    if (millis() - lastPlugCheck >= 500)
    {
        bool shouldDisconnect = false;
        
        // Method 1: BMS timeout (3 seconds) - Most reliable
        if ((gunPhysicallyConnected || batteryConnected) && (millis() - lastBMS > 3000))
        {
            Serial.println("[PLUG] 🔌 Disconnected: BMS timeout (3s)");
            shouldDisconnect = true;
        }
        
        // Method 2: Zero current timeout - ONLY during active charging
        if (transactionActive && chargingEnabled && 
            terminalVolt > 56.0f && terminalCurr < 0.5f)
        {
            if (zeroCurrentStart == 0)
            {
                zeroCurrentStart = millis();
            }
            else if (millis() - zeroCurrentStart > 5000)
            {
                Serial.println("[PLUG] 🔌 Disconnected: Zero current during charging (5s)");
                shouldDisconnect = true;
            }
        }
        else
        {
            zeroCurrentStart = 0;
        }
        
        // Method 3: Voltage drop rate (>2V/s)
        if (terminalVolt > 10.0f)
        {
            if (lastVoltageTime > 0)
            {
                float deltaV = lastVoltageCheck - terminalVolt;
                float deltaT = (millis() - lastVoltageTime) / 1000.0f;
                if (deltaT > 0.5f && (deltaV / deltaT) > 2.0f)
                {
                    Serial.printf("[PLUG] 🔌 Disconnected: Fast voltage drop (%.1fV/s)\n", deltaV / deltaT);
                    shouldDisconnect = true;
                }
            }
            lastVoltageCheck = terminalVolt;
            lastVoltageTime = millis();
        }
        else
        {
            // Reset tracking when voltage too low
            lastVoltageCheck = 0.0f;
            lastVoltageTime = 0;
        }
        
        // Execute disconnect
        if (shouldDisconnect && (gunPhysicallyConnected || batteryConnected))
        {
            gunPhysicallyConnected = false;
            batteryConnected = false;
            zeroCurrentStart = 0;
            Serial.println("[PLUG] ✅ Status: DISCONNECTED");
            
            // Only stop transaction if one is actually running
            if (transactionActive && isTransactionRunning(1)) {
                Serial.printf("[PLUG] 🛑 Stopping transaction due to EV disconnect (txId=%d)\n", activeTransactionId);
                endTransaction(nullptr, "EVDisconnected");
            } else {
                Serial.println("[PLUG] ℹ️  No active transaction - just updating status to Available");
            }
        }
        
        lastPlugCheck = millis();
    }

    // Monitor plug connection state changes
    static bool lastPlugState = false;
    bool currentPlugState = (gunPhysicallyConnected && batteryConnected);
    
    if (currentPlugState != lastPlugState)
    {
        if (currentPlugState)
        {
            Serial.println("[PLUG] 🔌 Gun plugged, vehicle detected");
        }
        lastPlugState = currentPlugState;
    }
    
    // Send VehicleInfo for pay-and-charge: User needs vehicle data BEFORE RemoteStart
    // to calculate charging cost and choose charging options
    static unsigned long lastVehicleInfoSent = 0;
    static bool firstSendDone = false;
    
    // Send when EV connected in Preparing state (waiting for user to start charging)
    // Stop when transaction starts (RemoteStart accepted)
    bool shouldSendVehicleInfo = (
        batteryConnected && 
        gunPhysicallyConnected && 
        !transactionActive &&  // No transaction started yet
        !isTransactionRunning(1) &&  // Double-check no active transaction
        BMS_Imax > 0.0f && 
        terminalVolt > 56.0f &&
        socPercent > 0.0f  // Valid SOC data
    );
    
    if (shouldSendVehicleInfo)
    {
        // Fast updates: 3s first time, then 5s interval for real-time data
        unsigned long interval = firstSendDone ? 5000 : 3000;
        
        if (millis() - lastVehicleInfoSent >= interval)
        {
            ocpp::sendVehicleInfo(socPercent, BMS_Imax, terminalVolt, terminalCurr, chargerTemp, vehicleModel, rangeKm);
            lastVehicleInfoSent = millis();
            firstSendDone = true;
        }
    }
    else
    {
        // Reset when conditions not met
        if (transactionActive || isTransactionRunning(1) || !batteryConnected) {
            lastVehicleInfoSent = 0;
            firstSendDone = false;
        }
    }

    // SAFETY: Monitor BMS charging permission (100ms check)
    static bool lastBmsSafeToCharge = false;
    static unsigned long lastBmsSafetyCheck = 0;
    
    if (millis() - lastBmsSafetyCheck >= 100)
    {
        if (bmsSafeToCharge != lastBmsSafeToCharge)
        {
            if (!bmsSafeToCharge)
            {
                Serial.println("[SAFETY] 🚨 BMS CHARGING DISABLED!");
                
                if (transactionActive && isTransactionRunning(1))
                {
                    Serial.printf("[SAFETY] 🚨 EMERGENCY STOP - BMS switched OFF during charging (txId=%d)\n", activeTransactionId);
                    ocpp::sendBMSAlert("BMS_EMERGENCY_STOP", "BMS disabled charging during transaction");
                    endTransaction(nullptr, "EmergencyStop");
                }
                else
                {
                    ocpp::sendBMSAlert("BMS_CHARGING_DISABLED", "BMS not ready for charging");
                }
            }
            else
            {
                Serial.println("[SAFETY] ✅ BMS charging enabled");
                ocpp::sendBMSAlert("BMS_CHARGING_ENABLED", "BMS ready for charging");
            }
            lastBmsSafeToCharge = bmsSafeToCharge;
        }
        
        lastBmsSafetyCheck = millis();
    }

    // Accumulate energy when charging - use terminal values with validation
    static unsigned long lastEnergyTime = millis();
    static unsigned long lastChargerHealthCheck = 0;
    
    if (millis() - lastChargerHealthCheck >= 2000)
    {
        bool chargerHealthy = isChargerModuleHealthy();
        static bool lastChargerHealthy = false;
        static bool firstCheck = true;
        
        // Detect health state change (skip logging on first check)
        if (!firstCheck && chargerHealthy != lastChargerHealthy)
        {
            if (!chargerHealthy)
            {
                Serial.println("\n[CHARGER] ❌ CRITICAL: Charger module communication lost!");
                Serial.println("[CHARGER] ⚠️  Possible causes:");
                Serial.println("[CHARGER]    - Charger PCB powered OFF");
                Serial.println("[CHARGER]    - CAN bus disconnected");
                Serial.println("[CHARGER]    - Hardware fault");
                Serial.printf("[CHARGER] 🔍 Last messages: TermPower=%lums TermStatus=%lums Heartbeat=%lums ago\n",
                             millis() - lastTerminalPower,
                             millis() - lastTerminalStatus,
                             millis() - lastHeartbeat);
                
                // CRITICAL: Force connector to Unavailable
                Serial.println("[OCPP] 🚨 Forcing connector to Unavailable");
                // MicroOcpp will automatically update based on setEvseReadyInput
            }
            else
            {
                Serial.println("\n[CHARGER] ✅ Charger module communication restored!");
                Serial.println("[OCPP] ✅ Connector now Available");
            }
            
            lastChargerHealthy = chargerHealthy;
        }
        
        if (firstCheck)
        {
            lastChargerHealthy = chargerHealthy;
            firstCheck = false;
        }
        
        // If charging enabled but charger offline, stop transaction
        if (chargingEnabled && !chargerHealthy)
        {
            if (transactionActive && isTransactionRunning(1))
            {
                Serial.printf("[CHARGER] 🚨 SAFETY: Charger offline during transaction (txId=%d)\n", activeTransactionId);
                Serial.println("[CHARGER] 🔍 Check: CAN bus, charger power, hardware connection");
                endTransaction(nullptr, "EVSEFailure");
            }
        }
        
        lastChargerHealthCheck = millis();
    }
    
    // FINAL FIX: HARD GATE without txId check
    // Golden Rule: OCPP authorization comes from StartTransaction acceptance, NOT txId
    bool ocppAllows = ocppPermitsCharge(1);
    bool canCharge = (
        ocppAllows &&           // OCPP must permit FIRST
        transactionActive &&    // Transaction started
        chargingEnabled         // Hardware enabled by OCPP callback
    );
    
    // Only accumulate energy if HARD GATE is open AND hardware conditions valid
    if (canCharge && 
        terminalVolt > 56.0f && terminalVolt < 85.5f && 
        terminalCurr > 0.0f && terminalCurr < 300.0f)
    {
        unsigned long now = millis();
        float dt_hours = (now - lastEnergyTime) / 3600000.0f;
        float energyDelta = terminalVolt * terminalCurr * dt_hours;
        
        // Only add positive energy increments with mutex protection
        if (energyDelta > 0.0f && energyDelta < 1000.0f) {
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                energyWh += energyDelta;
                xSemaphoreGive(dataMutex);
            }
        }
        lastEnergyTime = now;
    }
    else
    {
        lastEnergyTime = millis();
    }

    // Debug output every 10 seconds - display terminal values
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug >= 10000)
    {
        // Check OCPP connection and transaction status
        bool ocppConnected = ocpp::isConnected();
        bool txActive = isTransactionActive(1);  // Preparing or running
        bool txRunning = isTransactionRunning(1);  // Actively running
        bool chargerHealthy = isChargerModuleHealthy();
        bool ocppPermits = ocppPermitsCharge(1);

        Serial.printf("\n[Status] Uptime: %us | WiFi: %s | OCPP: %s | State: %s\n",
                      g_healthMonitor.getUptimeSeconds(),
                      g_wifiManager.isConnected() ? "✅" : "❌",
                      ocppConnected ? "Connected" : "Disconnected",
                      g_ocppStateMachine.getStateName());
        Serial.printf("[Metrics] V=%.1fV I=%.1fA SOC=%.1f%% Range=%.1fkm Temp=%.1f°C Energy=%.2fWh (meter=%d)\n",
                      terminalVolt, terminalCurr, socPercent, rangeKm, chargerTemp, energyWh, (int)energyWh);
        
        const char* modelName = "Unknown";
        if (vehicleModel == 1) modelName = "Classic";
        else if (vehicleModel == 2) modelName = "Pro";
        else if (vehicleModel == 3) modelName = "Max";
        
        Serial.printf("[Vehicle] Model=%s | Capacity=%.0fAh | BMS_Imax=%.1fA\n",
                      modelName, batteryAh, BMS_Imax);
        Serial.printf("[Charger] Module=%s | Enabled=%s | TX=%s/%s | Current=%s | OCPP=%s\n",
                      chargerHealthy ? "ONLINE" : "OFFLINE",
                      chargingEnabled ? "YES" : "NO",
                      txActive ? "ACTIVE" : "IDLE",
                      txRunning ? "RUNNING" : "STOPPED",
                      (terminalCurr > 1.0f) ? "FLOWING" : "ZERO",
                      ocppPermits ? "PERMITS" : "BLOCKS");
        lastDebug = millis();
    }

    // FIX #5: Yield to prevent watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(10));
}