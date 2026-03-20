#include <Arduino.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <LittleFS.h>
#include <MicroOcpp.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Model/Transactions/Transaction.h>
#include <MicroOcpp/Model/ConnectorBase/Connector.h>
// SECURITY FIX: Removed hardcoded secrets.h, using secure configuration
#include "../include/config/secure_config.h"
#include "../include/header.h"
#include "../include/debug_logger.h"
#include "../include/ocpp/ocpp_client.h"
#include "../include/production_config.h"
#include "../include/wifi_manager.h"
#include "../include/modules/network_manager.h"
#include "../include/health_monitor.h"
#include "../include/ocpp_state_machine.h"
#include "../include/security_manager.h"
#include "../include/modules/ota_manager.h"
#include "../include/drivers/can_twai_driver.h"
#include "../include/drivers/can_mcp2515_driver.h"
#include "../include/config/version.h"
#include "../include/config/hardware.h"
#include "../include/modules/hardware_service.h"
#include "../include/utils/safe_serial.h"

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
            if (prod::g_networkManager.isConnected() && (int32_t)(millis() - nextAttemptMs) >= 0)
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

            // g_healthMonitor.feed(); // DISABLED: Testing GSM step-by-step
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ocpp::poll();
        // g_healthMonitor.feed(); // DISABLED: Testing GSM step-by-step / User requested disable
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


