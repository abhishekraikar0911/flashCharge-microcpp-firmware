#include <Arduino.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <MicroOcpp.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Model/Transactions/Transaction.h>
#include "../include/secrets.h"
#include "../include/header.h"
#include "../include/ocpp/ocpp_client.h"
#include "../include/production_config.h"
#include "../include/wifi_manager.h"
#include "../include/health_monitor.h"
#include "../include/ocpp_state_machine.h"
#include "../include/security_manager.h"
#include "../include/drivers/can_driver.h"

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

// OCPP task (runs on Core 0) - FIXED: Wait for WiFi and initialize OCPP properly
void ocppTask(void *pvParameters)
{
    Serial.println("[OCPP] 🔌 OCPP Task started, waiting for WiFi...");

    // CRITICAL FIX #1: Wait for WiFi to be fully connected
    uint32_t wifiWaitStart = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        Serial.print(".");

        // Safety timeout - 30 seconds
        if (millis() - wifiWaitStart > 30000)
        {
            Serial.println("\n[OCPP] ❌ WiFi timeout! Rebooting...");
            esp_restart();
        }
    }
    Serial.println("\n[OCPP] ✅ WiFi connected, initializing OCPP...");

    // CRITICAL FIX #2: Initialize OCPP with plain WS (non-secure)
    static bool ocppInitialized = false;
    if (!ocppInitialized)
    {
        mocpp_initialize(
            "ws://ocpp.rivotmotors.com:8080/steve/websocket/CentralSystemService/RIVOT_100A_01",
            "RIVOT_100A_01",
            "Rivot Charger",
            "Rivot Motors");

        // CRITICAL FIX: Set up meter inputs with validation
        // Energy meter - ensure only non-negative values are sent
        setEnergyMeterInput([]()
                            {
                                extern float energyWh;
                                // Clamp to prevent invalid values
                                if (energyWh < 0.0f)
                                {
                                    Serial.printf("[OCPP] ⚠️  Energy meter invalid (%.1f), clamping to 0\n", energyWh);
                                    energyWh = 0.0f;
                                }
                                return (int)energyWh; // Convert to int for MicroOcpp
                            },
                            1);

        // Power meter
        setPowerMeterInput([]()
                           {
            extern float chargerVolt, chargerCurr;
            return (int)(chargerVolt * chargerCurr); },
                           1);

        // Plug detection
        setConnectorPluggedInput([]()
                                 {
            extern bool gunPhysicallyConnected;
            return gunPhysicallyConnected; },
                                 1);

        // Battery ready state
        setEvseReadyInput([]()
                          {
            extern bool batteryConnected, gunPhysicallyConnected;
            return batteryConnected && gunPhysicallyConnected; },
                          1);

        // Remote start/stop handlers
        setTxNotificationOutput([](MicroOcpp::Transaction *tx, TxNotification notification)
                                {
            if (notification == TxNotification_RemoteStart || notification == TxNotification_StartTx)
            {
                extern bool chargingEnabled;
                chargingEnabled = true;
                Serial.println("[OCPP] ▶️  Charging enabled (RemoteStart/StartTx)");
            }
            else if (notification == TxNotification_StopTx || notification == TxNotification_RemoteStop)
            {
                extern bool chargingEnabled;
                chargingEnabled = false;
                Serial.println("[OCPP] ⏹️  Charging disabled (StopTx/RemoteStop)");
            } },
                                1);

        ocppInitialized = true;
        Serial.println("[OCPP] ✅ OCPP initialized, entering main loop...");
    }

    // CRITICAL FIX #3: NOW start the OCPP loop
    for (;;)
    {
        // Poll OCPP
        mocpp_loop();

        // Feed watchdog from OCPP task
        g_healthMonitor.feed();

        // Give CPU time to breathe
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.println("  ESP32 OCPP EVSE Controller - v2.0");
    Serial.println("  Production-Ready Edition");
    Serial.println("========================================\n");

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
    g_persistence.recordRebootCount();
    Serial.printf("[System] Reboot count: %u\n", g_persistence.getRebootCount());

    // Initialize CAN bus
    if (!CAN::init())
    {
        Serial.println("[System] ❌ CAN init failed!");
    }

    // FIX #4: Increase CAN task priority (safety-critical)
    // Create CAN RX task - HIGH PRIORITY (priority 8)
    xTaskCreatePinnedToCore(
        can_rx_task,
        "CAN_RX",
        4096,
        nullptr,
        8, // Increased from 5 - higher priority for safety
        nullptr,
        1);

    // Create charger communication task - HIGH PRIORITY (priority 7)
    xTaskCreatePinnedToCore(
        chargerCommTask,
        "CHARGER_COMM",
        4096,
        nullptr,
        7, // Increased from 4 - safety-critical
        nullptr,
        1);

    // FIX #3: Create OCPP task on Core 0 - MEDIUM PRIORITY (priority 3)
    xTaskCreatePinnedToCore(
        ocppTask,
        "OCPP_LOOP",
        8192, // Larger stack for WebSocket handling
        nullptr,
        3, // Lower priority than CAN, but dedicated to avoid blocking
        &ocppTaskHandle,
        0); // Core 0 for OCPP

    // Create UI task for serial menu - LOWEST PRIORITY
    xTaskCreatePinnedToCore(
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
    // FIX #5: Keep loop() lightweight - OCPP runs in its own task now
    // Poll WiFi (auto-reconnect if needed)
    g_wifiManager.poll();

    // Poll health monitor (check timeouts, etc.)
    g_healthMonitor.poll();

    // Poll OCPP state machine (deadlock prevention, timeout handling)
    g_ocppStateMachine.poll();

    // Accumulate energy when charging
    static unsigned long lastEnergyTime = millis();
    if (chargingEnabled && chargerVolt > 0 && chargerCurr > 0)
    {
        unsigned long now = millis();
        float dt_hours = (now - lastEnergyTime) / 3600000.0f;
        energyWh += chargerVolt * chargerCurr * dt_hours;
        lastEnergyTime = now;
    }
    else
    {
        lastEnergyTime = millis();
    }

    // Debug output every 10 seconds
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug >= 10000)
    {
        // Check OCPP connection status (not transaction status)
        bool ocppConnected = isOperative();

        Serial.printf("\n[Status] Uptime: %us | WiFi: %s | OCPP: %s | State: %s\n",
                      g_healthMonitor.getUptimeSeconds(),
                      g_wifiManager.isConnected() ? "✅" : "❌",
                      ocppConnected ? "Connected" : "Disconnected",
                      g_ocppStateMachine.getStateName());
        Serial.printf("[Metrics] V=%.1fV I=%.1fA SOC=%d%% Energy=%.2fWh\n",
                      chargerVolt, chargerCurr, (int)socPercent, energyWh);
        lastDebug = millis();
    }

    // FIX #5: Yield to prevent watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(10));
}