#pragma once

/**
 * @file ocpp_manager.h
 * @brief OCPP Protocol Manager Interface
 * @author Rivot Motors
 * @date 2026
 *
 * This module manages OCPP (Open Charge Point Protocol) communication
 * for charge point functionality.
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// ========== TRANSACTION STATES ==========

typedef enum
{
    OCPP_IDLE = 0,
    OCPP_CHARGING = 1,
    OCPP_SUSPENDED = 2,
    OCPP_FINISHED = 3,
    OCPP_ERROR = 4
} OcppTransactionState;

// ========== CONNECTION STATES ==========

typedef enum
{
    OCPP_DISCONNECTED = 0,
    OCPP_CONNECTING = 1,
    OCPP_CONNECTED = 2,
    OCPP_ERROR = 3
} OcppConnectionState;

// ========== TRANSACTION DATA ==========

struct OcppTransaction
{
    uint32_t transaction_id;
    OcppTransactionState state;
    uint32_t start_time;
    uint32_t end_time;
    uint32_t energy_wh;
    char connector_id[16];
    char user_id[32];
    bool is_active;
};

// ========== CONNECTION STATUS ==========

struct OcppStatus
{
    OcppConnectionState connection_state;
    bool authenticated;
    OcppTransaction current_transaction;
    uint32_t heartbeat_interval_s;
    uint32_t meter_value_interval_s;
    const char *central_system_url;
    uint32_t last_heartbeat_ms;
    uint32_t reconnect_attempts;
};

// ========== OCPP MANAGER INTERFACE ==========

namespace OCPP
{

    /**
     * @brief Initialize OCPP client
     * @param central_system_url OCPP central system URL (ws://...)
     * @param charge_point_id Charge point identifier
     * @return true if initialization successful
     */
    bool init(const char *central_system_url, const char *charge_point_id);

    /**
     * @brief Connect to central system
     * @return true if connection initiated
     */
    bool connect();

    /**
     * @brief Disconnect from central system
     * @return true if disconnection initiated
     */
    bool disconnect();

    /**
     * @brief Get OCPP status
     * @return Current OCPP status
     */
    OcppStatus getStatus();

    /**
     * @brief Start charging transaction
     * @param connector_id Connector identifier
     * @param user_id Optional user identifier
     * @return Transaction ID, 0 if failed
     */
    uint32_t startTransaction(const char *connector_id, const char *user_id = nullptr);

    /**
     * @brief Stop charging transaction
     * @param transaction_id Transaction ID from startTransaction()
     * @return true if stop command sent
     */
    bool stopTransaction(uint32_t transaction_id);

    /**
     * @brief Send meter values to central system
     * @param transaction_id Transaction ID
     * @param energy_wh Energy in Watt-hours
     * @param power_w Current power in Watts
     * @return true if sent successfully
     */
    bool sendMeterValues(uint32_t transaction_id, uint32_t energy_wh, uint32_t power_w);

    /**
     * @brief Handle configuration changes from central system
     * @param key Configuration key
     * @param value Configuration value
     * @return true if accepted
     */
    bool handleConfigurationChange(const char *key, const char *value);

    /**
     * @brief Check if connected to central system
     * @return true if connected
     */
    bool isConnected();

    /**
     * @brief Get error message if any
     * @return Error string, empty if no error
     */
    const char *getLastError();

    /**
     * @brief Perform OCPP update
     * Call this regularly to process incoming messages
     */
    void update();

} // namespace OCPP