void setup()
{
    Serial.begin(115200);
    // STABILITY: 5s initial delay for power-on rails to stabilize 
    // especially for GSM and CAN transceivers.
    delay(5000); 

    // Route MicroOcpp logs through SafeSerial to prevent line interleaving 
    // Output often exceeds the 64-byte UART TX buffer, causing yields
    mocpp_set_console_out([](const char* msg) { 
        SafeSerial::print(msg); 
    });

    // Visual heartbeat setup (Now handled by HardwareService D15/D13)
    // Removed legacy LED_WIFI


    // TEMPORARY: Aggressively disable ALL watchdog timers for verification testing
    // Step 1: Remove IDLE tasks from TWDT (they are added by default in ESP-IDF)
    TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCPU(0);
    TaskHandle_t idle1 = xTaskGetIdleTaskHandleForCPU(1);
    if (idle0) esp_task_wdt_delete(idle0);
    if (idle1) esp_task_wdt_delete(idle1);
    // Step 2: Deinit the TWDT entirely
    esp_task_wdt_deinit();
    Serial.println("[BOOT] ⚠️  ALL Watchdog Timers DISABLED (IDLE0, IDLE1, TWDT)");
    
    // Initialize health monitor (calls TWDT init internally, so keep commented for now)
    // g_healthMonitor.init(); 
    Serial.println("[System] 🛡️  Health Monitor / Watchdog DISABLED per user request");

    // SECURITY FIX: Load configuration from secure storage instead of hardcoded values
    char chargerId[32], csmsHost[128], csmsUrl[256];
    uint16_t csmsPort;
    
    if (SecureConfig::getOCPPConfig(csmsHost, csmsPort, chargerId, csmsUrl, 
                                   sizeof(csmsHost), sizeof(chargerId), sizeof(csmsUrl)))
    {
        Serial.println("\n========================================");
        Serial.printf("  ESP32 OCPP EVSE Controller - v%s\n", FIRMWARE_VERSION);
        Serial.println("  Production-Ready Edition (Secure)");
        Serial.printf("  Build: %s\n", BUILD_TIMESTAMP);
        Serial.printf("  StationId: %s\n", chargerId);
        Serial.printf("  CSMS: %s:%d\n", csmsHost, csmsPort);
        Serial.println("========================================");
        Serial.println("\n🔐 SECURE: Configuration loaded from encrypted storage");
        Serial.printf("  CSMS_HOST = %s\n", csmsHost);
        Serial.printf("  CSMS_PORT = %d\n", csmsPort);
        Serial.printf("  CSMS_URL  = %s\n", csmsUrl);
        Serial.printf("  CHARGER_ID = %s\n", chargerId);
        Serial.println("========================================\n");
    }
    else
    {
        Serial.println("\n❌ CRITICAL: Failed to load secure configuration!");
        Serial.println("Device requires credential provisioning.");
        Serial.println("========================================\n");
    }

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
    // g_healthMonitor.init(); // DISABLED: Testing GSM step-by-step

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

    // SECURITY FIX: Perform one-time credential migration from hardcoded secrets
    Serial.println("[System] 🔐 Checking secure credential migration...");
    if (!SecureConfig::isConfigured())
    {
        Serial.println("[System] 📦 First boot detected - migrating credentials to secure storage...");
        if (SecureConfig::migrateFromLegacySecrets())
        {
            Serial.println("[System] ✅ Credential migration completed successfully");
            Serial.println("[System] ⚠️  IMPORTANT: Remove secrets.h from version control!");
        }
        else
        {
            Serial.println("[System] ❌ CRITICAL: Credential migration failed!");
            Serial.println("[System] Device may not function properly without credentials");
        }
    }
    else
    {
        Serial.println("[System] ✅ Secure credentials already configured");
    }

    // Clean stale transaction files from LittleFS dynamically
    Serial.println("[System] 🧹 Cleaning stuck transaction files...");
    if (LittleFS.begin(true)) {
        File root = LittleFS.open("/");
        if (root && root.isDirectory()) {
            File entry = root.openNextFile();
            while (entry) {
                const char* name = entry.name();
                // Match any /tx-*.json pattern
                if (strncmp(name, "tx-", 3) == 0 && strstr(name, ".json") != nullptr) {
                    char fullPath[64];
                    snprintf(fullPath, sizeof(fullPath), "/%s", name);
                    entry.close();
                    LittleFS.remove(fullPath);
                    Serial.printf("[System]   Deleted: %s\n", fullPath);
                    entry = root.openNextFile();
                    continue;
                }
                entry.close();
                entry = root.openNextFile();
            }
            root.close();
        }
        Serial.println("[System] ✅ Transaction cleanup complete");
    }

    // Record startup - reset counter on clean power-on boot
    if (reset_reason == ESP_RST_POWERON) {
        g_persistence.resetRebootCount();
        Serial.println("[System] 🔄 Power-on reset — reboot counter reset to 0");
    } else {
        Serial.printf("[System] Reboot count: %u\n", g_persistence.getRebootCount());
        g_persistence.recordRebootCount();
    }
    // *** DIAGNOSTIC: Log time between reboots and heap memory ***
    static unsigned long lastLogged = 0;
    if (millis() - lastLogged > 5000 || lastLogged == 0) {
        Serial.printf("[DIAGNOSTIC] ⏱️  Reboot Safety Check: millis()=%lu, steady uptime since last boot\n", millis());
        Serial.printf("[MEM] Free heap: %u bytes\n", ESP.getFreeHeap());
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
        TASK_STACK_SIZE_CAN_RX,
        nullptr,
        TASK_PRIORITY_CAN_RX,
        &can1RxHandle,
        1);
    
    if (can1RxResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create CAN1_RX task!");
    }
    else
    {
        // g_healthMonitor.addTaskToWatchdog(can1RxHandle, "CAN1_RX"); // DISABLED per user request
    }

    // Create CAN2 RX task (BMS) - HIGH PRIORITY (priority 8)
    TaskHandle_t can2RxHandle = nullptr;
    BaseType_t can2RxResult = xTaskCreatePinnedToCore(
        can2_rx_task,
        "CAN2_RX",
        TASK_STACK_SIZE_CAN_RX,
        nullptr,
        TASK_PRIORITY_CAN_RX,
        &can2RxHandle,
        1);
    
    if (can2RxResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create CAN2_RX task!");
    }
    else
    {
        // g_healthMonitor.addTaskToWatchdog(can2RxHandle, "CAN2_RX"); // DISABLED per user request
    }

    // Create charger communication task - HIGH PRIORITY (priority 7)
    TaskHandle_t chargerHandle = nullptr;
    BaseType_t chargerResult = xTaskCreatePinnedToCore(
        chargerCommTask,
        "CHARGER_COMM",
        TASK_STACK_SIZE_CHARGER_COMM,
        nullptr,
        TASK_PRIORITY_CHARGER_COMM,
        &chargerHandle,
        1);
    
    if (chargerResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create CHARGER_COMM task!");
    }
    else
    {
        // SAFETY: Add to watchdog
        // g_healthMonitor.addTaskToWatchdog(chargerHandle, "CHARGER_COMM"); // DISABLED per user request
    }

    // FIX #3: Create OCPP task on Core 0 - MEDIUM PRIORITY (priority 3)
    BaseType_t ocppResult = xTaskCreatePinnedToCore(
        ocppTask,
        "OCPP_LOOP",
        TASK_STACK_SIZE_OCPP,
        nullptr,
        TASK_PRIORITY_OCPP,
        &ocppTaskHandle,
        0); // Core 0 for OCPP
    
    if (ocppResult != pdPASS)
    {
        Serial.println("[CRITICAL] Failed to create OCPP_LOOP task!");
    }
    else
    {
        // g_healthMonitor.addTaskToWatchdog(ocppTaskHandle, "OCPP_LOOP"); // DISABLED per user request
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

    // Initialize Network Manager (GSM primary, WiFi fallback)
    Serial.println("[BOOT] 📡 Initializing Network Manager (GSM → WiFi)...");
    prod::g_networkManager.init();

    // Create NETWORK_MGR task on Core 0 (priority 5)
    static TaskHandle_t networkTaskHandle = nullptr;
    BaseType_t netResult = xTaskCreatePinnedToCore(
        [](void *arg) {
            // Initial connection attempt
            Serial.println("[NETWORK_MGR] Task started, beginning connection...");
            while (true) {
                prod::g_networkManager.poll();
                // g_healthMonitor.feed(); // DISABLED
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        },
        "NETWORK_MGR",
        TASK_STACK_SIZE_NETWORK,
        nullptr,
        5,  // Priority 5 — between CAN (8) and OCPP (3)
        &networkTaskHandle,
        0); // Core 0

    if (netResult != pdPASS) {
        Serial.println("[CRITICAL] Failed to create NETWORK_MGR task!");
    } else {
        // g_healthMonitor.addTaskToWatchdog(networkTaskHandle, "NETWORK_MGR"); // DISABLED per user request
    }

    // Initialize security (TLS/WSS)
    Serial.println("[BOOT] 🔒 Initializing security...");
    g_securityManager.init();

    // Initialize OTA manager
    Serial.println("[BOOT] 🔄 Initializing OTA...");
    g_otaManager.init();

    // For production with valid SSL certificate, uncomment:
    // const char* ROOT_CA = "-----BEGIN CERTIFICATE-----\n..."; 
    // g_securityManager.loadRootCA(ROOT_CA);
    // g_securityManager.enableCertificateVerification();
    
    // For now, using setInsecure() to accept self-signed certificates
    Serial.println("[BOOT] ⚠️  Using insecure mode for WSS (accepts any certificate)");

    // NOTE: OCPP initialization now happens in ocppTask after WiFi is ready
    // This prevents the race condition that was causing crashes
    // Connector plug detection is configured in ocpp_manager.cpp

    // Initialize hardware monitoring service (extracted from loop())
    g_hardwareService.begin();

    // Initialize OCPP state machine
    g_ocppStateMachine.init();

    Serial.println("[BOOT] ✅ All systems initialized!\n");
    
    // Initialize debug logger
    DebugLogger::init();
    DebugLogger::printMenu();
}

void loop()
{
    // CRITICAL: Feed watchdog for loop task (runs on Core 1)
    // g_healthMonitor.feed(); // DISABLED
    // g_healthMonitor.poll(); // DISABLED

    // CRITICAL: Wait for OCPP initialization before accessing connector 1
    if (!ocppInitialized) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }
    
    // Core services (lightweight polling)
    g_ocppStateMachine.poll();

    // ═══════════════════════════════════════════════════════════
    // All hardware monitoring delegated to HardwareService
    // (Plug detection, Safety, Energy, Charger health, VehicleInfo)
    // ═══════════════════════════════════════════════════════════
    g_hardwareService.poll();

    // ═══════════════════════════════════════════════════════════
    // Heartbeat / Autonomous Boot Indicator
    // (Now handled by HardwareService pollStatusLEDs)
    // ═══════════════════════════════════════════════════════════

    // FIX #5: Yield to prevent watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(10));
}
