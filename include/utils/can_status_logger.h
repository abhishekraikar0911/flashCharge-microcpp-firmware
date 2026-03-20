#ifndef CAN_STATUS_LOGGER_H
#define CAN_STATUS_LOGGER_H

#include "utils/log_macros.h"
#include "utils/safe_serial.h"
#include "driver/twai.h"

/**
 * @file can_status_logger.h
 * @brief Clean CAN bus status reporting (single-line format)
 */

namespace CANStatusLogger {

// Print CAN state as readable string
inline const char* getStateStr(twai_state_t state) {
    switch(state) {
        case TWAI_STATE_STOPPED:     return "STOPPED";
        case TWAI_STATE_RUNNING:     return "RUNNING";
        case TWAI_STATE_BUS_OFF:     return "BUS-OFF";
        case TWAI_STATE_RECOVERING:  return "RECOVERING";
        default:                     return "UNKNOWN";
    }
}

// Single-line CAN1 status (replaces 12-line Unicode box)
inline void printStatusReport(const twai_status_info_t& status) {
    SafeSerial::printf("[CAN1] %s | TEC=%d REC=%d TxFail=%d RxMiss=%d ArbLost=%d BusErr=%d TxQ=%d RxQ=%d\n",
        getStateStr(status.state),
        status.tx_error_counter, status.rx_error_counter,
        status.tx_failed_count, status.rx_missed_count,
        status.arb_lost_count, status.bus_error_count,
        status.msgs_to_tx, status.msgs_to_rx);
}

// Single-line CAN2 (BMS) status
inline void printMCP2515Status(const char* state, uint8_t tec, uint8_t rec, uint32_t total_rx, uint32_t total_tx) {
    SafeSerial::printf("[CAN2] %s | TEC=%d REC=%d RX=%lu TX=%lu\n",
        state, tec, rec, total_rx, total_tx);
}

// Print diagnostic analysis
inline void printDiagnostics(const twai_status_info_t& status) {
    LOG_SECTION_START("CAN BUS DIAGNOSTICS");
    
    // State analysis
    if(status.state == TWAI_STATE_BUS_OFF) {
        LOG_ERROR(CAN, "Bus-off state detected");
        
        if(status.tx_error_counter > 127) {
            LOG_ERROR_F(CAN, "TX errors critical: %d (>127)", status.tx_error_counter);
            Serial.println("\n  Possible causes:");
            Serial.println("    • Charger module not responding");
            Serial.println("    • Wrong CAN ID configuration");
            Serial.println("    • Missing termination resistors");
            Serial.println("    • Hardware connection issue");
        }
        
        if(status.rx_error_counter > 127) {
            LOG_ERROR_F(CAN, "RX errors critical: %d (>127)", status.rx_error_counter);
            Serial.println("\n  Possible causes:");
            Serial.println("    • Excessive noise on bus");
            Serial.println("    • Incorrect baud rate");
            Serial.println("    • Poor signal quality");
        }
    } else if(status.state == TWAI_STATE_RUNNING) {
        LOG_INFO(CAN, "Bus operating normally");
    }
    
    // Error analysis
    if(status.bus_error_count > 20) {
        LOG_WARN_F(CAN, "High bus errors: %d", status.bus_error_count);
    }
    
    if(status.tx_failed_count > 0) {
        LOG_WARN_F(CAN, "TX failures detected: %d", status.tx_failed_count);
    }
    
    if(status.rx_missed_count > 0) {
        LOG_WARN_F(CAN, "RX messages missed: %d", status.rx_missed_count);
    }
    
    LOG_SECTION_END();
}

// Print recovery progress
inline void printRecoveryStep(int step, int total, const char* description) {
    LOG_INFO_F(CAN, "Recovery %d/%d: %s", step, total, description);
}

// Print recovery complete
inline void printRecoveryComplete(bool success) {
    if(success) {
        LOG_INFO(CAN, "Recovery sequence completed successfully");
    } else {
        LOG_ERROR(CAN, "Recovery sequence failed");
    }
}

} // namespace CANStatusLogger

#endif // CAN_STATUS_LOGGER_H
