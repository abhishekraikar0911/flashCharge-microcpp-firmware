#include "../../include/modules/ocpp_transaction_manager.h"
#include "../../include/ocpp/ocpp_client.h"
#include "../../include/modules/system_state.h"
#include "../../include/config/hardware.h"
#include "../../include/header.h"
#include "../../include/production_config.h"
#include "../../include/health_monitor.h"
#include <MicroOcpp.h>

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

    // 3. EVSE Ready (Operative status)
    setEvseReadyInput([]() {
        return isChargerModuleHealthy();
    });

    Serial.println("[OCPP] ✓ Connector inputs registered (Plug, EvReady, EvseReady)");
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
    state.setEnergyWh(0.0f);

    state.setActiveTransactionId(txId);
    state.setTransactionActive(true);
    state.setChargingEnabled(true);
    state.setTxStartTime(millis());
    _stopTxPending = false;

    Serial.printf("[TX_MGR] ▶️  Transaction STARTED: txId=%d (Energy reset to 0)\n", txId);
    
    // PHASE 3: Removed g_ocppStateMachine.onTransactionStarted()
    // The library manages the Available→Preparing→Charging transition internally.
    g_healthMonitor.onTransactionStarted();
    
    // Persist transaction for crash recovery
    if (txId > 0) {
        char txnIdStr[32];
        snprintf(txnIdStr, sizeof(txnIdStr), "%d", txId);
        g_persistence.saveTransaction(txnIdStr, "RemoteStart");
    }
}

void OcppTransactionManager::handleRemoteStop(MicroOcpp::Transaction* tx) {
    auto& state = SystemState::instance();
    int activeTx = state.getActiveTransactionId();
    
    // Sync txId if server has a different one
    int serverTxId = tx ? tx->getTransactionId() : -1;
    if (serverTxId > 0 && serverTxId != activeTx) {
        state.setActiveTransactionId(serverTxId);
        activeTx = serverTxId;
    }

    // PHASE 3: Removed g_ocppStateMachine.onRemoteStopTransaction() gate.
    // The library accepted the RemoteStop, so we always process it.
    state.setChargingEnabled(false);
    sendImmediateChargerStop();
    
    Serial.printf("[TX_MGR] 🛑 RemoteStop received, ending txId=%d\n", activeTx);
    ocpp::endTransactionSafe(nullptr, "Remote", 1);
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

} // namespace prod
