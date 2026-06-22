// OCPP Manager: All OCPP-related logic isolated for easy debugging
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>             // time(), localtime_r(), strftime(), struct tm — required for NTP validation
#include <MicroOcpp.h>
// HAL v1: AppContext for new driver interfaces
#include "app/AppContext.h"
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Model/Transactions/Transaction.h>
#include "config/hardware.h"
#include "system/SafeSerial.h"
#include "services/safety/HealthMonitor.h" // g_healthMonitor.feed() — required inside NTP wait loop
#include "system/FaultQueue.h"             // Hardware fault drain
#include "system/CrashForensics.h"         // Activity tracking + heap low-water mark

#include "services/ocpp/OcppClient.h"
#include "config/production_config.h"
#include "config/secure_config.h"
#include "services/ota/OtaManager.h"
#include "services/ota/HttpOtaClient.h"
// PHASE 4: Removed #include "ocpp_state_machine.h" — library manages state internally
#include "config/certificates.h"
#include "config/version.h"
#include "services/network/NetworkManager.h"
#include "services/ocpp/OcppConnectionHelper.h"
#include <MicroOcpp/Core/Context.h>
#include <MicroOcpp/Model/Model.h>
#include <MicroOcpp/Model/Boot/BootService.h>
#include <MicroOcpp/Model/FirmwareManagement/FirmwareService.h>
#include <freertos/semphr.h>
#include "services/charging/TransactionService.h"
#include "services/charging/MeterService.h"
#include "system/state/SystemState.h"
#include <Preferences.h>

// All state now accessed through SystemState::instance()

// Charger health check
extern bool isChargerModuleHealthy();

using namespace prod;

namespace
{
    static SemaphoreHandle_t ocppMutex = nullptr;

