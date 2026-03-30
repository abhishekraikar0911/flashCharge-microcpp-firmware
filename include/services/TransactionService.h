#pragma once

#include <Arduino.h>
#include <MicroOcpp.h>

namespace prod {

/**
 * @file ocpp_transaction_manager.h
 * @brief Handles OCPP transaction lifecycle and state tracking.
 * 
 * PHASE 3: Simplified — validation is now handled by MicroOcpp's
 * addErrorDataInput() which blocks transactions when faults are active.
 * syncTransactionState() removed — library is the single source of truth.
 */
class OcppTransactionManager {
public:
    void begin();
    void registerConnectorInputs();
    
    // Notification Handlers (called by setTxNotificationOutput)
    void handleRemoteStart(MicroOcpp::Transaction* tx);
    void handleStartTx(MicroOcpp::Transaction* tx);
    void handleRemoteStop(MicroOcpp::Transaction* tx);
    void handleStopTx(MicroOcpp::Transaction* tx);
    
    // Local Session Controls
    void startLocalTransaction(const char* idTag);
    void stopLocalTransaction();
    
    // State Getters
    bool isStopTxPending() const { return _stopTxPending; }
    void clearStopTxPending() { _stopTxPending = false; }

private:
    bool _stopTxPending = false;
    unsigned long _txStartTime = 0;
    unsigned long _txStopTime = 0;
};

extern OcppTransactionManager g_transactionManager;

} // namespace prod
