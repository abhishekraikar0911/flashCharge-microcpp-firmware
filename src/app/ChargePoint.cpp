#include "app/ChargePoint.h"
#include <Arduino.h>
#include <nvs_flash.h>
#include <LittleFS.h>
#include "config/production_config.h"
#include "config/secure_config.h"
#include "config/version.h"
#include "system/state/SystemState.h"
#include "system/FaultQueue.h"    // Boot fault reporting (CRASH_LOOP, WDT_CRASH)
#include "system/CrashForensics.h"  // Pre-crash activity/heap/TLS tracking
#include "services/network/NetworkManager.h"
#include "system/security/SecurityManager.h"
#include "services/ota/OtaManager.h"
#include "services/safety/SystemMonitor.h"
#include "tasks/system_tasks.h"
#include "services/provisioning/ProvisioningManager.h"

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
    Serial.println("[System] 🔐 Checking secure credentials in NVS...");

    // ── Zero-Touch Auto-Provisioning ─────────────────────────────────────
    // On a completely new / factory-reset ESP32 the NVS is empty.
    // We block here and auto-generate the Charger ID from the MAC address,
    // load default CSMS/APN/WiFi settings, and save them to NVS permanently.
    // This block is skipped on every future boot (wire re-flash or OTA).
    // ─────────────────────────────────────────────────────────────────────
    if (Provisioning::isProvisioningRequired()) {
        Serial.println("[System] 🆕 NEW DEVICE DETECTED — NVS is empty.");
        Serial.println("[System] 🔧 Starting Zero-Touch Auto-Provisioning...");
        Provisioning::enterProvisioningMode(); // blocks until done, then restarts ESP32
        // ESP32 restarts inside enterProvisioningMode(), so we never reach here.
        return;
    }

    Serial.println("[System] ✅ NVS credentials found — skipping provisioning.");

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

    // ── Load crash forensics from previous boot BEFORE building fault report ─
    // CrashForensics persists lastActivity/tlsMs/heap to NVS before risky ops.
    // We read those values here so the WDT_CRASH report sent to CSMS includes
    // what the firmware was actually doing when it died.
    CrashForensics::load();

    // ── Classify the reset reason ────────────────────────────────────
    // ESP_RST_POWERON  (1)  → Clean first boot, no stop reason needed.
    // ESP_RST_SW       (3)  → Intentional software restart: OCPP Reset.req / OTA.
    // ESP_RST_PANIC    (6)  → Firmware crash / assertion.
    // ESP_RST_INT_WDT  (7)  → Interrupt watchdog timeout.
    // ESP_RST_TASK_WDT (8)  → Task watchdog timeout (our WDT).
    // ESP_RST_WDT      (9)  → Other watchdog.
    // ESP_RST_BROWNOUT (12) → Supply voltage dipped.
    bool isWdtOrPanic = (reset_reason == ESP_RST_PANIC    ||
                         reset_reason == ESP_RST_INT_WDT  ||
                         reset_reason == ESP_RST_TASK_WDT ||
                         reset_reason == ESP_RST_WDT);

    if (reset_reason == ESP_RST_POWERON) {
        Serial.printf("[System] Reset reason: %d (Power-On — clean boot)\n", reset_reason);
    } else if (reset_reason == ESP_RST_SW) {
        Serial.printf("[System] Reset reason: %d (Software Reset — OCPP/OTA initiated)\n", reset_reason);
        SystemState::instance().setStopReason(StopReason::SOFT_RESET);
    } else {
        Serial.printf("[System] Reset reason: %d (Unexpected — PowerLoss/WDT/Panic)\n", reset_reason);
        SystemState::instance().setStopReason(StopReason::POWER_RESTART);
    }

    // ── WDT / Panic reset: queue enriched fault for CSMS ─────────────────
    // Includes forensics fields so CSMS can diagnose remotely:
    //   lastActivity   — what operation was running before crash
    //   tlsDurationMs  — how long TLS connect took last time (0 if not reached)
    //   minHeapBytes   — lowest heap seen before crash (heap corruption indicator)
    //   currentHeapBytes — heap NOW at next boot (post-crash state)
    uint8_t rebootCount = g_persistence.getRebootCount();

    char desc[FAULT_DESC_LEN];
    snprintf(desc, sizeof(desc),
        "reset=%d act=%s up=%lu tlsMs=%lu tlsMax=%lu minHeap=%lu minBlk=%lu curHeap=%lu boots=%u fw=%s",
        (int)reset_reason,
        CrashForensics::getActivity(),
        (unsigned long)CrashForensics::getUptimeAtPersist(),
        (unsigned long)CrashForensics::getTlsDurationMs(),
        (unsigned long)CrashForensics::getMaxTlsDurationMs(),
        (unsigned long)CrashForensics::getMinHeapBytes(),
        (unsigned long)CrashForensics::getMinLargestBlock(),
        (unsigned long)CrashForensics::getCurrentHeapBytes(),
        (unsigned)rebootCount,
        FIRMWARE_VERSION);

    if (isWdtOrPanic) {
        FaultQueue::push("WDT_CRASH", desc, FAULT_SEV_CRITICAL);
        Serial.printf("[FORENSICS] 💥 Crash report: %s\n", desc);
    } else {
        // Send diagnostic even for Soft Resets or manual hard reboots so the 
        // Admin UI can see what state the charger was in before the reset.
        FaultQueue::push("BOOT_DIAGNOSTIC", desc, FAULT_SEV_INFO);
        Serial.printf("[FORENSICS] 📊 Boot diagnostic: %s\n", desc);
    }

    // ── Crash loop detection: only unexpected resets count ────────────────
    // Intentional resets (ESP_RST_POWERON, ESP_RST_SW) are excluded —
    // OTA and OCPP resets increment reboot count otherwise and cause
    // false-positive CRASH_LOOP alerts after repeated OTA updates.
    bool isIntentionalReset = (reset_reason == ESP_RST_POWERON ||
                               reset_reason == ESP_RST_SW);

    if (rebootCount > 3 && !isIntentionalReset) {
        Serial.printf("[DIAGNOSTIC] ⚠️  ⚠️  CRASH LOOP DETECTED! Reboot count: %u\n", rebootCount);
        char desc[FAULT_DESC_LEN];
        snprintf(desc, sizeof(desc),
            "Charger has rebooted %u times unexpectedly. "
            "Possible firmware crash loop. Investigate serial backtrace.",
            (unsigned)rebootCount);
        FaultQueue::push("CRASH_LOOP", desc, FAULT_SEV_CRITICAL);
    }

    // Reset counter on intentional resets; increment only on unexpected ones
    if (isIntentionalReset) {
        g_persistence.resetRebootCount();  // Clean boot or OTA/OCPP reboot — not a crash
        CrashForensics::clear();           // Also clear forensics — stale data from last crash
    } else {
        g_persistence.recordRebootCount(); // WDT, panic, brownout — counts toward crash loop
        // Do NOT clear forensics here — we just read them for the report above.
        // They will be overwritten naturally as the firmware proceeds.
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
    CrashForensics::setActivity(CrashForensics::ACT_BOOT);
    CrashForensics::persist();   // If we crash during boot, next report shows ACT_BOOT

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
