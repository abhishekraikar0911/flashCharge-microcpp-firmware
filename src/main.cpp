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
#include "../include/drivers/can_driver.h"
#include "../include/config/version.h"

using namespace prod;

// OCPP Task handle
static TaskHandle_t ocppTaskHandle = nullptr;

// Plug sensor function - customize based on your hardware
bool getPlugState()
{
    // Use the gunPhysicallyConnected signal from CAN bus
    extern bool gunPhysicallyConnected;
    return gunPhysicallyConnected;
}

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

    // Initialize CAN bus
    if (!CAN::init())
    {
        Serial.println("[System] ❌ CAN init failed!");
    }

    // FIX #4: Increase CAN task priority and stack size (safety-critical)
    // Create CAN RX task - HIGH PRIORITY (priority 8)
    TaskHandle_t canRxHandle = nullptr;
    BaseType_t canRxResult = xTaskCreatePinnedToCore(
        can_rx_task,
        "CAN_RX",
        6144, // Increased from 4096 to prevent stack overflow
        nullptr,
        8, // Increased from 5 - higher priority for safety
        &canRxHandle,
        1);
    
    if (canRxResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create CAN_RX task!");
    }
    else
    {
        // SAFETY: Add to watchdog
        g_healthMonitor.addTaskToWatchdog(canRxHandle, "CAN_RX");
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

    // For production with valid SSL certificate, uncomment:
    // const char* ROOT_CA = "-----BEGIN CERTIFICATE-----\n..."; 
    // g_securityManager.loadRootCA(ROOT_CA);
    // g_securityManager.enableCertificateVerification();
    
    // For now, using setInsecure() to accept self-signed certificates
    Serial.println("[System] ⚠️  Using insecure mode for WSS (accepts any certificate)");

    // NOTE: OCPP initialization now happens in ocppTask after WiFi is ready
    // This prevents the race condition that was causing crashes

    // FIX #3: Set plug sensor for Finishing -> Available transition
    setConnectorPluggedInput([]()
                             { return getPlugState(); });

    // Initialize OCPP state machine
    g_ocppStateMachine.init();

    Serial.println("[System] ✅ All systems initialized!\n");
}

void loop()
{
    // CRITICAL: Feed watchdog for loop task
    g_healthMonitor.feed();
    
    // FIX #5: Keep loop() lightweight - OCPP runs in its own task now
    // Poll WiFi (auto-reconnect if needed)
    g_wifiManager.poll();

    // Poll health monitor (check timeouts, etc.)
    g_healthMonitor.poll();

    // Poll OCPP state machine (deadlock prevention, timeout handling)
    g_ocppStateMachine.poll();

    // Monitor plug state and send status updates
    static bool lastPlugState = false;
    bool currentPlugState = getPlugState();
    if (currentPlugState != lastPlugState)
    {
        lastPlugState = currentPlugState;
        if (!currentPlugState)
        {
            // Gun unplugged - send Available status
            Serial.println("[OCPP] 🔌 Gun unplugged, sending Available status");
            // MicroOcpp will automatically send StatusNotification when plug state changes
        }
    }

    // Accumulate energy when charging - use terminal values with validation
    static unsigned long lastEnergyTime = millis();
    static unsigned long lastChargerHealthCheck = 0;
    
    // Check if OCPP permits charging (Smart Charging limits, availability)
    bool ocppAllowsCharge = ocppPermitsCharge(1);
    
    // PRODUCTION FIX: Check charger module health every 2 seconds
    if (millis() - lastChargerHealthCheck >= 2000)
    {
        bool chargerHealthy = isChargerModuleHealthy();
        static bool lastChargerHealthy = true; // Track previous state
        
        // Detect health state change
        if (chargerHealthy != lastChargerHealthy)
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
                
                // Send OCPP StatusNotification: Faulted
                Serial.println("[OCPP] 🚨 Sending StatusNotification: Faulted");
                // MicroOcpp will send this via state machine
            }
            else
            {
                Serial.println("\n[CHARGER] ✅ Charger module communication restored!");
                Serial.println("[OCPP] ✅ Sending StatusNotification: Available");
            }
            
            lastChargerHealthy = chargerHealthy;
        }
        
        // If charging enabled but charger offline, stop transaction
        if (chargingEnabled && !chargerHealthy)
        {
            if (isTransactionRunning(1))
            {
                Serial.println("[CHARGER] 🛑 Auto-stopping transaction due to charger module offline");
                endTransaction();
            }
        }
        
        // If OCPP doesn't permit charging, disable it
        if (chargingEnabled && !ocppAllowsCharge)
        {
            Serial.println("[OCPP] ⚠️  OCPP does not permit charging (Smart Charging limit or unavailable)");
            chargingEnabled = false;
        }
        
        lastChargerHealthCheck = millis();
    }
    
    // Only accumulate energy if OCPP permits and conditions are valid
    if (chargingEnabled && ocppAllowsCharge && 
        terminalVolt > 56.0f && terminalVolt < 85.5f && 
        terminalCurr > 0.0f && terminalCurr < 300.0f)
    {
        unsigned long now = millis();
        float dt_hours = (now - lastEnergyTime) / 3600000.0f;
        energyWh += terminalVolt * terminalCurr * dt_hours;
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
        bool ocppAllows = ocppPermitsCharge(1);

        Serial.printf("\n[Status] Uptime: %us | WiFi: %s | OCPP: %s | State: %s\n",
                      g_healthMonitor.getUptimeSeconds(),
                      g_wifiManager.isConnected() ? "✅" : "❌",
                      ocppConnected ? "Connected" : "Disconnected",
                      g_ocppStateMachine.getStateName());
        Serial.printf("[Metrics] V=%.1fV I=%.1fA SOC=%.1f%% Range=%.1fkm Temp=%.1f°C Energy=%.2fWh\n",
                      terminalVolt, terminalCurr, socPercent, rangeKm, chargerTemp, energyWh);
        
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
                      ocppAllows ? "PERMITS" : "BLOCKS");
        lastDebug = millis();
    }

    // FIX #5: Yield to prevent watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(10));
}