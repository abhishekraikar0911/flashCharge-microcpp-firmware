#include <Arduino.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <MicroOcpp.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Model/Transactions/Transaction.h>
#include <MicroOcpp/Model/ConnectorBase/Connector.h>
#include "../include/secrets.h"
#include "../include/header.h"
#include "../include/debug_logger.h"
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
#include "../include/config/hardware.h"

extern void processDebugCommand(char c);

using namespace prod;

// OCPP Task handle
static TaskHandle_t ocppTaskHandle = nullptr;

// OCPP task (runs on Core 0) - Uses ocpp_manager for all OCPP logic
void ocppTask(void *pvParameters)
{
    Serial.println("[OCPP] OCPP Task started");

    uint32_t backoffMs = 5000;
    uint32_t nextAttemptMs = 0;

    // Main OCPP loop
    for (;;)
    {
        if (!ocppInitialized)
        {
            if (WiFi.status() == WL_CONNECTED && (int32_t)(millis() - nextAttemptMs) >= 0)
            {
                Serial.printf("[OCPP] Init attempt (backoff %u ms)\n", backoffMs);
                if (ocpp::init())
                {
                    backoffMs = 5000;
                    nextAttemptMs = 0;
                }
                else
                {
                    nextAttemptMs = millis() + backoffMs;
                    backoffMs = (backoffMs < 60000) ? (backoffMs * 2) : 60000;
                }
            }

            g_healthMonitor.feed();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

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
    Serial.printf("  StationId: %s\n", SECRET_CHARGER_ID);
    Serial.printf("  CSMS: %s:%d\n", SECRET_CSMS_HOST, SECRET_CSMS_PORT);
    Serial.println("========================================");
    Serial.println("\n🔍 DEBUG: Configuration Verification");
    Serial.printf("  CSMS_HOST = %s\n", SECRET_CSMS_HOST);
    Serial.printf("  CSMS_PORT = %d\n", SECRET_CSMS_PORT);
    Serial.printf("  CSMS_URL  = %s\n", SECRET_CSMS_URL);
    Serial.printf("  CHARGER_ID = %s\n", SECRET_CHARGER_ID);
    Serial.println("========================================\n");

    // Log reset reason
    esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.printf("[System] Reset reason: %d ", reset_reason);
    switch (reset_reason) {
        case ESP_RST_POWERON: Serial.println("(Power-on reset)"); break;
        case ESP_RST_EXT:     Serial.println("(External pin reset)"); break;
        case ESP_RST_SW:      Serial.println("(Software reset)"); break;
        case ESP_RST_PANIC:   Serial.println("(Software panic reset) ❌ CRASH DETECTED"); break;
        case ESP_RST_INT_WDT: Serial.println("(Interrupt watchdog reset) ⚠️  Tight loop?"); break;
        case ESP_RST_TASK_WDT:Serial.println("(Task watchdog reset) ⚠️  Task blocked?"); break;
        case ESP_RST_WDT:      Serial.println("(Other watchdog reset)"); break;
        case ESP_RST_DEEPSLEEP:Serial.println("(Deep sleep reset)"); break;
        case ESP_RST_BROWNOUT: Serial.println("(Brownout reset - check power CPU/WiFi spike)"); break;
        case ESP_RST_SDIO:    Serial.println("(SDIO reset)"); break;
        default:              Serial.println("(Unknown reset)"); break;
    }
    
    // *** DIAGNOSTIC: Check if rebooting too quickly (indicates crash loop) ***
    static unsigned long lastBootTime = 0;
    unsigned long bootTime = millis();
    if (lastBootTime > 0 && bootTime < 30000) {
        Serial.printf("[DIAGNOSTIC] ⚠️  ⚠️  RAPID REBOOT DETECTED! Only %lu ms uptime before crash\n", bootTime);
    }
    lastBootTime = bootTime;


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

    // Initialize persistence after NVS
    if (nvs_ret == ESP_OK)
    {
        if (!g_persistence.init())
        {
            Serial.println("[System] ⚠️  Persistence init failed - continuing without NVS");
        }
    }

    // Record startup
    Serial.printf("[System] Reboot count: %u\n", g_persistence.getRebootCount());
    g_persistence.recordRebootCount();
    
    // *** DIAGNOSTIC: Log time between reboots ***
    static unsigned long lastLogged = 0;
    if (millis() - lastLogged > 5000 || lastLogged == 0) {
        Serial.printf("[DIAGNOSTIC] ⏱️  Reboot Safety Check: millis()=%lu, steady uptime since last boot\n", millis());
        lastLogged = millis();
    }

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
    else
    {
        g_healthMonitor.addTaskToWatchdog(ocppTaskHandle, "OCPP_LOOP");
    }

    // Create UI task for serial menu - LOWEST PRIORITY
    BaseType_t uiResult = xTaskCreatePinnedToCore(
        [](void *arg)
        {
            while (true)
            {
                if (Serial.available()) {
                    char c = Serial.read();
                    processDebugCommand(c);
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
    WiFi.setSleep(false); // Fix: Disable power save for stable WebSocket

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
    
    // Initialize debug logger
    DebugLogger::init();
    DebugLogger::printMenu();
}

void loop()
{
    // CRITICAL: Wait for OCPP initialization before accessing connector 1
    if (!ocppInitialized) {
        g_wifiManager.poll();
        g_healthMonitor.poll();
        g_healthMonitor.feed();
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
        
        // Method 2: Zero current timeout - ONLY during active charging AND after grace period
        static unsigned long transactionStartTime = 0;
        if (transactionActive && !chargingEnabled) {
            transactionStartTime = millis(); // Reset grace period when transaction starts
        }
        
        const unsigned long ZERO_CURRENT_GRACE_PERIOD = 30000; // 30s grace period after transaction start
        bool gracePeriodExpired = (millis() - transactionStartTime) > ZERO_CURRENT_GRACE_PERIOD;
        
        /*
        if (transactionActive && chargingEnabled && gracePeriodExpired &&
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
        */
        
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
            if (transactionActive && ocpp::isTransactionRunningSafe(1)) {
                Serial.printf("[PLUG] 🛑 Stopping transaction due to EV disconnect (txId=%d)\n", activeTransactionId);
                ocpp::endTransactionSafe(nullptr, "EVDisconnected");
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
    static unsigned long lastChargerStatusSent = 0;
    
    // Send when EV connected in Preparing state (waiting for user to start charging)
    // Stop when transaction starts (RemoteStart accepted)
    bool shouldSendVehicleInfo = (
        batteryConnected && 
        gunPhysicallyConnected && 
        !transactionActive &&  // No transaction started yet
        !ocpp::isTransactionRunningSafe(1) &&  // Double-check no active transaction
        BMS_Imax > 0.0f && 
        terminalVolt > 56.0f &&
        socPercent > 0.0f  // Valid SOC data
    );
    
    if (shouldSendVehicleInfo)
    {
        // Fast updates: 3s first time, then 5s interval for real-time data
        unsigned long interval = firstSendDone ? 5000 : 3000;
        
/*
        if (millis() - lastVehicleInfoSent >= interval)
        {
            ocpp::sendVehicleInfo(socPercent, BMS_Imax, terminalVolt, terminalCurr, chargerTemp, vehicleModel, rangeKm);
            lastVehicleInfoSent = millis();
            firstSendDone = true;
        }
*/
        
        // Send ChargerStatus ONLY when there's a blocking issue
        if (millis() - lastChargerStatusSent >= 5000)
        {
            bool chargerHealthy = isChargerModuleHealthy();
            bool wifiConnected = g_wifiManager.isConnected();
            bool tempOk = (chargerTemp < ALERT_TEMP_CRITICAL_C);
            bool voltageOk = (terminalVolt >= MIN_VOLTAGE_V && terminalVolt <= MAX_VOLTAGE_V);
            
            // ONLY send if there's a problem
            if (!chargerHealthy) {
                ocpp::sendChargerStatus(false, "Charger module offline - check CAN bus connection");
                lastChargerStatusSent = millis();
            } else if (!wifiConnected) {
                ocpp::sendChargerStatus(false, "WiFi disconnected - check network connection");
                lastChargerStatusSent = millis();
            } else if (!bmsSafeToCharge) {
                ocpp::sendChargerStatus(false, "BMS charging disabled - vehicle not ready");
                lastChargerStatusSent = millis();
            } else if (!tempOk) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Temperature too high: %.1f°C", chargerTemp);
                ocpp::sendChargerStatus(false, msg);
                lastChargerStatusSent = millis();
            } else if (!voltageOk) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Voltage out of range: %.1fV", terminalVolt);
                ocpp::sendChargerStatus(false, msg);
                lastChargerStatusSent = millis();
            }
            // If everything is OK, don't send anything
        }
    }
    else
    {
        // Reset when conditions not met
        if (transactionActive || ocpp::isTransactionRunningSafe(1) || !batteryConnected) {
            lastVehicleInfoSent = 0;
            firstSendDone = false;
            lastChargerStatusSent = 0;
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
                
                if (transactionActive && ocpp::isTransactionRunningSafe(1))
                {
                    Serial.printf("[SAFETY] 🚨 EMERGENCY STOP - BMS switched OFF during charging (txId=%d)\n", activeTransactionId);
                    ocpp::sendBMSAlert("BMS_EMERGENCY_STOP", "BMS disabled charging during transaction");
                    ocpp::endTransactionSafe(nullptr, "EmergencyStop");
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
                
                // Send alert to server
                ocpp::sendSystemAlert("CHARGER_OFFLINE", "CAN communication timeout", "Critical");
                
                Serial.println("[OCPP] 🚨 Forcing connector to Unavailable");
                // MicroOcpp will automatically update based on setEvseReadyInput
            }
            else
            {
                Serial.println("\n[CHARGER] ✅ Charger module communication restored!");
                Serial.println("[OCPP] ✅ Connector now Available");
                
                // Send recovery alert
                ocpp::sendSystemAlert("CHARGER_ONLINE", "CAN communication restored", "Info");
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
            if (transactionActive && ocpp::isTransactionRunningSafe(1))
            {
                Serial.printf("[CHARGER] 🚨 SAFETY: Charger offline during transaction (txId=%d)\n", activeTransactionId);
                Serial.println("[CHARGER] 🔍 Check: CAN bus, charger power, hardware connection");
                ocpp::endTransactionSafe(nullptr, "EVSEFailure");
            }
        }
        
        lastChargerHealthCheck = millis();
    }
    
    // FINAL FIX: HARD GATE without txId check
    // Golden Rule: OCPP authorization comes from StartTransaction acceptance, NOT txId
    bool ocppAllows = ocpp::ocppPermitsChargeSafe(1);
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
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
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
        /*
        bool ocppConnected = ocpp::isConnected();
        bool txActive = ocpp::isTransactionActiveSafe(1);  // Preparing or running
        bool txRunning = ocpp::isTransactionRunningSafe(1);  // Actively running
        bool chargerHealthy = isChargerModuleHealthy();
        bool ocppPermits = ocpp::ocppPermitsChargeSafe(1);
        */
        
        // [DISABLED] Periodic status logging - removed for cleaner console
        // const char* currentState = g_ocppStateMachine.getStateName();
        // Serial.printf("[DIAGNOSTIC] 🔍 State Machine Status: %s | TX_Active=%d | TX_Running=%d\n", 
        //              currentState, txActive, txRunning);
        // Serial.printf("\n[Status] Uptime: %us | WiFi: %s | OCPP: %s | State: %s\n",
        //               g_healthMonitor.getUptimeSeconds(),
        //               g_wifiManager.isConnected() ? "✅" : "❌",
        //               ocppConnected ? "Connected" : "Disconnected",
        //               currentState);
        // Serial.printf("[Metrics] V=%.1fV I=%.1fA SOC=%.1f%% Range=%.1fkm Temp=%.1f°C Energy=%.2fWh (meter=%d)\n",
        //               terminalVolt, terminalCurr, socPercent, rangeKm, chargerTemp, energyWh, (int)energyWh);
        // 
        // const char* modelName = "Unknown";
        // if (vehicleModel == 1) modelName = "Classic";
        // else if (vehicleModel == 2) modelName = "Pro";
        // else if (vehicleModel == 3) modelName = "Max";
        // 
        // Serial.printf("[Vehicle] Model=%s | Capacity=%.0fAh | BMS_Imax=%.1fA\n",
        //               modelName, batteryAh, BMS_Imax);
        // Serial.printf("[Charger] Module=%s | Enabled=%s | TX=%s/%s | Current=%s | OCPP=%s\n",
        //               chargerHealthy ? "ONLINE" : "OFFLINE",
        //               chargingEnabled ? "YES" : "NO",
        //               txActive ? "ACTIVE" : "IDLE",
        //               txRunning ? "RUNNING" : "STOPPED",
        //               (terminalCurr > 1.0f) ? "FLOWING" : "ZERO",
        //               ocppPermits ? "PERMITS" : "BLOCKS");
        lastDebug = millis();
    }

    // WiFi connection monitoring
    static bool lastWifiConnected = true;
    bool wifiConnected = g_wifiManager.isConnected();
    
    if (wifiConnected != lastWifiConnected) {
        if (!wifiConnected) {
            Serial.println("[WIFI] ❌ Network connection lost!");
            ocpp::sendSystemAlert("WIFI_DISCONNECTED", "Network connection lost", "Critical");
        } else {
            Serial.println("[WIFI] ✅ Network connection restored!");
            ocpp::sendSystemAlert("WIFI_RECONNECTED", "Network connection restored", "Info");
        }
        lastWifiConnected = wifiConnected;
    }
    
    // Temperature monitoring
    static bool tempWarningActive = false;
    static bool tempCriticalActive = false;
    
    if (chargerTemp > ALERT_TEMP_CRITICAL_C && !tempCriticalActive) {
        Serial.printf("[TEMP] 🚨 CRITICAL: Temperature %.1f°C (limit: %.0f°C)\n", chargerTemp, ALERT_TEMP_CRITICAL_C);
        char msg[64];
        snprintf(msg, sizeof(msg), "Temperature: %.1f°C", chargerTemp);
        ocpp::sendSystemAlert("TEMPERATURE_CRITICAL", msg, "Critical");
        tempCriticalActive = true;
        tempWarningActive = true;
    } else if (chargerTemp > ALERT_TEMP_WARNING_C && !tempWarningActive) {
        Serial.printf("[TEMP] ⚠️  WARNING: Temperature %.1f°C (limit: %.0f°C)\n", chargerTemp, ALERT_TEMP_WARNING_C);
        char msg[64];
        snprintf(msg, sizeof(msg), "Temperature: %.1f°C", chargerTemp);
        ocpp::sendSystemAlert("TEMPERATURE_WARNING", msg, "Warning");
        tempWarningActive = true;
    } else if (chargerTemp < (ALERT_TEMP_WARNING_C - 5.0f) && tempWarningActive) {
        Serial.printf("[TEMP] ✅ Temperature normal: %.1f°C\n", chargerTemp);
        ocpp::sendSystemAlert("TEMPERATURE_NORMAL", "Temperature recovered", "Info");
        tempWarningActive = false;
        tempCriticalActive = false;
    }
    
    // Voltage monitoring
    static bool voltageAlertActive = false;
    
    if (terminalVolt > 0.0f && batteryConnected) {
        if ((terminalVolt > ALERT_VOLTAGE_MAX_V || terminalVolt < ALERT_VOLTAGE_MIN_V) && !voltageAlertActive) {
            Serial.printf("[VOLTAGE] 🚨 Out of range: %.1fV (range: %.0f-%.0fV)\n", 
                terminalVolt, ALERT_VOLTAGE_MIN_V, ALERT_VOLTAGE_MAX_V);
            char msg[64];
            snprintf(msg, sizeof(msg), "Voltage: %.1fV", terminalVolt);
            ocpp::sendSystemAlert("VOLTAGE_FAULT", msg, "Critical");
            voltageAlertActive = true;
        } else if (terminalVolt >= MIN_VOLTAGE_V && terminalVolt <= MAX_VOLTAGE_V && voltageAlertActive) {
            Serial.printf("[VOLTAGE] ✅ Normal: %.1fV\n", terminalVolt);
            ocpp::sendSystemAlert("VOLTAGE_NORMAL", "Voltage recovered", "Info");
            voltageAlertActive = false;
        }
    }
    
    // Current monitoring
    static bool currentAlertActive = false;
    
    if (terminalCurr > ALERT_CURRENT_MAX_A && !currentAlertActive) {
        Serial.printf("[CURRENT] 🚨 Overcurrent: %.1fA (limit: %.0fA)\n", terminalCurr, ALERT_CURRENT_MAX_A);
        char msg[64];
        snprintf(msg, sizeof(msg), "Current: %.1fA", terminalCurr);
        ocpp::sendSystemAlert("OVERCURRENT", msg, "Critical");
        currentAlertActive = true;
    } else if (terminalCurr < MAX_CURRENT_A && currentAlertActive) {
        Serial.printf("[CURRENT] ✅ Normal: %.1fA\n", terminalCurr);
        ocpp::sendSystemAlert("CURRENT_NORMAL", "Current recovered", "Info");
        currentAlertActive = false;
    }

    // FIX #5: Yield to prevent watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(10));
}