    static const char* ocppStatusToString(ChargePointStatus status) {
        switch (status) {
            case ChargePointStatus_Available:    return "Available";
            case ChargePointStatus_Preparing:    return "Preparing";
            case ChargePointStatus_Charging:     return "Charging";
            case ChargePointStatus_SuspendedEVSE: return "SuspendedEVSE";
            case ChargePointStatus_SuspendedEV:   return "SuspendedEV";
            case ChargePointStatus_Finishing:    return "Finishing";
            case ChargePointStatus_Reserved:     return "Reserved";
            case ChargePointStatus_Unavailable:  return "Unavailable";
            case ChargePointStatus_Faulted:      return "Faulted";
            default: return "Unknown";
        }
    }

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

// Local session entry point: skips server Authorize.req.
// Use this for RFID swipes or physical button presses where the local
// firmware has already made the authorization decision.
bool ocpp::beginTransactionAuthorizedSafe(const char *idTag, unsigned int connectorId)
{
    OcppLock lock(5000); // Increased timeout to 5s for manual actions
    if (!lock.ok())
    {
        return false;
    }
    return beginTransaction_authorized(idTag, nullptr, connectorId);
}

bool ocpp::endTransactionSafe(const char *idTag, const char *reason, unsigned int connectorId)
{
    OcppLock lock(5000); // Increased timeout to 5s for manual actions
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
    // Guard: MicroOcpp logs a warning on every call if the context is not yet
    // initialized (before BootNotification completes). Suppress the spam by
    // checking the init flag first — charging is never permitted before OCPP
    // is up anyway.
    if (!SystemState::instance().getOcppInitialized())
    {
        return false;
    }

    OcppLock lock;
    if (!lock.ok())
    {
        return false;
    }
    return ocppPermitsCharge(connectorId);
}

// Unified connection (GSM + WiFi + Watchdog)
prod::UnifiedConnection g_ocppConnection;
prod::UnifiedConnection* prod::g_unifiedConnectionPtr = &g_ocppConnection;

bool ocpp::init()
{
    Serial.println("[OCPP] 🔌 Initializing OCPP...");

    // Load dynamic config from secure storage
    char chargerId[32] = {0}, csmsHost[128] = {0}, csmsUrl[256] = {0};
    uint16_t csmsPort = 443;
    
    if (SecureConfig::getOCPPConfig(csmsHost, csmsPort, chargerId, csmsUrl,
                                   sizeof(csmsHost), sizeof(chargerId), sizeof(csmsUrl)))
    {
        Serial.printf("[OCPP] 📍 StationId: %s\n", chargerId);
        Serial.printf("[OCPP] 🌐 CSMS Host: %s:%d\n", csmsHost, csmsPort);
        Serial.printf("[OCPP] 🔗 Full URL: %s/%s\n", csmsUrl, chargerId);
        
        // Configure the connection with real credentials
        g_ocppConnection.setServer(csmsHost, csmsPort, chargerId);
    }
    else
    {
        Serial.println("[OCPP] ❌ Failed to load OCPP config from secure storage!");
        Serial.println("[OCPP] ⚠️  Device requires provisioning. Run SecureConfig::migrateFromLegacySecrets().");
        return false;
    }

    // Require network (GSM or WiFi) before initializing
    if (!prod::g_networkManager.isConnected())
    {
        Serial.println("[OCPP] ⚠️  Network not connected - init deferred");
        return false;
    }
    Serial.printf("[OCPP] ✅ Network connected via %s\n",
                  prod::connectionTypeToString(prod::g_networkManager.getActiveConnection()));

    // ── STRICT TLS REQUIREMENT: Wait for NTP Time Sync ──
    Serial.println("[OCPP] ⏳ Waiting for system time sync (required for TLS cert validation)...");
    uint32_t ntpWaitStart = millis();
    bool timeValid = false;
    
    // Non-blocking wait loop for NTP sync (up to 30 seconds)
    while (millis() - ntpWaitStart < 30000) {
        time_t now = time(nullptr);
        // Threshold: 1735689600 = Jan 1, 2026.
        // Any time value below this means NTP has NOT synced (clock is at epoch 0 or stale).
        if (now > 1735689600L) {
            timeValid = true;
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            Serial.printf("[OCPP] ✅ System time synced: %s\n", timeStr);
            break;
        }
        g_healthMonitor.feed();         // Feed watchdog to prevent task crash
        vTaskDelay(pdMS_TO_TICKS(250)); // Yield to RTOS scheduler
    }

    if (!timeValid) {
        Serial.println("[OCPP] ❌ FATAL: NTP time sync failed!");
        Serial.println("[OCPP] ⚠️  TLS certificate validation requires accurate system time.");
        Serial.println("[OCPP] 🔄 Aborting OCPP init. Will retry in next poll cycle.");
        return false; // Safely abort and let the state machine retry
    }

    // Test network connectivity to server BEFORE initializing WebSocket
    // NOTE: WiFiClientSecure uses WiFi DNS — skip when connected via GSM
    if (prod::g_networkManager.getActiveConnection() == prod::ConnectionType::WIFI) {
        Serial.println("[OCPP] 🔍 Testing TCP connectivity to server...");
        Serial.printf("[OCPP] 🎯 Target: %s:%d\n", csmsHost, csmsPort);
        
        WiFiClientSecure testClient;
        testClient.setCACert(ISRG_ROOT_X1_CERT);
        bool serverReachable = testClient.connect(csmsHost, csmsPort, 10000);
        
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
        
        // Use the UnifiedConnection which handles GSM/WiFi and the idle watchdog.
        // It wraps the standard WebSocketsClient for WiFi and uses a custom
        // transport for GSM. Both enforce strict TLS using ISRG_ROOT_X1_CERT.
        mocpp_initialize(
            g_ocppConnection,
            ChargerCredentials(DEFAULT_CHARGER_MODEL, DEFAULT_CHARGER_VENDOR, FIRMWARE_VERSION),
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

    // ═══════════════════════════════════════════════════════════════
    // PHASE 1: Standard MicroOcpp Library Inputs (Production Alignment)
    // These lambdas are polled by MicroOcpp every loop(). When a fault
    // is detected, the library AUTOMATICALLY:
    //   1) Transitions connector to Faulted
    //   2) Sends StatusNotification(errorCode) to CSMS
    //   3) Blocks new RemoteStart until fault clears
    // ═══════════════════════════════════════════════════════════════

    // 1. Over-Temperature Fault
    addErrorDataInput([]() -> MicroOcpp::ErrorData {
        auto snap = SystemState::instance().snapshot();
        if (snap.chargerTemp > ALERT_TEMP_CRITICAL_C) {
            MicroOcpp::ErrorData err("HighTemperature");
            err.info = "Charger terminal temperature exceeded safe limit";
            err.vendorId = "RivotMotors";
            return err;
        }
        return MicroOcpp::ErrorData(nullptr);
    });

    // 2. Over/Under Voltage Fault
    // IMPORTANT: Only fires during active charging (chargingEnabled=true).
    // Post-charge residual battery voltage is normal — must NOT trigger a Faulted state.
    addErrorDataInput([]() -> MicroOcpp::ErrorData {
        auto snap = SystemState::instance().snapshot();
        if (snap.batteryConnected && snap.chargingEnabled && snap.terminalVolt > 0.0f) {
            if (snap.terminalVolt > ALERT_VOLTAGE_MAX_V) {
                MicroOcpp::ErrorData err("OverVoltage");
                err.info = "Terminal voltage exceeded maximum during active charge";
                err.vendorId = "RivotMotors";
                return err;
            }
            if (snap.terminalVolt < ALERT_VOLTAGE_MIN_V) {
                MicroOcpp::ErrorData err("UnderVoltage");
                err.info = "Terminal voltage below minimum during active charge";
                err.vendorId = "RivotMotors";
                return err;
            }
        }
        return MicroOcpp::ErrorData(nullptr);
    });

    // 3. BMS Communication Timeout
    addErrorDataInput([]() -> MicroOcpp::ErrorData {
        auto snap = SystemState::instance().snapshot();
        if (snap.transactionActive && (millis() - snap.lastBMS > 5000)) {
            MicroOcpp::ErrorData err("OtherError");
            err.info = "BMS CAN communication timeout (>5s)";
            err.vendorId = "RivotMotors";
            err.vendorErrorCode = "BMS_TIMEOUT";
            return err;
        }
        return MicroOcpp::ErrorData(nullptr);
    });

    // 3b. BMS CAN Controller Hardware Fault (MCP2515)
    // Triggers instantly if the CAN chip dies or goes Bus-Off, even while idle.
    addErrorDataInput([]() -> MicroOcpp::ErrorData {
        if (g_app.bms && !g_app.bms->isHardwareHealthy()) {
            MicroOcpp::ErrorData err("InternalError");
            err.info = "MCP2515 CAN Controller Hardware Fault / Bus-Off";
            err.vendorId = "RivotMotors";
            err.vendorErrorCode = "E_CAN2_FAULT";
            return err;
        }
        return MicroOcpp::ErrorData(nullptr);
    });


    // 4. HAL v1 STEP 3: Charger Module Fault via g_app.charger->hasFault()
    // Replaces legacy isChargerModuleHealthy() call.
    // CM1ChargerDriver reports hasFault()=true if telemetry hasn't been
    // received for >10 seconds (comm fault).
    addErrorDataInput([]() -> MicroOcpp::ErrorData {
        static bool canTimeoutLogged = false; // shared across both branches
        if (g_app.charger && g_app.charger->hasFault()) {
            if (!canTimeoutLogged) {
                SystemState::instance().setStopReason(StopReason::CAN_TIMEOUT);
                canTimeoutLogged = true;
            }
            MicroOcpp::ErrorData err("PowerSwitchFailure");
            err.info = "Charger module comm fault (HAL: no telemetry >10s)";
            err.vendorId = "RivotMotors";
            return err;
        }
        canTimeoutLogged = false; // reset when fault clears
        return MicroOcpp::ErrorData(nullptr);
    });

    Serial.println("[OCPP]   ✓ Standard Error Inputs registered (Temp, Voltage, BMS, Charger)");

    // ═══════════════════════════════════════════════════════════════
    // Smart Charging: CSMS can send SetChargingProfile to limit amps
    // Library calls this callback whenever the limit changes.
    // limitAmps = -1 means "no OCPP limit" → use BMS Imax.
    // ═══════════════════════════════════════════════════════════════
    setSmartChargingCurrentOutput([](float limitAmps) {
        if (limitAmps < 0) {
            // No OCPP limit defined — use BMS Imax (default behavior)
            return;
        }
        Serial.printf("[SMART_CHG] ⚡ OCPP current limit: %.1f A\n", limitAmps);
        
        auto& state = SystemState::instance();
        auto snap = state.snapshot();
        
        // Forward to hardware if charging is active.
        // The TransactionManager will also pick this up on its next poll,
        // but applying it here ensures immediate response for tests.
        if (g_app.charger && snap.transactionActive && snap.chargingEnabled) {
            float targetV = (snap.BMS_Vmax > 20.0f) ? snap.BMS_Vmax : 84.0f;
            g_app.charger->startCharging(targetV, limitAmps);
        }
    });
    Serial.println("[OCPP]   ✓ Smart Charging output registered");

    // ═══════════════════════════════════════════════════════════════
    // Remote Reset: CSMS can send Reset.req (Hard or Soft)
    // ═══════════════════════════════════════════════════════════════
    setOnResetNotify([](bool isHard) -> bool {
        Serial.printf("[RESET] 🔄 Reset.req received (hard=%d)\n", isHard);
        if (isTransactionRunning(1)) {
            Serial.println("[RESET] ❌ Rejected: Transaction in progress");
            return false;
        }
        return true;
    });

    setOnResetExecute([](bool isHard) {
        Serial.printf("[RESET] 🔄 Executing %s reset...\n", isHard ? "HARD" : "SOFT");
        delay(500); // Allow OCPP response to flush
        esp_restart();
    });
    Serial.println("[OCPP]   ✓ Remote Reset handler registered");

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

        // FIX: Inject custom HTTPS downloader into MicroOcpp.
        // Without this, MicroOcpp's default FtpClient rejects https:// URLs.
        // Must be called BEFORE setDownloadFileWriter so the lambda captures
        // the correct transport.
        getOcppContext()->setFtpClient(
            std::unique_ptr<prod::HttpOtaClient>(new prod::HttpOtaClient(ISRG_ROOT_X1_CERT))
        );
        Serial.println("[OCPP]   ✓ Custom HTTPS OTA downloader injected");

        if (auto fwService = getOcppContext()->getModel().getFirmwareService())
        {
            // FIX: Register build number so MicroOcpp detects version change
            // after OTA reboot and sends FirmwareStatusNotification(Installed).
            // Without this, buildNumber is empty and Installed is never sent.
            fwService->setBuildNumber(FIRMWARE_VERSION);
            Serial.printf("[OCPP]   ✓ Build number registered: %s\n", FIRMWARE_VERSION);

            fwService->setDownloadFileWriter(
                prod::OTAManager::onFirmwareData,
                [](MO_FtpCloseReason reason) {
                    prod::OTAManager::onDownloadComplete((int)reason);
                }
            );
            
            // Allow MicroOcpp to transition to "Installing" before rebooting
            fwService->setOnInstall([](const char* location) -> bool {

                // ── Post-deferred-reboot guard ────────────────────────────
                // After a deferred reboot, _updateValid is false (RAM cleared).
                // But OTA_SUCCESS was recorded in NVS. MicroOcpp re-fires the
                // install callback because it's still in "Installing" state.
                // We return true so MicroOcpp can detect version change via
                // setBuildNumber() and send FirmwareStatusNotification: Installed.
                if (prod::OTAManager::checkUpdateSuccess()) {
                    Serial.println("[OTA] ✅ Post-reboot: OTA already applied. "
                                   "Returning true for MicroOcpp Installed transition.");
                    g_persistence.recordLastError("BOOT_NORMAL"); // Clear flag
                    return true;
                }

                // ── Safety check: gun plugged or session active ──────────
                // Instead of rejecting (which locks the charger in Unavailable
                // forever), we defer: set a flag and return true so MicroOcpp
                // sends "Installing". hw_svc_task will reboot once gun unplugs.
                if (SystemState::instance().getGunPhysicallyConnected() ||
                    SystemState::instance().getTransactionActive()) {

                    Serial.println("[OTA] ⏳ INSTALL DEFERRED: Vehicle connected. "
                                   "Will reboot automatically when gun is unplugged.");
                    
                    // Notify CSMS so admin knows the charger is waiting
                    FaultQueue::push("OTA_DEFERRED",
                        "OTA install deferred: vehicle is physically connected. "
                        "Charger will reboot and apply firmware when gun is unplugged.",
                        FAULT_SEV_WARNING);
                } else {
                    Serial.println("[OTA] ⏳ MicroOcpp queued 'Installing'. Handing over to hw_svc_task for reboot.");
                }

                if (prod::OTAManager::isUpdateValid()) {
                    // ZERO ALLOCATION ARCHITECTURE:
                    // Instead of creating a new OtaRebootTask (which starves if priority is too low 
                    // or panics if heap is fragmented), we simply flag the reboot.
                    // The highly-reliable hw_svc_task (Priority 3) checks this flag every 5 seconds 
                    // and will execute the ESP.restart() safely.
                    prod::OTAManager::setDeferredReboot(true);
                    return true;
                } else {
                    Serial.println("[OTA] ❌ Firmware valid flag not set, aborting installation phase.");
                    return false;
                }
            });

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
    SystemState::instance().setOcppInitialized(true);

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
    // PHASE 4: Removed g_transactionManager.syncTransactionState() — library is single source of truth

    auto& state = SystemState::instance();

    // ═══════════════════════════════════════════════════════════════
    // RECONNECT DETECTION: Force StatusNotification re-send after WS reconnect.
    //
    // Root cause of "Firmware=Preparing, Server=Available" bug:
    //   1. Vehicle plugged in → firmware enters Preparing.
    //   2. WebSocket idle watchdog fires → WS torn down and reconnected.
    //   3. CSMS drops all charger state and assumes "Available".
    //   4. MicroOcpp never re-sends StatusNotification(Preparing) because the
    //      firmware state never changed — it only sends on state *transitions*.
    //
    // Fix: Detect the WS reconnection edge (false→true). On reconnect, reset
    // lastReportedStatus to -1 so the block below sees a "state change" and
    // forces MicroOcpp to re-queue StatusNotification with the current state.
    // ═══════════════════════════════════════════════════════════════
    // Declared here (before reconnect block) so it can be reset on reconnect.
    static int lastReportedStatus = -1;

    {
        static bool lastWsConnected = false;
        bool wsNowConnected = false;
        {
            OcppLock lock;
            if (lock.ok()) wsNowConnected = isOperative();
        }
        if (!lastWsConnected && wsNowConnected) {
            // Rising edge: WS just reconnected. Force status re-announcement.
            Serial.println("[OCPP] ✅ WebSocket reconnected — forcing StatusNotification re-sync with CSMS");
            lastReportedStatus = -1;

            // DEFENSIVE LAYER 2: Push FirmwareInfo via DataTransfer on every reconnect.
            // BootNotification carries firmwareVersion, but it is only sent once per power-on.
            // If the CSMS missed it (or stored NULL from a previous firmware), this message
            // lets the backend update the version field without waiting for a reboot.
            sendRequest("DataTransfer",
                []() -> std::unique_ptr<MicroOcpp::JsonDoc> {
                    auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(256));
                    JsonObject payload = doc->to<JsonObject>();
                    payload["vendorId"]  = "RivotMotors";
                    payload["messageId"] = "FirmwareInfo";

                    MicroOcpp::JsonDoc dataDoc(128);
                    JsonObject dataObj = dataDoc.to<JsonObject>();
                    dataObj["firmwareVersion"] = FIRMWARE_VERSION;
                    dataObj["model"]           = DEFAULT_CHARGER_MODEL;
                    dataObj["vendor"]          = DEFAULT_CHARGER_VENDOR;

                    String dataStr;
                    serializeJson(dataObj, dataStr);
                    payload["data"] = dataStr;
                    return doc;
                },
                [](JsonObject response) {
                    Serial.printf("[OCPP] ✅ FirmwareInfo acknowledged by CSMS (status=%s)\n",
                                  response["status"] | "Unknown");
                }
            );
            Serial.printf("[OCPP] 📤 FirmwareInfo queued → version=%s\n", FIRMWARE_VERSION);
        }
        lastWsConnected = wsNowConnected;
    }

    // ═══════════════════════════════════════════════════════════════
    // DIAGNOSTICS & HEALTH (EVERY 5S/10S)
    // ═══════════════════════════════════════════════════════════════
    int currentStatus = (int)getChargePointStatus(1);
    
    static bool vehicleInfoSentForCurrentState = false;
    static unsigned long lastVehicleInfoAttempt = 0;

    if (currentStatus != lastReportedStatus && currentStatus > 0) {
        lastReportedStatus = currentStatus;
        vehicleInfoSentForCurrentState = false; // Reset on every state change

        const char* statusNames[] = {
            "UNDEFINED", "Available", "Preparing", "Charging", 
            "SuspendedEVSE", "SuspendedEV", "Finishing", 
            "Reserved", "Unavailable", "Faulted"
        };
        const char* name = (currentStatus >= 0 && currentStatus <= 9) ? statusNames[currentStatus] : "UNKNOWN";
        Serial.printf("\n[OCPP_STATE] 🔄 Status changed to: %s\n\n", name);
    }

    // Polled-retry logic for VehicleInfo: Send exactly once per state, but wait for BMS readiness
    if (!vehicleInfoSentForCurrentState && 
        (currentStatus == ChargePointStatus_Preparing || currentStatus == ChargePointStatus_Finishing)) {
        
        if (millis() - lastVehicleInfoAttempt >= 3000) {
            lastVehicleInfoAttempt = millis();
            auto snap = state.snapshot();
            
            // Wait until BMS data is valid (Imax > 0) and we are online before attempting to send.
            if ((snap.gunPhysicallyConnected || snap.batteryConnected) && snap.BMS_Imax > 0.0f && ocpp::isConnected()) {
                
                // If handshake guard is active, delay marking it sent so it retries
                if (!(snap.transactionActive && snap.activeTransactionId <= 0)) {
                    uint8_t vehicleModel = 1;
                    float capacityAh = 30.0f; // Classic

                    if (snap.BMS_Imax >= 60.0f) {
                        vehicleModel = 3; // Max
                        capacityAh = 90.0f;
                    }
                    else if (snap.BMS_Imax >= 30.0f) {
                        vehicleModel = 2; // Pro
                        capacityAh = 60.0f;
                    }

                    // Range = (Capacity * SOC) * 2.7 km/Ah
                    float estimatedRange = capacityAh * (snap.socPercent / 100.0f) * 2.7f;

                    // Get VIN dynamically from NVS or fallback to default
                    Preferences prefs;
                    String vinStr = "ME9NP1411H2172005"; // Default
                    // Open in read/write mode (false) so it creates the namespace silently if NOT_FOUND
                    if (prefs.begin("config", false)) {
                        if (prefs.isKey("vin")) {
                            String stored = prefs.getString("vin", "");
                            if (stored.length() > 0) vinStr = stored;
                        }
                        prefs.end();
                    }

                    ocpp::sendVehicleInfo(snap.socPercent, snap.BMS_Imax, snap.terminalVolt, snap.terminalCurr, snap.chargerTemp, vehicleModel, estimatedRange, vinStr.c_str());
                    
                    vehicleInfoSentForCurrentState = true; // Mark as successfully queued
                }
            }
        }
    }

    static unsigned long lastHealthPoll = 0;
    if (millis() - lastHealthPoll >= 30000) {
        lastHealthPoll = millis();
        bool healthy = false;
        if (g_app.charger) {
            healthy = g_app.charger->isReady() && !g_app.charger->hasFault();
        }
        auto snap = state.snapshot();

        // LIVE TXID SYNC: MicroOcpp assigns TxId asynchronously after StartTransaction.conf.
        // If our SystemState still shows -1 but the library has a real ID, sync it now.
        if (snap.transactionActive && snap.activeTransactionId <= 0) {
            int libTxId = ocpp::getTransactionIdSafe(1);
            if (libTxId > 0) {
                state.setActiveTransactionId(libTxId);
                Serial.printf("[SYS] 🔄 TxId synced from library: %d\n", libTxId);
                
                // Persist now that we have the real ID
                char txnIdStr[32];
                snprintf(txnIdStr, sizeof(txnIdStr), "%d", libTxId);
                
                // Get the real idTag from the library transaction
                const char *realTag = "Unknown";
                auto tx = getTransaction(1); // 1 = connectorId
                if (tx && tx->getIdTag()) {
                    realTag = tx->getIdTag();
                }
                
                g_persistence.saveTransaction(txnIdStr, realTag);
                snap = state.snapshot(); // refresh for log below
                
                // State-based trigger for Charging state:
                // We send it right after TxId is synced, ensuring the handshake guard doesn't block it.
                if (snap.gunPhysicallyConnected || snap.batteryConnected) {
                    uint8_t vehicleModel = 1;
                    float capacityAh = 30.0f; // Classic

                    if (snap.BMS_Imax >= 60.0f) {
                        vehicleModel = 3; // Max
                        capacityAh = 90.0f;
                    }
                    else if (snap.BMS_Imax >= 30.0f) {
                        vehicleModel = 2; // Pro
                        capacityAh = 60.0f;
                    }

                    // Range = (Capacity * SOC) * 2.7 km/Ah
                    float estimatedRange = capacityAh * (snap.socPercent / 100.0f) * 2.7f;

                    // Reset handshake guard so it doesn't artificially block this
                    // sendVehicleInfo handles handshakeGuardStart internally but it won't be blocked here
                    // because activeTransactionId is now > 0.
                    // Get VIN dynamically from NVS or fallback to default
                    Preferences prefs;
                    String vinStr = "ME9NP1411H2172005"; // Default
                    // Open in read/write mode (false) so it creates the namespace silently if NOT_FOUND
                    if (prefs.begin("config", false)) {
                        if (prefs.isKey("vin")) {
                            String stored = prefs.getString("vin", "");
                            if (stored.length() > 0) vinStr = stored;
                        }
                        prefs.end();
                    }

                    ocpp::sendVehicleInfo(snap.socPercent, snap.BMS_Imax, snap.terminalVolt, snap.terminalCurr, snap.chargerTemp, vehicleModel, estimatedRange, vinStr.c_str());
                }
            }
        }

        // Single merged STATUS line: replaces separate [SYS] + [NET] + [CON] logs
        SafeSerial::printf("[SYS] @%lums | %s | Tx=%s TxId=%d | Chg=%s | Healthy=%s | GSM=%d WS=%d CSQ=%d\n",
                     millis(),
                     ocppStatusToString(getChargePointStatus(1)),
                     snap.transactionActive ? "Active" : "Idle",
                     snap.activeTransactionId,
                     snap.chargingEnabled ? "ON" : "OFF",
                     healthy ? "YES" : "NO",
                     (int)g_gsmManager.isConnected(),
                     (int)ocpp::isConnected(),
                     g_gsmManager.getSignalQuality());
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
        Serial.println("[OCPP] ⚠️  Skip VehicleInfo: Charger is not operative (Offline or Faulted)");
        return;
    }

    // RATE-LIMIT: Don't queue if previous VehicleInfo hasn't received a response yet
    static unsigned long pendingSince = 0;
    if (vehicleInfoPending) {
        if (millis() - pendingSince < 30000) {
            Serial.println("[OCPP] ⏳ VehicleInfo skipped — previous still pending in queue");
            return;
        } else {
            Serial.println("[OCPP] ⚠️  VehicleInfo pending lock timed out! Forcing clear.");
            vehicleInfoPending = false;
        }
    }

    // HANDSHAKE GUARD: Suppress DataTransfer during the StartTx handshake window.
    // If a transaction is active but TxId is still -1, the StartTransaction.conf is
    // still in-flight. Queuing a DataTransfer at this moment competes for the same
    // RequestQueue slot and causes "response doesn't match" errors, which can make
    // the device never receive its TxId — leading to a 140s timeout + DeAuthorized abort.
    // We suppress for 10 seconds after RemoteStart is accepted to let the conf through.
    auto& state = SystemState::instance();
    auto snap = state.snapshot();
    static unsigned long handshakeGuardStart = 0;
    if (snap.transactionActive && snap.activeTransactionId <= 0) {
        handshakeGuardStart = millis(); // arm the guard
        Serial.println("[OCPP] 🔒 VehicleInfo suppressed — waiting for StartTx.conf (TxId not yet assigned)");
        return;
    }
    if (millis() - handshakeGuardStart < 10000) {
        Serial.println("[OCPP] 🔒 VehicleInfo suppressed — handshake guard (10s window)");
        return;
    }

    // Validate: require BMS data (maxCurrent > 0 proves BMS is connected and responding).
    // NOTE: voltage is intentionally NOT required — it is 0.0 pre-charge (charger module off).
    // SOC=0 is also valid (vehicle at 0% charge).
    if (maxCurrent <= 0.0f) {
        Serial.printf("[OCPP] ⚠️  Skip VehicleInfo: maxCurrent=%.1fA (BMS not ready)\n", maxCurrent);
        return;
    }

    const char* modelName = "Unknown";
    if (model == 1) modelName = "Classic";
    else if (model == 2) modelName = "Pro";
    else if (model == 3) modelName = "Max";

    if (!SystemState::instance().getGunPhysicallyConnected()) {
        Serial.println("[OCPP] ⚠️  Skip VehicleInfo: Gun not physically connected (waiting for BMS CAN)");
        return;
    }

    // VehicleInfo state label — shows current OCPP state correctly
    const char* stateLabel = ocppStatusToString(getChargePointStatus(1));

    Serial.printf("\n[OCPP] 📤 Sending VehicleInfo (%s):\n", stateLabel);
    Serial.printf("  SOC=%.1f%% | Model=%s | Range=%.1fkm | MaxI=%.1fA | VIN=%s\n", soc, modelName, range, maxCurrent, vin);

    // ── Forensics: track this as the active operation ────────────────────
    // If a crash occurs during JSON serialisation or DataTransfer queueing,
    // the crash report will show lastActivity=SEND_VEHICLE_INFO instead of
    // TLS_CONNECT, which strongly points to Theory B (heap corruption).
    CrashForensics::setActivity(CrashForensics::ACT_SEND_VEHICLE_INFO);
    CrashForensics::updateHeap();  // capture heap BEFORE serialisation
    CrashForensics::persist();

    vehicleInfoPending = true;  // Mark as pending BEFORE queuing
    pendingSince = millis();

    // ── VIN OWNERSHIP FIX ───────────────────────────────────────────
    // BEFORE: `vin` was captured as `const char*` (raw pointer) in the
    // MicroOcpp-deferred lambda. By the time MicroOcpp calls the builder,
    // the caller's `vinStr` Arduino String had been destroyed. The dangling
    // pointer then read live OCPP message buffer memory, producing:
    //   "vin": "[2,\"fb1dbdfb-936c..."   ← actual OCPP WebSocket frame
    //
    // AFTER: We copy `vin` into a `std::string` on the heap NOW, before the
    // lambda is stored. The lambda captures `vinCopy` by VALUE, so it owns
    // its own allocation that outlives the caller's stack frame.
    std::string vinCopy(vin);  // heap-allocated copy, owned by the lambda

    sendRequest("DataTransfer",
        [soc, maxCurrent, model, range, modelName, vinCopy]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            MicroOcpp::JsonDoc dataDoc(256);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["soc"] = soc;
            dataObj["maxCurrent"] = maxCurrent;
            dataObj["model"] = modelName;
            dataObj["range"] = range;
            dataObj["vin"] = vinCopy.c_str();   // safe: std::string is owned by lambda
            dataObj["timestamp"] = millis();
            
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
            CrashForensics::setActivity(CrashForensics::ACT_IDLE);
            CrashForensics::persist();
        }
    );
}

void ocpp::sendSessionSummary(float finalSoc, double energyDelivered, float duration,
                               int txId, const char* stopReason,
                               float terminalVolt, float terminalCurr)
{
    OcppLock lock;
    if (!lock.ok())
    {
        return;
    }
    if (!isOperative()) {
        Serial.println("[OCPP] ⚠️  Offline! SessionSummary will be queued and sent upon reconnect.");
    }

    Serial.printf("\n[OCPP] 📤 Sending SessionSummary to CSMS:\n");
    Serial.printf("  TxId=%d | StopReason=%s\n", txId, stopReason);
    Serial.printf("  FinalSOC=%.1f%% | Energy=%.2fWh | Duration=%.1fmin\n", finalSoc, energyDelivered, duration);
    Serial.printf("  Terminal: V=%.1fV I=%.1fA\n\n", terminalVolt, terminalCurr);

    // Capture by value so lambda outlives this stack frame
    String stopReasonStr(stopReason);

    sendRequest("DataTransfer",
        [finalSoc, energyDelivered, duration, txId, stopReasonStr, terminalVolt, terminalCurr]()
            -> std::unique_ptr<MicroOcpp::JsonDoc>
        {
            // Inner data object (matches serial log format exactly)
            MicroOcpp::JsonDoc dataDoc(384);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["txId"]              = txId;
            dataObj["stopReason"]        = stopReasonStr.c_str();
            dataObj["finalSoc"]          = finalSoc;
            dataObj["energyDeliveredWh"] = energyDelivered;
            dataObj["durationMinutes"]   = duration;
            dataObj["terminalVolt"]      = terminalVolt;
            dataObj["terminalCurr"]      = terminalCurr;
            dataObj["timestamp"]         = millis();

            String dataStr;
            serializeJson(dataObj, dataStr);

            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(640));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"]   = "RivotMotors";
            payload["messageId"]  = "SessionSummary";
            payload["data"]       = dataStr;
            return doc;
        },
        [](JsonObject response) {
            const char* status = response["status"] | "Unknown";
            Serial.printf("[OCPP] ✅ SessionSummary acknowledged by CSMS: %s\n\n", status);
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
        Serial.println("[OCPP] ⚠️  Offline! BMSAlert will be queued and sent upon reconnect.");
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
        Serial.println("[OCPP] ⚠️  Offline! SystemAlert will be queued and sent upon reconnect.");
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
        Serial.println("[OCPP] ⚠️  Offline! ChargerStatus will be queued and sent upon reconnect.");
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

// ═══════════════════════════════════════════════════════════════════════
// sendHardwareFault — immediate DataTransfer for real hardware problems
// ═══════════════════════════════════════════════════════════════════════
// messageId is severity-dependent:
//   Critical / Warning  →  "HardwareFault"    (shows as fault in Admin UI)
//   Info                →  "DeviceDiagnostic" (boot report, not a fault)
void ocpp::sendHardwareFault(const char* code, const char* description, uint8_t severity)
{
    OcppLock lock;
    if (!lock.ok()) return;

    const char* sevStr = (severity >= 2) ? "Critical" :
                         (severity == 1) ? "Warning" : "Info";

    // Use a distinct messageId for informational diagnostics so they don't
    // appear as hardware faults in the CSMS Admin UI.
    const char* msgId  = (severity >= 1) ? "HardwareFault" : "DeviceDiagnostic";

    Serial.printf("[FAULT] 📡 DataTransfer/%s [%s]: %s — %s\n", msgId, sevStr, code, description);

    // Capture by String value (not const char*) — guards against dangling pointers
    // if MicroOcpp defers the builder lambda beyond the caller's stack frame.
    String codeStr(code);
    String descStr(description);
    String sevStrObj(sevStr);
    String msgIdStr(msgId);

    sendRequest("DataTransfer",
        [codeStr, descStr, sevStrObj, msgIdStr]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            // Inner data JSON
            MicroOcpp::JsonDoc dataDoc(384);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["faultCode"]       = codeStr.c_str();
            dataObj["description"]     = descStr.c_str();
            dataObj["severity"]        = sevStrObj.c_str();
            dataObj["uptimeMs"]        = (uint32_t)millis();
            dataObj["firmwareVersion"] = FIRMWARE_VERSION;

            String dataStr;
            serializeJson(dataObj, dataStr);

            // Outer OCPP DataTransfer wrapper
            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(640));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"]  = "RivotMotors";
            payload["messageId"] = msgIdStr.c_str();
            payload["data"]      = dataStr;
            return doc;
        },
        [codeStr, msgIdStr](JsonObject response) {
            Serial.printf("[FAULT] ✅ %s [%s] acknowledged by CSMS\n",
                          msgIdStr.c_str(), codeStr.c_str());
        }
    );
}

// ═══════════════════════════════════════════════════════════════════════
// drainFaultQueue — called from OCPP task loop every second
// Sends pending hardware faults one-at-a-time once OCPP is operative.
// ═══════════════════════════════════════════════════════════════════════
void ocpp::drainFaultQueue()
{
    if (!FaultQueue::hasItems()) return;

    // Only drain when OCPP is operative
    {
        OcppLock lock(200);
        if (!lock.ok()) return;
        if (!isOperative()) return;
        // lock released here at end of scope
    }

    // Pop one fault and send it — sendHardwareFault() takes its own fresh lock
    PendingFault f;
    if (FaultQueue::pop(f)) {
        sendHardwareFault(f.code, f.description, f.severity);
    }
}
