#include "../../include/modules/ocpp_transaction_manager.h"
#include "../../include/ocpp/ocpp_client.h"
#include "../../include/ocpp_state_machine.h"
#include "../../include/modules/system_state.h"
#include "../../include/config/hardware.h"
#include "../../include/header.h"
#include "../../include/production_config.h"
#include <MicroOcpp.h>

namespace prod {

OcppTransactionManager g_transactionManager;

void OcppTransactionManager::begin() {
    Serial.println("[TX_MGR] Transaction manager started");
}

void OcppTransactionManager::registerConnectorInputs() {
    // 1. Plug Detection (Available -> Preparing)
    setConnectorPluggedInput([]() {
        return g_ocppStateMachine.isPlugConnected();
    });

    // 2. EV Ready (Preparing -> Charging if transaction active)
    setEvReadyInput([]() {
        // Simplified: if plug is connected, we consider EV ready
        // In a real J1772/Type2 system, this would check the Pilot signal state
        return g_ocppStateMachine.isPlugConnected();
    });

    // 3. EVSE Ready (Operative status)
    setEvseReadyInput([]() {
        return isChargerModuleHealthy();
    });

    Serial.println("[TX_MGR] ✓ Connector inputs registered (Plug, EvReady, EvseReady)");
}

bool OcppTransactionManager::validateRemoteStart(const char* idTag) {
    auto& state = SystemState::instance();
    auto snap = state.snapshot();

    Serial.println("[TX_MGR] 🔍 Pre-transaction safety validation...");

    if (!snap.bmsSafeToCharge) {
        Serial.println("[TX_MGR] ❌ REJECTED: BMS not ready");
        ocpp::sendChargerStatus(false, "BMS not ready - vehicle not safe to charge");
        return false;
    }

    if (snap.terminalVolt > ALERT_VOLTAGE_MAX_V || snap.terminalVolt < ALERT_VOLTAGE_MIN_V) {
        Serial.printf("[TX_MGR] ❌ REJECTED: Voltage out of range (%.1fV)\n", snap.terminalVolt);
        ocpp::sendChargerStatus(false, "Voltage out of safe range");
        return false;
    }

    if (snap.chargerTemp > ALERT_TEMP_CRITICAL_C) {
        Serial.printf("[TX_MGR] ❌ REJECTED: Temperature too high (%.1f°C)\n", snap.chargerTemp);
        ocpp::sendChargerStatus(false, "Temperature too high");
        return false;
    }

    if (!snap.chargerModuleOnline) {
        // Allow brief dip - logic handled in ocpp_manager or here?
        // Let's keep it simple for now as in the original code
        Serial.println("[TX_MGR] ❌ REJECTED: Charger module offline");
        ocpp::sendChargerStatus(false, "Charger module offline");
        return false;
    }

    if (snap.faultLockActive) {
        Serial.println("[TX_MGR] ❌ REJECTED: Fault recovery in progress");
        ocpp::sendChargerStatus(false, "Fault recovery in progress");
        return false;
    }

    // Delegate to SM
    bool acceptedBySM = g_ocppStateMachine.onRemoteStartTransaction(idTag, 1);
    if (!acceptedBySM) {
        Serial.println("[TX_MGR] ❌ REJECTED by State Machine");
        return false;
    }

    return true;
}

void OcppTransactionManager::handleRemoteStart(MicroOcpp::Transaction* tx) {
    auto& state = SystemState::instance();
    state.setActiveTransactionId(-1);
    state.setTransactionActive(false);
    state.setRemoteStartAccepted(false);

    const char* idTag = tx ? tx->getIdTag() : "Remote";
    if (validateRemoteStart(idTag)) {
        Serial.println("[TX_MGR] ✅ RemoteStart accepted");
        state.setRemoteStartAccepted(true);
    }
}

void OcppTransactionManager::handleStartTx(MicroOcpp::Transaction* tx) {
    auto& state = SystemState::instance();
    int txId = tx ? tx->getTransactionId() : -1;
    
    state.setActiveTransactionId(txId);
    state.setTransactionActive(true);
    state.setChargingEnabled(true);
    state.setTxStartTime(millis());
    _stopTxPending = false; // New session starts, clear guard

    Serial.printf("[TX_MGR] ▶️  Transaction STARTED: txId=%d\n", txId);
    
    g_ocppStateMachine.onTransactionStarted(1, "RemoteStart", txId);
}

void OcppTransactionManager::handleRemoteStop(MicroOcpp::Transaction* tx) {
    auto& state = SystemState::instance();
    int activeTx = state.getActiveTransactionId();
    
    // Sync if needed
    int serverTxId = tx ? tx->getTransactionId() : -1;
    if (serverTxId > 0 && serverTxId != activeTx) {
        state.setActiveTransactionId(serverTxId);
        activeTx = serverTxId;
    }

    if (g_ocppStateMachine.onRemoteStopTransaction(activeTx)) {
        state.setChargingEnabled(false);
        sendImmediateChargerStop();
        
        Serial.printf("[TX_MGR] 🛑 RemoteStop received, ending txId=%d\n", activeTx);
        ocpp::endTransactionSafe(nullptr, "Remote", 1);
    }
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
    _stopTxPending = true; // Guard ON

    g_persistence.clearTransaction();
    g_ocppStateMachine.onTransactionStopped(-1);
    
    Serial.println("[TX_MGR] ⏹️  Transaction stopped - all flags cleared (stopTxPending=ON)");
}

void OcppTransactionManager::syncTransactionState() {
    auto& state = SystemState::instance();
    
    // Skip if StopTx is pending confirmation (the bug fix)
    if (_stopTxPending) {
        if (!ocpp::isTransactionRunningSafe(1)) {
            _stopTxPending = false;
            Serial.println("[TX_MGR] ✅ StopTransaction confirmed by server — stopTxPending cleared");
        }
        return;
    }

    // Logic to sync library state with global flags if they drift
    bool running = ocpp::isTransactionRunningSafe(1);
    bool active = state.getTransactionActive();

    if (running && active) {
        int currentId = state.getActiveTransactionId();
        if (currentId <= 0) {
            int updatedId = ocpp::getTransactionIdSafe(1);
            if (updatedId > 0) {
                state.setActiveTransactionId(updatedId);
                
                // Also update persistence so it survives a crash/reboot
                char txnIdStr[32];
                snprintf(txnIdStr, sizeof(txnIdStr), "%d", updatedId);
                g_persistence.saveTransaction(txnIdStr, "RemoteStart");
                
                Serial.printf("[TX_MGR] 🔄 Corrected txId from %d to %d (Matched from Server)\n", currentId, updatedId);
            }
        }
    }
}

} // namespace prod
