#include "../include/ocpp_state_machine.h"
#include "../include/production_config.h"
#include "../include/health_monitor.h"
#include "../include/header.h"
#include "../include/ocpp/ocpp_client.h"
#include <Arduino.h>
#include <ArduinoJson.h>

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

        // CRITICAL: Check charger module health and update OCPP status
        static unsigned long lastHealthCheck = 0;
        if (now - lastHealthCheck >= 2000) // Check every 2 seconds
        {
            bool chargerHealthy = isChargerModuleHealthy();
            
            // If charger goes offline, send Faulted status
            if (!chargerHealthy && currentState != ConnectorState::Faulted)
            {
                Serial.println("[OCPP_SM] ❌ Charger module offline - sending Faulted status");
                forceState(ConnectorState::Faulted);
                
                // CRITICAL: Tell MicroOcpp to send Faulted status
                ocpp::notifyChargerFault(true);
            }
            // If charger comes back online and we're in Faulted state, recover
            else if (chargerHealthy && currentState == ConnectorState::Faulted)
            {
                Serial.println("[OCPP_SM] ✅ Charger module recovered - sending Available status");
                forceState(ConnectorState::Available);
                
                // CRITICAL: Tell MicroOcpp fault is cleared
                ocpp::notifyChargerFault(false);
            }
            
            lastHealthCheck = now;
        }

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

                // Automatic transition from Finishing to Available when plug is removed
                if (!currentPlugState && currentState == ConnectorState::Finishing)
                {
                    Serial.println("[OCPP_SM] 🔄 Plug removed, transitioning Available");
                    forceState(ConnectorState::Available);
                    g_persistence.clearTransaction();
                    g_healthMonitor.onTransactionEnded();
                }
            }
        }

        // Check for state-specific timeouts
        uint32_t stateAge = now - stateEnterTime;

        // Finishing state timeout (60 seconds max)
        if (currentState == ConnectorState::Finishing && stateAge > FINISHING_TIMEOUT_MS)
        {
            Serial.printf("[OCPP_SM] ⏱️  Finishing timeout (%.0f sec) - forcing Available\n",
                          FINISHING_TIMEOUT_MS / 1000.0f);
            forceState(ConnectorState::Available);
            g_persistence.clearTransaction();
            g_healthMonitor.onTransactionEnded();
        }
    }

    void OCPPStateMachine::onTransactionStarted(int connectorId, const char *idTag, int transactionId)
    {
        Serial.printf("[OCPP_SM] ✅ Transaction started: %d (tag: %s)\n", transactionId, idTag);

        char txnIdStr[32];
        snprintf(txnIdStr, sizeof(txnIdStr), "%d", transactionId);
        g_persistence.saveTransaction(txnIdStr, idTag);

        forceState(ConnectorState::Charging);
        g_healthMonitor.onTransactionStarted();
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
            Serial.println("[OCPP_SM] ❌ Charger module OFFLINE - cannot start transaction");
            Serial.println("[OCPP_SM] ⚠️  Please check: Charger PCB power, CAN bus connection");
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
        return ::gunPhysicallyConnected;
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
        if (currentState == newState)
            return;

        Serial.printf("[OCPP_SM] 🔄 State: %s → %s\n", getStateName(),
                      (newState >= ConnectorState::Available && newState <= ConnectorState::Faulted)
                          ? STATE_NAMES[static_cast<int>(newState)]
                          : "Unknown");

        currentState = newState;
        stateEnterTime = millis();
    }

    OCPPStateMachine g_ocppStateMachine;

} // namespace prod
