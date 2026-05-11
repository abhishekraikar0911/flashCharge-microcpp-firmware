#include "app/ChargePoint.h"
#include <Arduino.h>
#include <nvs_flash.h>
#include <LittleFS.h>
#include "config/production_config.h"
#include "config/secure_config.h"
#include "config/version.h"
#include "system/state/SystemState.h"
#include "services/network/NetworkManager.h"
#include "system/security/SecurityManager.h"
#include "services/ota/OtaManager.h"
#include "services/safety/SystemMonitor.h"
#include "tasks/system_tasks.h"

namespace prod {

ChargePoint& ChargePoint::instance() {
    static ChargePoint cp;
    return cp;
}

void ChargePoint::initStorage() {
    Serial.println("[System] 💾 Initializing NVS Flash...");
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("[System] ⚠️  NVS partition needs erasing...");
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }

    if (nvs_ret == ESP_OK) {
        Serial.println("[System] ✅ NVS Flash initialized");
        if (!g_persistence.init()) {
            Serial.println("[System] ⚠️  Persistence init failed - continuing without NVS");
        }
    } else {
        Serial.printf("[System] ❌ NVS Flash init failed: 0x%X\n", nvs_ret);
    }
}

void ChargePoint::cleanStaleTransactions() {
    Serial.println("[System] 🧹 Cleaning stuck transaction files...");
    if (LittleFS.begin(true)) {
        File root = LittleFS.open("/");
        if (root && root.isDirectory()) {
            File entry = root.openNextFile();
            while (entry) {
                const char* name = entry.name();
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
}

void ChargePoint::initSecurity() {
    Serial.println("[System] 🔐 Checking secure credential migration...");
    if (!SecureConfig::isConfigured()) {
        Serial.println("[System] 📦 First boot detected - migrating credentials to secure storage...");
        if (SecureConfig::migrateFromLegacySecrets()) {
            Serial.println("[System] ✅ Credential migration completed successfully");
        } else {
            Serial.println("[System] ❌ CRITICAL: Credential migration failed!");
        }
    }

    char chargerId[32], csmsHost[128], csmsUrl[256];
    uint16_t csmsPort;
    
    if (SecureConfig::getOCPPConfig(csmsHost, csmsPort, chargerId, csmsUrl, 
                                   sizeof(csmsHost), sizeof(chargerId), sizeof(csmsUrl))) {
        Serial.println("\n========================================");
        Serial.printf("  ESP32 OCPP EVSE Controller - v%s\n", FIRMWARE_VERSION);
        Serial.println("  Production-Ready Edition (Secure)");
        Serial.printf("  Build: %s\n", BUILD_TIMESTAMP);
        Serial.printf("  StationId: %s\n", chargerId);
        Serial.println("========================================\n");
    } else {
        Serial.println("\n❌ CRITICAL: Failed to load secure configuration!");
    }
}

void ChargePoint::checkCrashLoop() {
    esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.printf("[System] Reset reason: %d\n", reset_reason);

    if (reset_reason != ESP_RST_POWERON) {
        SystemState::instance().setStopReason(StopReason::POWER_RESTART);
    }

    uint8_t rebootCount = g_persistence.getRebootCount();
    if (rebootCount > 3 && reset_reason != ESP_RST_POWERON) {
        Serial.printf("[DIAGNOSTIC] ⚠️  ⚠️  CRASH LOOP DETECTED! Reboot count: %u\n", rebootCount);
    }

    if (reset_reason == ESP_RST_POWERON) {
        g_persistence.resetRebootCount();
    } else {
        g_persistence.recordRebootCount();
    }
}

void ChargePoint::launchTasks() {
    Serial.println("[BOOT] 🚀 Launching RTOS Tasks...");
    
    // Core 0 Tasks
    tasks::start_network_task();
    tasks::start_ocpp_task();

    // Core 1 Tasks
    tasks::start_hw_svc_task();
    tasks::start_can_rx_task();
    tasks::start_ui_task();
}

void ChargePoint::boot() {
    initStorage();
    checkCrashLoop();
    initSecurity();
    cleanStaleTransactions();

    Serial.println("[BOOT] 📡 Initializing Network Manager (GSM → WiFi)...");
    g_networkManager.init();

    Serial.println("[BOOT] 🔒 Initializing security...");
    g_securityManager.init();

    Serial.println("[BOOT] 🔄 Initializing OTA...");
    g_otaManager.init();

    SystemMonitor::instance().begin();

    launchTasks();
    
    Serial.println("[BOOT] ✅ Application Orchestrator booted successfully!\n");
}

} // namespace prod
