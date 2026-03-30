#ifndef OCPP_STATUS_LOGGER_H
#define OCPP_STATUS_LOGGER_H

#include "system/LogMacros.h"

/**
 * @file ocpp_status_logger.h
 * @brief Clean OCPP status reporting
 */

namespace OCPPStatusLogger {

// Print system status summary
inline void printSystemStatus(
    unsigned long uptime,
    bool wifiConnected,
    bool ocppConnected,
    const char* stateMachine,
    bool txActive,
    bool charging
) {
    LOG_SECTION_START("SYSTEM STATUS");
    
    char uptimeStr[32];
    snprintf(uptimeStr, sizeof(uptimeStr), "%lu s", uptime / 1000);
    LOG_DATA("Uptime", uptimeStr);
    LOG_DATA("WiFi", wifiConnected ? "Connected" : "Disconnected");
    LOG_DATA("OCPP", ocppConnected ? "Connected" : "Disconnected");
    LOG_DATA("State", stateMachine);
    LOG_DATA("Transaction", txActive ? "Active" : "Idle");
    LOG_DATA("Charging", charging ? "Enabled" : "Disabled");
    
    LOG_SECTION_END();
}

// Print vehicle metrics
inline void printVehicleMetrics(
    float voltage,
    float current,
    float soc,
    float range,
    float temp,
    float energy,
    const char* model
) {
    LOG_SECTION_START("VEHICLE METRICS");
    
    LOG_DATA_UNIT("Voltage", voltage, "V");
    LOG_DATA_UNIT("Current", current, "A");
    LOG_DATA_UNIT("State of Charge", soc, "%");
    LOG_DATA_UNIT("Range", range, "km");
    LOG_DATA_UNIT("Temperature", temp, "°C");
    LOG_DATA_UNIT("Energy Delivered", energy, "Wh");
    LOG_DATA("Model", model);
    
    LOG_SECTION_END();
}

// Print vehicle info being sent
inline void printVehicleInfoSent(
    float soc,
    const char* model,
    float range,
    float maxCurrent,
    const char* vin
) {
    LOG_SECTION_START("VEHICLE INFO (Sending to CSMS)");
    
    LOG_DATA_UNIT("State of Charge", soc, "%");
    LOG_DATA("Model", model);
    LOG_DATA_UNIT("Range", range, "km");
    LOG_DATA_UNIT("Max Current", maxCurrent, "A");
    LOG_DATA("VIN", vin);
    
    LOG_SECTION_END();
}

// Print session summary
inline void printSessionSummary(
    float finalSoc,
    float energyDelivered,
    float durationMinutes
) {
    LOG_SECTION_START("SESSION SUMMARY");
    
    LOG_DATA_UNIT("Final SOC", finalSoc, "%");
    LOG_DATA_UNIT("Energy Delivered", energyDelivered, "Wh");
    LOG_DATA_UNIT("Duration", durationMinutes, "min");
    
    LOG_SECTION_END();
}

// Print connection event
inline void printConnectionEvent(bool connected) {
    if(connected) {
        LOG_INFO(OCPP, "WebSocket connected to CSMS");
    } else {
        LOG_WARN(OCPP, "WebSocket disconnected from CSMS");
    }
}

// Print transaction event
inline void printTransactionEvent(const char* event, int txId, const char* idTag = nullptr) {
    if(strcmp(event, "START") == 0) {
        if(idTag) {
            LOG_INFO_F(OCPP, "Transaction started: ID=%d, Tag=%s", txId, idTag);
        } else {
            LOG_INFO_F(OCPP, "Transaction started: ID=%d", txId);
        }
    } else if(strcmp(event, "STOP") == 0) {
        LOG_INFO_F(OCPP, "Transaction stopped: ID=%d", txId);
    } else if(strcmp(event, "AUTHORIZED") == 0) {
        LOG_INFO_F(OCPP, "Transaction authorized: ID=%d", txId);
    } else if(strcmp(event, "REJECTED") == 0) {
        LOG_WARN_F(OCPP, "Transaction rejected: ID=%d", txId);
    }
}

// Print remote command
inline void printRemoteCommand(const char* command, bool accepted, const char* reason = nullptr) {
    if(accepted) {
        LOG_INFO_F(OCPP, "Remote command accepted: %s", command);
    } else {
        if(reason) {
            LOG_WARN_F(OCPP, "Remote command rejected: %s (%s)", command, reason);
        } else {
            LOG_WARN_F(OCPP, "Remote command rejected: %s", command);
        }
    }
}

} // namespace OCPPStatusLogger

#endif // OCPP_STATUS_LOGGER_H
