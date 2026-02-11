#include "../include/ocpp_state_machine.h"
#include "../include/production_config.h"
#include "../include/health_monitor.h"
#include "../include/header.h"
#include "../include/ocpp/ocpp_client.h"
#include <Arduino.h>
#include <ArduinoJson.h>

// External declarations
extern bool bmsSafeToCharge;

namespace prod
{

    const char *STATE_NAMES[] = {
        "Available", "Preparing", "Charging", "SuspendedEVSE",
        "SuspendedEV", "Finishing", "Reserved", "Unavailable", "Faulted"};

    void OCPPStateMachine::init()
    {
        Serial.println("[OCPP_SM] 🔧 Initializing state machine");

        // Check if there's a persisted transaction on startup
        char txnId[32] = {0};
        char idTag[32] = {0};

        if (g_persistence.restoreTransaction(txnId, idTag, sizeof(txnId)))
        {
            Serial.printf("[OCPP_SM] 📋 Resuming persisted transaction: %s\n", txnId);
            
            // CRITICAL: Update global flags to reflect restored state
            transactionActive = true;
            activeTransactionId = atoi(txnId);
            strncpy(persistedIdTag, idTag, sizeof(persistedIdTag) - 1); // Store for library re-hydration
            remoteStartAccepted = true; // Assume accepted to allow remote commands
            
            currentState = ConnectorState::Charging;
            stateEnterTime = millis();
            g_healthMonitor.onTransactionStarted();
        }

        // Set up plug detection
        // TODO: Configure GPIO or CAN signal for plug detection
        lastPlugCheckTime = millis();

        // Note: RemoteStartTransaction and RemoteStopTransaction are handled
        // by setRequestHandler if using MicroOcpp's operation system
        // For now, handlers would be registered via setRequestHandler or
        // MicroOcpp's built-in transaction callbacks

        Serial.println("[OCPP_SM] ✅ State machine ready");
    }

    void OCPPStateMachine::poll()
    {
        uint32_t now = millis();

        // Note: Charger health status is now handled by setEvseReadyInput in ocpp_manager.cpp
        // MicroOcpp library automatically manages connector status based on EVSE ready state

        // Check for plug status changes (debounced)
        if (now - lastPlugCheckTime > PLUG_DEBOUNCE_MS)
        {
            lastPlugCheckTime = now;
            bool currentPlugState = isPlugConnected();

            if (currentPlugState != lastPlugState)
            {
                Serial.printf("[OCPP_SM] 🔌 Plug state changed: %s\n",
                              currentPlugState ? "CONNECTED" : "DISCONNECTED");
                lastPlugState = currentPlugState;

                // FIX 2: LOCK STATE MACHINE - Strict state transitions
                // Available → (EV Plugged) → Preparing
                if (currentPlugState && currentState == ConnectorState::Available)
                {
                    Serial.println("[OCPP_SM] 🔄 Plug connected, transitioning Preparing");
                    forceState(ConnectorState::Preparing);
                }
                
                // Finishing → (EV unplugged) → Available
                // FIX 2: NEVER send Available while in Preparing or Charging
                if (!currentPlugState && currentState == ConnectorState::Finishing)
                {
                    Serial.println("[OCPP_SM] 🔄 Plug removed, transitioning Available");
                    forceState(ConnectorState::Available);
                    g_persistence.clearTransaction();
                    g_healthMonitor.onTransactionEnded();
                }
                else if (!currentPlugState && (currentState == ConnectorState::Preparing || currentState == ConnectorState::Charging))
                {
                    // FIX 2: ABSOLUTE RULE - Once in Preparing/Charging, NEVER go back to Available
                    Serial.printf("[OCPP_SM] ⚠️  Plug removed but in %s state - keeping state (waiting for transaction end)\n", getStateName());
                }
            }
        }

        // Check for state-specific timeouts
        uint32_t stateAge = now - stateEnterTime;

        if (currentState == ConnectorState::Finishing && stateAge > FINISHING_TIMEOUT_MS)
        {
            // FOR TESTING: Force transition to Available after 10s timeout, even if EV still connected
            Serial.printf("[OCPP_SM] ⏱️  Finishing timeout (%.0f sec) - FORCING Available state (Testing)\n",
                          FINISHING_TIMEOUT_MS / 1000.0f);
            forceState(ConnectorState::Available);
            g_persistence.clearTransaction();
            g_healthMonitor.onTransactionEnded();
        }
    }

    void OCPPStateMachine::onTransactionStarted(int connectorId, const char *idTag, int transactionId)
    {
        // *** CRITICAL DIAGNOSTIC: Log function entry parameters ***
        Serial.printf("[OCPP_SM_DIAG] 🎯 onTransactionStarted() CALLED with:\n");
        Serial.printf("[OCPP_SM_DIAG]    connectorId=%d, idTag=%s, txId=%d\n", connectorId, idTag ? idTag : "NULL", transactionId);
        Serial.printf("[OCPP_SM_DIAG]    Current state BEFORE transition: %s\n", getStateName());
        
        Serial.printf("[OCPP_SM] ✅ Transaction started: %d (tag: %s)\n", transactionId, idTag);

        // CRITICAL FIX: Only persist valid transaction IDs (positive integers)
        // MicroOcpp returns -1 before StartTransaction.conf arrives
        if (transactionId > 0)
        {
            char txnIdStr[32];
            snprintf(txnIdStr, sizeof(txnIdStr), "%d", transactionId);
            g_persistence.saveTransaction(txnIdStr, idTag);
            Serial.printf("[OCPP_SM_DIAG] 💾 Persisted transaction: txId=%s, idTag=%s\n", txnIdStr, idTag);
        }
        else
        {
            Serial.printf("[OCPP_SM] ⚠️  Invalid txId=%d, not persisting (waiting for StartTransaction.conf)\n", transactionId);
        }

        // Only transition to Charging if not already there
        if (currentState != ConnectorState::Charging)
        {
            Serial.println("[OCPP_SM_DIAG] 🔄 Calling forceState(Charging)...");
            forceState(ConnectorState::Charging);
            g_healthMonitor.onTransactionStarted();
            Serial.println("[OCPP_SM_DIAG] ✅ Health monitor notified");
        }
        else
        {
            Serial.println("[OCPP_SM_DIAG] ℹ️  Already in Charging state, skipping redundant transition");
        }
    }

