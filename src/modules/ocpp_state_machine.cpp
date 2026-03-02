#include "../include/ocpp_state_machine.h"
#include "../include/secrets.h"
#include "../include/production_config.h"
#include "../include/health_monitor.h"
#include "../include/header.h"
#include "../include/ocpp/ocpp_client.h"
#include "../include/config/hardware.h"  // For ENABLE_TEST_MODE
#include <Arduino.h>
#include <ArduinoJson.h>

// External declarations
extern bool bmsSafeToCharge;
extern bool batteryConnected;

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
            Serial.printf("[OCPP_SM] 📋 Found persisted transaction: %s\n", txnId);
            Serial.println("[OCPP_SM] ⚠️  Clearing stale transaction - will be cleaned by OCPP manager");
            
            // FIX: Don't restore state machine to Charging
            // Let OCPP manager handle cleanup and sync with library
            currentState = ConnectorState::Available;
            stateEnterTime = millis();
            
            // Clear the persisted transaction immediately
            g_persistence.clearTransaction();
        }
        else
        {
            // No persisted transaction - check battery status for initial state
            if (batteryConnected)
            {
                // Battery connected → start in PREPARING state
                Serial.println("[OCPP_SM] 🔋 Battery is connected on startup - Starting in PREPARING state");
                currentState = ConnectorState::Preparing;
            }
            else
            {
                // Battery not connected → start in AVAILABLE state
                Serial.println("[OCPP_SM] 🔌 Battery NOT connected on startup - Starting in AVAILABLE state");
                currentState = ConnectorState::Available;
            }
            stateEnterTime = millis();
        }

        // Set up plug detection
        // TODO: Configure GPIO or CAN signal for plug detection
        lastPlugCheckTime = millis();

        // Note: RemoteStartTransaction and RemoteStopTransaction are handled
        // by setRequestHandler if using MicroOcpp's operation system
        // For now, handlers would be registered via setRequestHandler or
        // MicroOcpp's built-in transaction callbacks

        Serial.printf("[OCPP_SM] ✅ State machine ready (Initial state: %s)\n", getStateName());
    }

    void OCPPStateMachine::poll()
    {
        uint32_t now = millis();

        // ═══════════════════════════════════════════════════════════════
        // CRITICAL: HARDWARE HEALTH CHECK (Priority #1)
        // ═══════════════════════════════════════════════════════════════
        bool healthy = isChargerModuleHealthy();
        
        if (!healthy)
        {
            // Transition to Faulted if not already there
            if (currentState != ConnectorState::Faulted)
            {
                Serial.println("[OCPP_SM] 🚨 HARDWARE FAULT DETECTED - Transitioning to FAULTED");
                forceState(ConnectorState::Faulted);
            }
            // Block all other transitions while Faulted
            return; 
        }
        else if (currentState == ConnectorState::Faulted)
        {
            // Recovery: Hardware is healthy again, return to Available
            Serial.println("[OCPP_SM] ✅ HARDWARE RECOVERED - Returning to AVAILABLE");
            forceState(ConnectorState::Available);
            
            // CRITICAL: Reset cached states to force re-evaluation of battery/plug
            lastBatteryState = !batteryConnected; 
            lastPlugState = !isPlugConnected();
        }

        // Note: Charger health status is now handled by setEvseReadyInput in ocpp_manager.cpp
        // MicroOcpp library automatically manages connector status based on EVSE ready state

        // BATTERY CONNECTION STATE MANAGEMENT
        // Rule: Battery connected → PREPARING state
        //       Battery NOT connected → AVAILABLE state
        
        static uint32_t lastBatteryCheckTime = 0;
        
        if (now - lastBatteryCheckTime > PLUG_DEBOUNCE_MS)
        {
            lastBatteryCheckTime = now;
            
            if (batteryConnected != lastBatteryState)
            {
                lastBatteryState = batteryConnected;
                
                if (batteryConnected)
                {
                    // Battery connected → Transition to PREPARING
                    if (currentState == ConnectorState::Available)
                    {
                        Serial.println("[OCPP_SM] 🔋 Battery connected - Transitioning to PREPARING state");
                        forceState(ConnectorState::Preparing);
                    }
                    else if (currentState == ConnectorState::Finishing)
                    {
                        Serial.println("[OCPP_SM] 🔋 Battery connected (was in Finishing) - Transitioning to PREPARING");
                        forceState(ConnectorState::Preparing);
                        g_persistence.clearTransaction();
                    }
                }
                else
                {
                    // Battery NOT connected → Transition to AVAILABLE
                    if (currentState != ConnectorState::Available && currentState != ConnectorState::Faulted)
                    {
                        Serial.printf("[OCPP_SM] 🔌 Battery disconnected - Transitioning from %s to AVAILABLE\n", getStateName());
                        
                        // Clear any active transaction if battery was yanked out
                        if (currentState == ConnectorState::Charging || currentState == ConnectorState::Preparing)
                        {
                            Serial.println("[OCPP_SM] 🧹 Battery disconnected during transaction - clearing state");
                            g_persistence.clearTransaction();
                            g_healthMonitor.onTransactionEnded();
                        }
                        
                        forceState(ConnectorState::Available);
                    }
                }
            }
        }

        // Check for plug status changes (debounced) - based on terminal voltage
        if (now - lastPlugCheckTime > PLUG_DEBOUNCE_MS)
        {
            lastPlugCheckTime = now;
            bool currentPlugState = isPlugConnected();

            if (currentPlugState != lastPlugState)
            {
                Serial.printf("[OCPP_SM] 🔌 Plug state changed: %s\n",
                              currentPlugState ? "CONNECTED" : "DISCONNECTED");
                lastPlugState = currentPlugState;

                // CRITICAL: Let MicroOCPP handle state transitions automatically
                // Available → Preparing happens via setConnectorPluggedInput()
                // Preparing → Charging happens via beginTransaction()
                // We only track for internal logic, not force transitions
                
                if (!currentPlugState && currentState == ConnectorState::Finishing)
                {
                    // Only transition to Available after transaction fully ended
                    Serial.println("[OCPP_SM] 🔄 Plug removed after Finishing, ready for Available");
                    forceState(ConnectorState::Available);
                    g_persistence.clearTransaction();
                    g_healthMonitor.onTransactionEnded();
                }
            }
        }

        // Check for state-specific timeouts
        uint32_t stateAge = now - stateEnterTime;

        if (currentState == ConnectorState::Finishing && stateAge > FINISHING_TIMEOUT_MS)
        {
            // Timeout: Force transition to Available after 10s
            Serial.printf("[OCPP_SM] ⏱️  Finishing timeout (%.0f sec) - transitioning to Available\n",
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

        // Clear state machine to Available
        forceState(ConnectorState::Available);
        g_healthMonitor.onTransactionEnded();
        g_persistence.clearTransaction();
    }

    bool OCPPStateMachine::onRemoteStartTransaction(const char *idTag, int connectorId)
    {
        Serial.printf("[OCPP_SM] 📥 RemoteStartTransaction: %s (connector %d)\n", idTag, connectorId);

#if ENABLE_TEST_MODE
        // ═══════════════════════════════════════════════════════════════
        // TEST MODE BYPASS - Skip all hardware validation checks
        // ═══════════════════════════════════════════════════════════════
        Serial.println("\n╔═══════════════════════════════════════════════════════════════╗");
        Serial.println("║  ⚠️  TEST MODE ACTIVE - Bypassing Hardware Safety Checks    ║");
        Serial.println("╚═══════════════════════════════════════════════════════════════╝");
        Serial.printf("[TEST_MODE] 🔓 Accepting RemoteStart WITHOUT validation\n");
        Serial.printf("[TEST_MODE] 📊 Current state: chargerHealthy=%d bmsSafe=%d gunPhys=%d battConn=%d\n",
                     isChargerModuleHealthy(), bmsSafeToCharge, ::gunPhysicallyConnected, ::batteryConnected);
        Serial.println("[TEST_MODE] ✅ RemoteStart ACCEPTED (test mode bypass)");
        Serial.println("╚═══════════════════════════════════════════════════════════════╝\n");
        return true;
#endif

        if (!isChargerModuleHealthy())
        {
            Serial.printf("[OCPP_SM] ❌ Charger module OFFLINE - REJECTING RemoteStart (stationId=%s)\n", SECRET_CHARGER_ID);
            return false;
        }

        if (!bmsSafeToCharge)
        {
            Serial.printf("[OCPP_SM] ❌ BMS charging disabled - REJECTING RemoteStart (bmsSafeToCharge=%d)\n", bmsSafeToCharge);
            ocpp::sendBMSAlert("BMS_CHARGING_DISABLED", "Cannot start: BMS MOSFET is OFF");
            return false;
        }

        // Validation checks before accepting
        if (!isHardwareSafe())
        {
            Serial.println("[OCPP_SM] ❌ Hardware not safe for charging");
            return false;
        }

        // REMOVED: Don't reject based on state machine state
        // Let MicroOCPP library handle state validation
        // The library knows the true transaction state better than our local state machine

        if (!::gunPhysicallyConnected)
        {
            Serial.printf("[OCPP_SM] ❌ Gun not connected - REJECTING RemoteStart (gunPhys=%d)\n", 
                          ::gunPhysicallyConnected);
            return false;
        }

        // All checks passed - MicroOCPP will handle state transition to Preparing/Charging
        Serial.println("[OCPP_SM] ✅ RemoteStartTransaction accepted (MicroOCPP will manage state)");
        return true;
    }

    bool OCPPStateMachine::onRemoteStopTransaction(int transactionId)
    {
        Serial.printf("[OCPP_SM] 📤 RemoteStopTransaction: %d (currentState=%s)\n", transactionId, getStateName());

        // ROBUST STOP: Accept RemoteStop if we are in any active or "stuck" state
        // This helps recover when server and client are out of sync on transaction IDs
        bool isActive = (currentState == ConnectorState::Charging || 
                         currentState == ConnectorState::Preparing ||
                         currentState == ConnectorState::SuspendedEV ||
                         currentState == ConnectorState::SuspendedEVSE);
                         
        if (isActive || (currentState == ConnectorState::Finishing))
        {
            Serial.printf("[OCPP_SM] ✅ RemoteStop accepted (state=%s) - moving to Finishing\n", getStateName());
            forceState(ConnectorState::Finishing);
            return true;
        }
        else
        {
            Serial.printf("[OCPP_SM] ❌ RemoteStop REJECTED: state=%s, txId=%d (expected Charging/Preparing/Suspended)\n", 
                         getStateName(), transactionId);
        }

        return false;
    }

    bool OCPPStateMachine::isPlugConnected()
    {
        // CRITICAL: Detect vehicle connection via terminal voltage
        // When vehicle connects, terminal voltage appears (56-99V range)
        // terminalVolt is declared in header.h as global extern
        
        // Vehicle is connected if terminal voltage is in valid range
        bool voltageDetected = (::terminalVolt >= 56.0f && ::terminalVolt <= 99.0f);
        
        // Also check physical gun connection for safety
        bool plugged = ::gunPhysicallyConnected && voltageDetected;
        
        static bool lastState = false;
        if (plugged != lastState) {
            Serial.printf("[OCPP_SM] 🔌 Vehicle detection: gun=%d voltage=%.1fV → %s\n",
                         ::gunPhysicallyConnected, ::terminalVolt, plugged ? "CONNECTED" : "DISCONNECTED");
            lastState = plugged;
        }
        
        return plugged;
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
