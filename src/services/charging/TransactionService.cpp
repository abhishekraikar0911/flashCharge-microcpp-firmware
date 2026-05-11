#include "services/charging/TransactionService.h"
#include "services/ocpp/OcppClient.h"
#include "system/state/SystemState.h"
#include "services/charging/ChargerService.h"
#include "config/hardware.h"
#include "config/production_config.h"
#include "services/safety/HealthMonitor.h"
#include <MicroOcpp.h>
// HAL v1: AppContext for new driver interfaces
#include "app/AppContext.h"
#include "services/network/GsmManager.h"    // g_gsmManager — used in stop reason diagnostic
#include <cstring>                  // strcmp — used in stop reason diagnostic

namespace prod {

OcppTransactionManager g_transactionManager;

// ═══════════════════════════════════════════════════════════════
// (Post-tx cooldown removed — Finishing state is handled by library)
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// Standalone plug detection with 1-second debounce
// (Extracted from ocpp_state_machine.cpp — Phase 4 preparation)
// ═══════════════════════════════════════════════════════════════
static bool isVehiclePlugged() {
    auto snap = SystemState::instance().snapshot();
    bool rawPlugged = snap.gunPhysicallyConnected || snap.batteryConnected;
    static bool lastRawState = false;
    static uint32_t lastChangeTime = 0;
    static bool debouncedPlugged = false;

    if (rawPlugged != lastRawState) {
        lastRawState = rawPlugged;
        lastChangeTime = millis();
    }

    if (rawPlugged != debouncedPlugged && (millis() - lastChangeTime > 1000)) {
        debouncedPlugged = rawPlugged;
        Serial.printf("[PLUG] 🔌 Vehicle detection (debounced 1s): %s\n",
                     debouncedPlugged ? "CONNECTED" : "DISCONNECTED");
    }

    return debouncedPlugged;
}

void OcppTransactionManager::begin() {
    Serial.println("[TX_MGR] Transaction manager started");
}

void OcppTransactionManager::registerConnectorInputs() {
    // 1. Plug Detection (Available -> Preparing)
    // READINESS GATE: Only report "plugged" to MicroOcpp when BOTH conditions are met:
    //   a) Gun is physically detected (CAN data flowing, BMS responding)
    //   b) Terminal voltage >= 50V (vehicle discharge/battery switch is ON)
    // If voltage is < 50V (discharge switch OFF), the connector stays "Available" on the
    // CSMS dashboard, preventing operators from attempting a doomed RemoteStart.
    setConnectorPluggedInput([]() {
        static bool voltGateOpen = false;

        // If the gun is physically unplugged (BMS timeout), reset everything.
        if (!isVehiclePlugged()) {
            voltGateOpen = false;
            return false;
        }
        
        // Once the gate is open, STAY open until physically unplugged!
        // This completely eliminates noise or voltage dips from causing
        // the state to flap between Available and Preparing.
        if (voltGateOpen) {
            return true;
        }

        // If we are here, gun is plugged but gate is closed.
        // Wait for voltage to cross 50V to open the gate.
        float termV = SystemState::instance().getTerminalVolt();
        if (termV > 50.0f) {
            voltGateOpen = true;
            return true;
        }
        
        return false;
    });

    // 2. EV Ready (Preparing -> Charging if transaction active)
    // MUST be true only when actively charging — NOT just when gun is plugged.
    // If true while plugged but idle, MicroOcpp skips Finishing → goes to Preparing.
    // Correct: Charging → Finishing (evReady=false) → Preparing (plug=true, evReady=false)
    setEvReadyInput([]() {
        auto& s = SystemState::instance();
        return s.getChargingEnabled() && s.getGunPhysicallyConnected();
    });

    // 3. HAL v1 STEP 2: EVSE Ready → g_app.charger->isReady() and !hasFault()
    // Returns true only when CM1ChargerDriver has received telemetry recently.
    setEvseReadyInput([]() {
        if (!g_app.charger) return false;
        return g_app.charger->isReady() && !g_app.charger->hasFault();
    });

    // 4. PowerSwitchFailure: when power module (CAN1) is offline, report Faulted.
    // FIX: "Preparing" is misleading when the charger cannot deliver power.
    // With this error active, MicroOcpp emits StatusNotification=Faulted immediately
    // upon gun plug-in, instead of silently staying in Preparing.
    addErrorDataInput([]() -> const char* {
        if (!g_app.charger) return "PowerSwitchFailure";   // driver not initialized
        if (!g_app.charger->isReady())  return "PowerSwitchFailure";  // module offline
        if (g_app.charger->hasFault())  return "PowerSwitchFailure";  // module faulted
        return nullptr; // no fault
    });

    Serial.println("[OCPP] ✓ Connector inputs registered (Plug+VoltGate, EvReady, EvseReady, PowerSwitchFailure)");
}

// PHASE 3: validateRemoteStart() DELETED
// The library's addErrorDataInput() now handles all safety checks:
//   - HighTemperature → blocks RemoteStart automatically
//   - OverVoltage/UnderVoltage → blocks RemoteStart automatically
//   - PowerSwitchFailure (charger offline) → blocks RemoteStart automatically
//   - BMS Timeout → blocks RemoteStart automatically
// The library checks isOperative() which returns false when any error input is active.

void OcppTransactionManager::handleRemoteStart(MicroOcpp::Transaction* tx) {
    auto& state = SystemState::instance();
    state.setActiveTransactionId(-1);
    state.setTransactionActive(false);
    
    // PHASE 3: Library already validated all safety conditions via addErrorDataInput.
    // If we reach this callback, the library has already accepted the RemoteStart.
    state.setRemoteStartAccepted(true);
    Serial.println("[TX_MGR] ✅ RemoteStart accepted (validated by MicroOcpp library)");
}

void OcppTransactionManager::handleStartTx(MicroOcpp::Transaction* tx) {
    auto& state = SystemState::instance();
    int txId = tx ? tx->getTransactionId() : -1;
    
    // CRITICAL FIX: Reset session energy at the start of a transaction
    state.setEnergyWh(0.0);  // double precision reset (energyWh is double)
    state.setStopReason(StopReason::NONE); // Clear any stale stop reasons (e.g. from boot)

    state.setActiveTransactionId(txId);
    state.setTransactionActive(true);
    state.setChargingEnabled(true);
    state.setTxStartTime(millis());
    _stopTxPending = false;

    Serial.printf("[TX_MGR] ▶️  Transaction STARTED: txId=%d (Energy reset to 0)\n", txId);

    // Reset dynamic limit tracking so the first BMS update always gets applied
    ChargerService::instance().resetDynamicLimits();

    // HAL v1 STEP 4: Start the charger module through the new driver interface.
    // Uses BMS-reported voltage/current limits from SystemState.
    if (g_app.charger) {
        float bmsVmax = SystemState::instance().getBMS_Vmax();
        float bmsImax = SystemState::instance().getBMS_Imax();
        if (bmsVmax > 10.0f && bmsImax > 0.0f && SystemState::instance().getBmsSafeToCharge()) {
            g_app.charger->startCharging(bmsVmax, bmsImax);
            Serial.printf("[TX_MGR] ⚡ HAL charger started: Vmax=%.1fV Imax=%.1fA\n", bmsVmax, bmsImax);
        } else {
            Serial.println("[TX_MGR] ⚠️ BMS parameters invalid or safe-to-charge is false. Hardware charger NOT started!");
        }
    }

    // PHASE 3: library manages Available→Preparing→Charging internally.
    g_healthMonitor.onTransactionStarted();
    
    // Persist transaction for crash recovery
    if (txId > 0) {
        char txnIdStr[32];
        snprintf(txnIdStr, sizeof(txnIdStr), "%d", txId);
        
        // Use the actual idTag from the transaction or fallback to "Unknown"
        const char *tag = (tx && tx->getIdTag()) ? tx->getIdTag() : "Unknown";
        g_persistence.saveTransaction(txnIdStr, tag);
    }
}

void OcppTransactionManager::handleRemoteStop(MicroOcpp::Transaction* tx) {
    auto& state = SystemState::instance();
    int activeTx = state.getActiveTransactionId();

    // Sync txId if server has a different one (pick up late-assigned IDs)
    int serverTxId = tx ? tx->getTransactionId() : -1;
    if (serverTxId > 0 && serverTxId != activeTx) {
        state.setActiveTransactionId(serverTxId);
        activeTx = serverTxId;
    }

    // PHASE 3: Library accepted RemoteStop, always process.
    state.setChargingEnabled(false);

    // HAL v1 STEP 4: Stop the charger module through the new driver interface.
    if (g_app.charger) {
        g_app.charger->stopCharging();
        Serial.println("[TX_MGR] ⏹️  HAL charger stopCharging() via RemoteStop");
    } else {
        Serial.println("[TX_MGR] ⚠️  HAL driver null, cannot stop charging!");
    }

    SystemState::instance().setStopReason(StopReason::REMOTE);
    Serial.printf("[TX_MGR] 🛑 RemoteStop received, txId=%d\n", activeTx);
    // NOTE: Do NOT call endTransactionSafe() here.
    // MicroOcpp fires TxNotification_StopTx after this callback completes and
    // handles StopTransaction.req internally. A manual call creates a duplicate
    // StopTransaction that corrupts the RequestQueue with a response mismatch.
}

void OcppTransactionManager::handleStopTx(MicroOcpp::Transaction* tx) {
    auto& state = SystemState::instance();
    unsigned long now = millis();
    unsigned long start = state.getTxStartTime();
    float durationMin = (start > 0) ? (float)(now - start) / 60000.0f : 0.0f;
    
    auto snap = state.snapshot();
    const char* moReason = (tx && tx->getStopReason() && tx->getStopReason()[0] != '\0') ? tx->getStopReason() : "None";
    StopReason sysReasonEnum = state.getStopReason();
    const char* sysReason = stopReasonStr(sysReasonEnum);

    // ─────────────────────────────────────────────────────────────────────
    // DETAILED STOP REASON DIAGNOSTIC BLOCK
    // ─────────────────────────────────────────────────────────────────────
    Serial.println("\n[TX_MGR] ════════════════════════════════════════════");
    Serial.println("[TX_MGR]          ⏹️  CHARGING SESSION ENDED");
    Serial.println("[TX_MGR] ════════════════════════════════════════════");
    Serial.printf( "[TX_MGR]   TxId    : %d\n", snap.activeTransactionId);
    Serial.printf( "[TX_MGR]   Energy  : %.2f Wh\n", snap.energyWh);
    Serial.printf( "[TX_MGR]   SOC     : %.1f %%\n", snap.socPercent);
    Serial.printf( "[TX_MGR]   Duration: %.1f min\n", durationMin);
    Serial.printf( "[TX_MGR]   V=%.1fV  I=%.1fA  Temp=%.1f°C\n",
                   snap.terminalVolt, snap.terminalCurr, snap.chargerTemp);
    Serial.println("[TX_MGR] ────────────────────────────────────────────");
    Serial.printf( "[TX_MGR]   HW StopReason  : %s\n", sysReason);
    Serial.printf( "[TX_MGR]   OCPP StopReason: %s\n", moReason);
    Serial.println("[TX_MGR] ────────────────────────────────────────────");

    // Decode into human-readable explanation
    Serial.println("[TX_MGR] 🔍 STOP CAUSE ANALYSIS:");

    // 1. Check server-initiated stops (moReason is what the OCPP library set)
    if (strcmp(moReason, "Remote") == 0) {
        Serial.println("[TX_MGR]   ✅ STOPPED BY: CSMS Remote Command");
        Serial.println("[TX_MGR]   📋 Someone (operator/automation) sent RemoteStopTransaction from the server.");
        Serial.println("[TX_MGR]   ℹ️  Check CSMS logs for who/what triggered RemoteStop for this TxId.");

    } else if (strcmp(moReason, "DeAuthorized") == 0) {
        Serial.println("[TX_MGR]   ⚠️  STOPPED BY: CSMS De-Authorization");
        Serial.println("[TX_MGR]   📋 Server rejected/revoked the ID tag authorization mid-session.");
        Serial.println("[TX_MGR]   ℹ️  Or StartTransaction.conf never arrived → 140s timeout expired.");

    } else if (strcmp(moReason, "EmergencyStop") == 0) {
        Serial.println("[TX_MGR]   🚨 STOPPED BY: EMERGENCY STOP");
        if (sysReasonEnum == StopReason::EMERGENCY_STOP) {
            Serial.println("[TX_MGR]   📋 Physical E-Stop button (GPIO 32) was pressed by operator.");
            Serial.println("[TX_MGR]   ℹ️  Relay opened + FaultLock activated. Release button to recover.");
        } else {
            Serial.println("[TX_MGR]   📋 Over-temperature critical limit exceeded on charger terminal.");
            Serial.printf( "[TX_MGR]   ℹ️  Temperature was %.1f°C. Wait for cooling before retry.\n", snap.chargerTemp);
        }

    } else if (strcmp(moReason, "HardReset") == 0) {
        Serial.println("[TX_MGR]   🚨 STOPPED BY: CONTACT WELDING FAULT");
        Serial.println("[TX_MGR]   📋 Relay contact welding detected — voltage did not decay after stop.");
        Serial.println("[TX_MGR]   ℹ️  Hardware inspection required. Relay contacts may be fused.");

    } else if (strcmp(moReason, "Local") == 0) {
        // Local can be: STOP button press OR network loss
        if (sysReasonEnum == StopReason::NETWORK_LOSS) {
            Serial.println("[TX_MGR]   🌐 STOPPED BY: NETWORK LOSS");
            Serial.println("[TX_MGR]   📋 GSM/WiFi disconnected during session beyond COMM_LOSS_TIMEOUT.");
            Serial.printf( "[TX_MGR]   ℹ️  GSM=%d WS=%d. Check SIM card / signal. CSQ was likely low.\n",
                           (int)g_gsmManager.isConnected(), (int)ocpp::isConnected());
        } else {
            Serial.println("[TX_MGR]   🟢 STOPPED BY: Local STOP button (GPIO 26)");
            Serial.println("[TX_MGR]   📋 Operator pressed the physical STOP button on the charger.");
        }

    } else if (strcmp(moReason, "Other") == 0) {
        // 'Other' covers BMS stops, CAN timeout, network loss, overtemp
        if (sysReasonEnum == StopReason::BMS_FULL_CHARGE) {
            Serial.println("[TX_MGR]   ✅ STOPPED BY: CHARGE COMPLETE — BATTERY FULL");
            Serial.println("[TX_MGR]   📋 BMS byte5=0x01 fired at SOC=100%. This is a normal end-of-session.");
            Serial.printf( "[TX_MGR]   ℹ️  Energy delivered=%.2fWh | Duration=%.1fmin | V=%.1fV\n",
                           snap.energyWh, durationMin, snap.terminalVolt);
            Serial.println("[TX_MGR]   ℹ️  No fault. Vehicle is ready for use.");

        } else if (sysReasonEnum == StopReason::BMS_SWITCH_OFF) {
            Serial.println("[TX_MGR]   ⚠️  STOPPED BY: BMS CHARGER MOSFET TRIPPED (PROTECTION FAULT)");
            Serial.println("[TX_MGR]   📋 BMS byte5=0x01 fired but SOC was NOT 100% — this is a fault stop.");
            Serial.printf( "[TX_MGR]   ℹ️  SOC at stop=%.1f%%. Possible causes:\n", snap.socPercent);
            Serial.println("[TX_MGR]      - Single cell overvoltage / cell imbalance");
            Serial.println("[TX_MGR]      - BMS internal overtemperature");
            Serial.println("[TX_MGR]      - BMS overcurrent protection");
            Serial.println("[TX_MGR]      - BMS internal fault (check vehicle BMS diagnostic)");
            Serial.println("[TX_MGR]   ℹ️  FaultLock active — check LED. Will clear after stabilization.");

        } else if (sysReasonEnum == StopReason::BMS_TIMEOUT) {
            Serial.println("[TX_MGR]   📡 STOPPED BY: BMS CAN COMMUNICATION TIMEOUT");
            Serial.printf( "[TX_MGR]   📋 No CAN frames received from vehicle BMS for >10 seconds.");
            Serial.printf( "[TX_MGR]   ℹ️  BmsAge=%lums at stop. Check CAN2 (MCP2515) wiring & BMS power.\n",
                           (now - snap.lastBMS));
        } else if (sysReasonEnum == StopReason::CAN_TIMEOUT) {
            Serial.println("[TX_MGR]   ⚡ STOPPED BY: CHARGER MODULE CAN TIMEOUT (CAN1/TWAI)");
            Serial.println("[TX_MGR]   📋 Charger power module stopped sending telemetry for >10 seconds.");
            Serial.println("[TX_MGR]   ℹ️  Check CAN1 (ISO1050/TWAI) wiring to charger module. Module may have faulted.");
        } else if (sysReasonEnum == StopReason::OVERTEMP) {
            Serial.println("[TX_MGR]   🌡️  STOPPED BY: OVER-TEMPERATURE (CRITICAL LIMIT)");
            Serial.printf( "[TX_MGR]   📋 Charger terminal temp %.1f°C exceeded ALERT_TEMP_CRITICAL_C.\n", snap.chargerTemp);
            Serial.println("[TX_MGR]   ℹ️  Allow cooling. Check fan/ventilation. FaultLock active.");
        } else if (sysReasonEnum == StopReason::NETWORK_LOSS) {
            Serial.println("[TX_MGR]   🌐 STOPPED BY: NETWORK LOSS");
            Serial.println("[TX_MGR]   📋 GSM/WiFi disconnected during session beyond COMM_LOSS_TIMEOUT.");
            Serial.printf( "[TX_MGR]   ℹ️  GSM=%d WS=%d. Check SIM card / signal.\n",
                           (int)g_gsmManager.isConnected(), (int)ocpp::isConnected());
        } else {
            Serial.println("[TX_MGR]   ❓ STOPPED BY: UNKNOWN HARDWARE FAULT");
            Serial.printf( "[TX_MGR]   📋 moReason='Other' but sysReason='%s'. Manual investigation required.\n", sysReason);
        }

    } else if (strcmp(moReason, "EVDisconnected") == 0) {
        Serial.println("[TX_MGR]   🔌 STOPPED BY: EV DISCONNECTED");
        Serial.println("[TX_MGR]   📋 MicroOcpp detected pluggedInput()=false while transaction was active.");
        Serial.println("[TX_MGR]   ℹ️  Gun was physically unplugged OR BmsAge>timeout (CAN silent = 'gun gone').");

    } else if (strcmp(moReason, "None") == 0 && sysReasonEnum == StopReason::POWER_RESTART) {
        Serial.println("[TX_MGR]   🔄 STOPPED BY: ESP32 RESTART / CRASH");
        Serial.println("[TX_MGR]   📋 The ESP32 rebooted mid-session (power cut, watchdog, or panic crash).");
        Serial.println("[TX_MGR]   ℹ️  Check [System] Reset reason at top of boot log:");
        Serial.println("[TX_MGR]      ESP_RST_PANIC    → firmware crash (stack overflow / null deref)");
        Serial.println("[TX_MGR]      ESP_RST_TASK_WDT → task blocked >30s (deadlock / infinite loop)");
        Serial.println("[TX_MGR]      ESP_RST_BROWNOUT → power supply sag under load (check PSU)");
        Serial.println("[TX_MGR]      ESP_RST_SW       → deliberate esp_restart() from firmware");

    } else {
        // Catch-all for any unrecognized combination
        Serial.printf( "[TX_MGR]   ❓ STOPPED BY: UNCLASSIFIED (moReason='%s' sysReason='%s')\n", moReason, sysReason);
        Serial.println("[TX_MGR]   📋 Review serial log above for [STOP_TRIGGER] or [SAFETY] entries.");
    }

    Serial.println("[TX_MGR] ════════════════════════════════════════════\n");
    // ─────────────────────────────────────────────────────────────────────
    
    ocpp::sendSessionSummary(
        snap.socPercent,
        snap.energyWh,
        durationMin,
        snap.activeTransactionId,   // txId — same value shown in serial [SYS] log
        sysReason,                  // hardware stop reason, e.g. "BMS Charger Switch OFF"
        snap.terminalVolt,          // last known terminal voltage at stop
        snap.terminalCurr           // last known terminal current at stop
    );

    // Clear state
    state.setActiveTransactionId(-1);
    state.setTransactionActive(false);
    state.setChargingEnabled(false);
    state.setTxStopTime(now);
    _stopTxPending = true;

    // HAL v1 STEP 4: Ensure charger is stopped
    if (g_app.charger) {
        g_app.charger->stopCharging();
    } else {
        Serial.println("[TX_MGR] ⚠️  HAL driver null, cannot stop charging!");
    }

    g_persistence.clearTransaction();
    // PHASE 3: Removed g_ocppStateMachine.onTransactionStopped()
    // The library manages the Charging→Finishing→Available transition internally.
    g_healthMonitor.onTransactionEnded();
    
    // Let MicroOcpp handle the Charging -> Finishing -> Available transition naturally
    Serial.println("[TX_MGR] ⏹️  Transaction stopped - all flags cleared (stopTxPending=ON)");
}

// PHASE 3: syncTransactionState() DELETED
// This function existed only to fix the "Split Brain" drift between our custom
// state machine and the library. With the custom state machine removed, there is
// only one source of truth (the library), so there is nothing to sync.
//
// The _stopTxPending guard is now handled in handleStopTx/handleStartTx directly.

void OcppTransactionManager::startLocalTransaction(const char* idTag) 
{    
    // Safety check BEFORE sending to library
    if (!isVehiclePlugged()) {
        Serial.println("[TX_MGR] ⚠️ Cannot start local transaction: Gun not plugged in!");
        return;
    }
    
    if (g_app.charger && g_app.charger->hasFault()) {
        Serial.println("[TX_MGR] ⚠️ Cannot start local transaction: Charger implies fault!");
        return;
    }

    // Check BMS connection and safety state with distinct messages
    if (g_app.bms && !g_app.bms->isConnected()) {
        Serial.println("[TX_MGR] ⚠️ Cannot start: BMS not yet connected — no CAN frames received. Check BMS cable.");
        return;
    }
    if (!SystemState::instance().getBmsSafeToCharge()) {
        Serial.println("[TX_MGR] ⚠️ Cannot start: BMS Charger Switch is OFF (flag 0x01). Turn ON the vehicle charging switch.");
        return;
    }

    // VEHICLE DISCHARGE SWITCH CHECK:
    // Terminal voltage below 50V means the vehicle's discharge/battery contactor is open.
    // This happens when the vehicle's discharge switch is OFF even if the gun is connected.
    // Attempting to charge in this state causes a false OCPP "Faulted" state because the
    // charger output voltage cannot rise to meet the battery. Block start and inform the user.
    float termV = SystemState::instance().getTerminalVolt();
    if (termV < 50.0f) {
        Serial.printf("[TX_MGR] ⚠️ Cannot start: Terminal voltage too low (%.1fV < 50V).\n", termV);
        Serial.println("[TX_MGR] ⚠️ Vehicle discharge/battery switch appears to be OFF. Turn it ON before charging.");
        return;
    }

    Serial.printf("[TX_MGR] ⚡ Local Transaction Request: %s\n", idTag);
    
    // Dispatch to MicroOcpp API using the _authorized variant.
    // This skips the Authorize.req round-trip to the server — "LOCAL_ADMIN_1"
    // is a local decision (button press / serial command) and does not need
    // server approval.  MicroOcpp will still send StartTransaction + MeterValues
    // + StopTransaction to the CSMS as normal.
    bool success = ocpp::beginTransactionAuthorizedSafe(idTag, 1);
    if (!success) {
        Serial.println("[TX_MGR] ❌ MicroOcpp Native API refused local start.");
    }
}

void OcppTransactionManager::stopLocalTransaction() {
    Serial.println("[TX_MGR] 🛑 Local Transaction Stop Request");

    // Dispatch to MicroOcpp API.
    // This will natively move state to Finishing and trigger handleStopTx().
    bool success = ocpp::endTransactionSafe(nullptr, "Local", 1);
    if (!success) {
        Serial.println("[TX_MGR] ❌ MicroOcpp Native API refused local stop. (Reason: No active session OR Network Mutex Timeout - Try again)");
    }
}

} // namespace prod
