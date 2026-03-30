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
#include "config/secure_config.h"
#include "system/DebugLogger.h"
#include "services/OcppClient.h"
#include "config/production_config.h"
#include "system/WifiManager.h"
#include "system/NetworkManager.h"
#include "system/HealthMonitor.h"
// PHASE 4: Removed ocpp_state_machine.h — library manages state internally
#include "system/SecurityManager.h"
#include "system/OtaManager.h"
#include "config/version.h"
#include "config/hardware.h"
#include "system/SafeSerial.h"
// HAL v2: New service coordinator replacing HardwareService progressively
#include "system/SystemMonitor.h"
// HAL v1 – BSP entry point (populates g_app)
#include "bsp/esp32_rev1/bsp_init.h"
#include "system/SystemState.h"

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
        if (!SystemState::instance().getOcppInitialized())
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

    // STABILITY: 10s initial delay for power-on rails to stabilize and allow monitor connection
    // CAN transceivers, MCP2515, and GSM modem all need rails stable before init.
    delay(10000);

    // ═══════════════════════════════════════════════════════════
    // HAL v1 BOOTSTRAP — Populate g_app with HAL and Driver refs
    // Placed AFTER delay(5000) so all power rails (CAN, SPI, GSM) are stable.
    // Old drivers remain active in parallel during migration.
    // ═══════════════════════════════════════════════════════════
    if (!BSP_Init()) {
        Serial.println("[BSP] CRITICAL: BSP_Init() failed! Hardware may be partially initialized.");
    } else {
        Serial.println("[BSP] HAL layer initialized. AppContext populated.");
    }


    // Route MicroOcpp logs through SafeSerial to prevent line interleaving 
    // Output often exceeds the 64-byte UART TX buffer, causing yields
    mocpp_set_console_out([](const char* msg) { 
        SafeSerial::print(msg); 
    });

    // Visual heartbeat setup (Now handled by HardwareService D15/D13)
    // Removed legacy LED_WIFI


    // Initialize health monitor with 30s Task Watchdog Timer
    g_healthMonitor.init();

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
        Serial.println("========================================");
        Serial.println("\n✅ SECURE: Configuration loaded from encrypted storage");
#ifdef DEBUG_VERBOSE
        // WARNING: Only print credentials in debug builds — never in production!
        Serial.printf("  CSMS_HOST = %s\n", csmsHost);
        Serial.printf("  CSMS_PORT = %d\n", csmsPort);
        Serial.printf("  CSMS_URL  = %s\n", csmsUrl);
        Serial.printf("  CHARGER_ID = %s\n", chargerId);
#else
        Serial.println("  [CSMS details hidden in production build]");
