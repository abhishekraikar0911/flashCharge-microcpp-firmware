#pragma once

#include <Arduino.h>
#include <MicroOcpp.h>

namespace prod {

/**
 * @file ocpp_transaction_manager.h
 * @brief Handles OCPP transaction lifecycle, safety validation, and state tracking.
 */
class OcppTransactionManager {
public:
    void begin();
    void registerConnectorInputs();
    
    // Safety validation for RemoteStart
    bool validateRemoteStart(const char* idTag);
    
    // Notification Handlers
    void handleRemoteStart(MicroOcpp::Transaction* tx);
    void handleStartTx(MicroOcpp::Transaction* tx);
    void handleRemoteStop(MicroOcpp::Transaction* tx);
    void handleStopTx(MicroOcpp::Transaction* tx);
    
    // State Getters
    bool isStopTxPending() const { return _stopTxPending; }
    void clearStopTxPending() { _stopTxPending = false; }
    
    // Transaction Sync Logic (Move from poll())
    void syncTransactionState();

private:
    bool _stopTxPending = false;
    unsigned long _txStartTime = 0;
    unsigned long _txStopTime = 0;
    
    // Internal safety checks
    bool checkBmsSafety();
    bool checkVoltageRange();
    bool checkTemperature();
    bool checkChargerHealth();
    bool checkFaultLock();
};

extern OcppTransactionManager g_transactionManager;

} // namespace prod
