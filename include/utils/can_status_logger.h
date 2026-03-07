#ifndef CAN_STATUS_LOGGER_H
#define CAN_STATUS_LOGGER_H

#include "utils/log_macros.h"
#include "driver/twai.h"

/**
 * @file can_status_logger.h
 * @brief Clean CAN bus status reporting
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

// Print formatted CAN status report
inline void printStatusReport(const twai_status_info_t& status) {
    LOG_SECTION_START("CAN BUS 1 (CHARGER) STATUS");
    
    LOG_DATA("State", getStateStr(status.state));
    LOG_DATA("TX Error Counter", status.tx_error_counter);
    LOG_DATA("RX Error Counter", status.rx_error_counter);
    LOG_DATA("TX Failed Count", status.tx_failed_count);
    LOG_DATA("RX Missed Count", status.rx_missed_count);
    LOG_DATA("Arbitration Lost", status.arb_lost_count);
    LOG_DATA("Bus Error Count", status.bus_error_count);
    LOG_DATA("TX Queue", status.msgs_to_tx);
    LOG_DATA("RX Queue", status.msgs_to_rx);
    
    LOG_SECTION_END();
}

// Print formatted MCP2515 status report (CAN2)
inline void printMCP2515Status(const char* state, uint8_t tec, uint8_t rec, uint32_t total_rx, uint32_t total_tx) {
    LOG_SECTION_START("CAN BUS 2 (BMS) STATUS");
    
    LOG_DATA("State", state);
    LOG_DATA("TX Error Counter", tec);
    LOG_DATA("RX Error Counter", rec);
    LOG_DATA("Total RX Success", total_rx);
    LOG_DATA("Total TX Success", total_tx);
    
    LOG_SECTION_END();
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