    void OCPPStateMachine::onTransactionStopped(int transactionId)
    {
        Serial.printf("[OCPP_SM] 🛑 Transaction stopped: %d\n", transactionId);

        forceState(ConnectorState::Finishing);
        g_healthMonitor.onTransactionEnded();
    }

    bool OCPPStateMachine::onRemoteStartTransaction(const char *idTag, int connectorId)
    {
        Serial.printf("[OCPP_SM] 📥 RemoteStartTransaction: %s (connector %d)\n", idTag, connectorId);

        // CRITICAL: Check charger module health FIRST
        if (!isChargerModuleHealthy())
        {
            Serial.println("[OCPP_SM] ❌ Charger module OFFLINE - REJECTING RemoteStart");
            Serial.println("[OCPP_SM] ⚠️  Connector is Unavailable - cannot start transaction");
            return false;  // Reject RemoteStart
        }

        // SAFETY: Check BMS charging permission
        if (!bmsSafeToCharge)
        {
            Serial.println("[OCPP_SM] ❌ BMS charging disabled - REJECTING RemoteStart");
            Serial.println("[OCPP_SM] ⚠️  BMS MOSFET is OFF (byte4=0x01)");
            ocpp::sendBMSAlert("BMS_CHARGING_DISABLED", "Cannot start: BMS MOSFET is OFF");
            return false;
        }

        // Validation checks before accepting
        if (!isHardwareSafe())
        {
            Serial.println("[OCPP_SM] ❌ Hardware not safe for charging");
            return false;
        }

        // Check connector state - must be Available or Preparing
        if (currentState != ConnectorState::Available && currentState != ConnectorState::Preparing)
        {
            Serial.printf("[OCPP_SM] ❌ Cannot start: connector in state %s (expected Available or Preparing)\n", getStateName());
            return false;
        }

        // Check plug is physically connected
        if (!isPlugConnected())
        {
            Serial.println("[OCPP_SM] ❌ Plug not connected, cannot start transaction");
            return false;
        }

        // All checks passed - transition to Preparing
        Serial.println("[OCPP_SM] ✅ RemoteStartTransaction accepted, moving to Preparing state");
        forceState(ConnectorState::Preparing);
        return true;
    }

    bool OCPPStateMachine::onRemoteStopTransaction(int transactionId)
    {
        Serial.printf("[OCPP_SM] 📤 RemoteStopTransaction: %d\n", transactionId);

        if (currentState == ConnectorState::Charging)
        {
            forceState(ConnectorState::Finishing);
            return true;
        }

        return false;
    }

    bool OCPPStateMachine::isPlugConnected()
    {
        // Check based on hardware signal from CAN bus
        // Use :: to access global namespace variable
        // BOTH gun physical connection AND battery BMS communication required
        return ::gunPhysicallyConnected && ::batteryConnected;
    }

    bool OCPPStateMachine::isHardwareSafe()
    {
        // Check all safety conditions
        // extern float chargerTemp;
        // if (chargerTemp > 80.0f) return false; // Overheat

        // Check voltages, currents, etc.
        // extern float chargerVolt, chargerCurr;

        // Check for faults from health monitor
        if (g_healthMonitor.checkHardwareFault())
        {
            return false;
        }

        return true;
    }

    const char *OCPPStateMachine::getStateName() const
    {
        int idx = static_cast<int>(currentState);
        if (idx >= 0 && idx < 9)
        {
            return STATE_NAMES[idx];
        }
        return "Unknown";
    }

    uint32_t OCPPStateMachine::getStateTimeMs() const
    {
        return millis() - stateEnterTime;
    }

    void OCPPStateMachine::forceState(ConnectorState newState)
    {
        // *** DIAGNOSTIC: Always log state transition attempts ***
        const char *newStateName = (newState >= ConnectorState::Available && newState <= ConnectorState::Faulted)
                                      ? STATE_NAMES[static_cast<int>(newState)]
                                      : "Unknown";
        const char *oldStateName = getStateName();
        
        Serial.printf("[OCPP_SM_DIAG] 🔄 forceState() called: %s → %s (check if same)\n", oldStateName, newStateName);
        
        if (currentState == newState) {
            Serial.printf("[OCPP_SM_DIAG] ℹ️  Already in %s state, no change\n", newStateName);
            return;
        }

        Serial.printf("[OCPP_SM] 🔄 State: %s → %s ✅ TRANSITION APPLIED\n", oldStateName, newStateName);

        currentState = newState;
        stateEnterTime = millis();
        
        Serial.printf("[OCPP_SM_DIAG] ✅ currentState updated to: %d (%s), time=%u\n", 
                     static_cast<int>(newState), newStateName, stateEnterTime);
    }

    OCPPStateMachine g_ocppStateMachine;

} // namespace prod