#endif
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

    // If reset was not a clean power-on, a transaction may have been running.
    // Log it clearly so the extended validation test suite can verify Power Restart behavior.
    if (reset_reason != ESP_RST_POWERON) {
        SystemState::instance().setStopReason(StopReason::POWER_RESTART);
    }

    
    // *** DIAGNOSTIC: Check if rebooting too quickly (indicates crash loop) ***
    // Uses persistent reboot counter — works across full power cycles.
    uint8_t rebootCount = g_persistence.getRebootCount();
    if (rebootCount > 3 && reset_reason != ESP_RST_POWERON) {
        Serial.printf("[DIAGNOSTIC] ⚠️  ⚠️  CRASH LOOP DETECTED! Reboot count: %u\n", rebootCount);
        Serial.println("[DIAGNOSTIC]    Check last crash: ESP_RST_PANIC or ESP_RST_TASK_WDT");
    }



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

    // CAN buses are now initialized by BSP_Init() above via Esp32Can and Esp32MCP2515.
    // Legacy direct CAN drivers are kept alive so existing tasks continue to work
    // during the migration phase. Both the HAL layer AND legacy layer will be active
    // in parallel until Phase 4-D cleanup.
    Serial.println("[System] 🚌 Initializing legacy dual CAN buses (migration phase)...");
    
    /* Legacy CAN inits disabled to prevent conflict with BSP HAL
    // CAN1 - ISO1050 (TWAI) - Charger Module
    if (!CAN_TWAI::init())
    {
        Serial.println("[System] ❌ CAN1 legacy (Charger) init failed!");
    }
    
    // CAN2 - MCP2515 (SPI) - Vehicle BMS
    if (!CAN_MCP2515::init())
    {
        Serial.println("[System] ❌ CAN2 legacy (BMS) init failed!");
    }
    */

    /* Legacy CAN tasks disabled to prevent frame theft from HAL drivers
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
        g_healthMonitor.addTaskToWatchdog(can1RxHandle, "CAN1_RX");
    }
    */

    /* Legacy CAN tasks disabled to prevent frame theft from HAL drivers
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
        g_healthMonitor.addTaskToWatchdog(can2RxHandle, "CAN2_RX");
    }
    */


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
        g_healthMonitor.addTaskToWatchdog(ocppTaskHandle, "OCPP_LOOP");
    }

    // Create UI task for serial menu - Increased priority for responsiveness
    BaseType_t uiResult = xTaskCreatePinnedToCore(
        [](void *arg)
        {
            Serial.println("[UI_TASK] Serial input listener started");
            while (true)
            {
                // Consume all available characters to prevent buffer buildup
                while (Serial.available() > 0) {
                    char c = Serial.read();
                    processDebugCommand(c);
                }
                vTaskDelay(pdMS_TO_TICKS(50)); // Poll at 20Hz
            }
        },
        "UI_TASK",
        4096,
        nullptr,
        3, // priority 3
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
                g_healthMonitor.feed();
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
        g_healthMonitor.addTaskToWatchdog(networkTaskHandle, "NETWORK_MGR");
    }

    // Initialize security (TLS/WSS)
    Serial.println("[BOOT] 🔒 Initializing security...");
    g_securityManager.init();

    // Initialize OTA manager
    Serial.println("[BOOT] 🔄 Initializing OTA...");
    g_otaManager.init();

    Serial.println("[BOOT] ✅ Security manager configured for Strict TLS (Profile 2)");

    // NOTE: OCPP initialization now happens in ocppTask after WiFi is ready
    // This prevents the race condition that was causing crashes
    // Connector plug detection is configured in ocpp_manager.cpp

    // CRITICAL: hardware_service removed
    // HAL v2: Initialize new service coordinator (parallel during migration)
    prod::SystemMonitor::instance().begin();

    // M7 FIX: Create HW_SVC task on Core 1 (priority 4) for deterministic hardware polling
    // This removes the dependency on the weak Arduino loop() handler.
    static TaskHandle_t hwSvcTaskHandle = nullptr;
    BaseType_t hwResult = xTaskCreatePinnedToCore(
        [](void *arg) {
            Serial.println("[HW_SVC] Task started for deterministic hardware monitoring");
            while (true) {
                // Feed the watchdog! Without this, the ESP32 crashes exactly 30s after boot.
                g_healthMonitor.feed();
                
                // ARCHITECTURE FIX: SystemMonitor must run ALWAYS — not gated on OCPP.
                // ChargerService calls charger->update() + bms->update() every cycle.
                // If we wait for OCPP to connect, lastTelemetryTime stays 0 and
                // isReady() returns false permanently → Healthy=NO.
                prod::SystemMonitor::instance().poll();
                
                vTaskDelay(pdMS_TO_TICKS(50)); // Poll at 20Hz
            }
        },
        "HW_SVC",
        4096,
        nullptr,
        4,  // Priority 4 — between Ui (2) and Network (5)
        &hwSvcTaskHandle,
        1); // Core 1

    if (hwResult != pdPASS) {
        Serial.println("[CRITICAL] Failed to create HW_SVC task!");
    } else {
        g_healthMonitor.addTaskToWatchdog(hwSvcTaskHandle, "HW_SVC");
    }

    Serial.println("[BOOT] ✅ All systems initialized!\n");
    
    // Initialize debug logger
    DebugLogger::init();
    DebugLogger::printMenu();
}

void loop()
{
    // CRITICAL: Feed watchdog for the idle loop task (runs on Core 1 at priority 1) 
    // and process the central health monitor check.
    g_healthMonitor.feed();
    g_healthMonitor.poll();

    // M7 FIX: All hardware monitoring has been moved to the HW_SVC FreeRTOS task.
    // The Arduino loop() now just idles and maintains the health monitor.
    vTaskDelay(pdMS_TO_TICKS(100));
}
