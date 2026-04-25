#include "services/TransactionService.h"
#include "services/OcppClient.h"
#include "system/SystemState.h"
#include "config/hardware.h"
#include "config/production_config.h"
#include "system/HealthMonitor.h"
#include <MicroOcpp.h>
// HAL v1: AppContext for new driver interfaces
#include "app/AppContext.h"

namespace prod {

OcppTransactionManager g_transactionManager;

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
    // PHASE 3: Uses standalone debounce instead of g_ocppStateMachine
    setConnectorPluggedInput([]() {
        return isVehiclePlugged();
    });

    // 2. EV Ready (Preparing -> Charging if transaction active)
    setEvReadyInput([]() {
        return isVehiclePlugged();
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

    Serial.println("[OCPP] ✓ Connector inputs registered (Plug, EvReady, EvseReady, PowerSwitchFailure)");
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

    state.setActiveTransactionId(txId);
    state.setTransactionActive(true);
    state.setChargingEnabled(true);
    state.setTxStartTime(millis());
    _stopTxPending = false;

    Serial.printf("[TX_MGR] ▶️  Transaction STARTED: txId=%d (Energy reset to 0)\n", txId);

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
    Serial.printf("[TX_MGR] 📊 Session Ended: Energy=%.2fWh SOC=%.1f%% Duration=%.1fmin\n", 
                  snap.energyWh, snap.socPercent, durationMin);
    
    ocpp::sendSessionSummary(snap.socPercent, snap.energyWh, durationMin);

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
    
    Serial.println("[TX_MGR] ⏹️  Transaction stopped - all flags cleared (stopTxPending=ON)");
}

// PHASE 3: syncTransactionState() DELETED
// This function existed only to fix the "Split Brain" drift between our custom
// state machine and the library. With the custom state machine removed, there is
// only one source of truth (the library), so there is nothing to sync.
//
// The _stopTxPending guard is now handled in handleStopTx/handleStartTx directly.

void OcppTransactionManager::startLocalTransaction(const char* idTag) {
    
    // Safety check BEFORE sending to library
    if (!isVehiclePlugged()) {
        Serial.println("[TX_MGR] ⚠️ Cannot start local transaction: Gun not plugged in!");
        return;
    }
    
    if (g_app.charger && g_app.charger->hasFault()) {
        Serial.println("[TX_MGR] ⚠️ Cannot start local transaction: Charger implies fault!");
        return;
    }

    if (!SystemState::instance().getBmsSafeToCharge()) {
        Serial.println("[TX_MGR] ⚠️ Cannot start local transaction: BMS reports NOT safe to charge (switch off or faulted)!");
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
        Serial.println("[TX_MGR] ❌ MicroOcpp Native API refused local stop (no active session?).");
    }
}

} // namespace prod
