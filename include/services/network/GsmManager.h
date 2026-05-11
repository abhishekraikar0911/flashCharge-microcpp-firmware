#pragma once

/**
 * @file gsm_manager.h
 * @brief GSM A7670C modem manager using TinyGSM
 * 
 * Manages the lifecycle of the SIM A7670C LTE Cat-1 modem:
 *   MODEM_BOOT → MODEM_READY → SIM_READY → NETWORK_REGISTERED →
 *   DATA_ATTACHED → IP_READY → CONNECTED
 * 
 * Hardware:
 *   UART2 (GPIO16 RX, GPIO17 TX), RESET on GPIO13 (Active HIGH)
 * 
 * @author Rivot Motors
 * @date 2026
 */

#include <Arduino.h>
#include <TinyGsmClient.h>

namespace prod {

    /**
     * @brief GSM modem lifecycle states
     */
    enum class GSMState : uint8_t {
        MODEM_OFF = 0,      // Not initialized
        MODEM_BOOT,         // RESET pulse sent, waiting for AT response
        MODEM_READY,        // AT responding
        SIM_READY,          // SIM card detected and unlocked
        NETWORK_REGISTERED, // Registered on LTE network
        DATA_ATTACHED,      // GPRS/LTE data attached
        IP_READY,           // IP address assigned
        CONNECTED,          // TCP layer ready for WebSocket
        ERROR               // Unrecoverable error (needs reset)
    };

    /**
     * @brief Detailed error codes for connection attempts
     */
    enum class GsmError : uint8_t {
        SUCCESS = 0,
        FAIL_RETRYABLE,     // Signal low, timeout, etc.
        FAIL_FATAL_NO_SIM,  // SIM pulled/missing
        FAIL_FATAL_MODEM    // Modem hardware not responding
    };

    /**
     * @brief Returns a human-readable string for the GSM state
     */
    const char* gsmStateToString(GSMState state);

    /**
     * @brief GSM A7670C Modem Manager
     */
    class GSMManager {
    public:
        /**
         * @brief Initialize UART and reset pin. Does NOT start connection.
         */
        void init();

        /**
         * @brief Attempt to bring the modem up through all states to CONNECTED.
         * @param networkTimeoutMs Timeout for network registration (Step 4/6)
         * @return GsmError indicating success or the specific cause of failure
         */
        GsmError connect(uint32_t networkTimeoutMs = 60000);

        /**
         * @brief Disconnect and power down the modem gracefully.
         */
        void disconnect();

        /**
         * @brief Poll for modem health. Call periodically from network task.
         *        Checks AT responsiveness and signal quality.
         */
        void poll();

        /**
         * @brief Hardware reset the modem via GPIO13 (Active HIGH, 2.5s pulse).
         */
        void hardReset();

        /**
         * @brief Soft reset via AT command.
         * @return true if modem responded after reset
         */
        bool softReset();

        // ── State Queries ──

        GSMState getState() const { return _state; }
        bool isConnected() const { return _state == GSMState::CONNECTED; }
        bool isModemReady() const { return _state >= GSMState::MODEM_READY; }

        // ── Diagnostics ──

        int getSignalQuality();        // CSQ value (0-31, 99=unknown)
        const char* getOperator();     // Network operator name
        const char* getLocalIP();      // Assigned IP address
        float getSupplyVoltage();      // AT+CBC voltage reading

        /**
         * @brief Get the TinyGSM client for TCP connections.
         *        Only valid when state == CONNECTED.
         */
        TinyGsmClient& getClient() { return _gsmClient; }

        /**
         * @brief Get the underlying TinyGSM modem object.
         */
        TinyGsm& getModem() { return _modem; }

    private:
        // ── State Machine Transitions ──
        bool stepBoot();
        bool stepModemReady();
        bool stepSimReady();
        bool stepNetworkRegistered(uint32_t timeoutMs = 60000);
        bool stepDataAttached();
        bool stepIpReady();

        // ── Internal Helpers ──
        bool waitForAT(uint32_t timeoutMs = 10000);
        void setState(GSMState newState);

        // ── TinyGSM Objects ──
        // NOTE: These are constructed in-place using placement new in init()
        // because they need the Serial reference which isn't available at static init time
        static uint8_t _modemBuf[];
        static uint8_t _clientBuf[];
        TinyGsm& _modem = reinterpret_cast<TinyGsm&>(_modemBuf);
        TinyGsmClient& _gsmClient = reinterpret_cast<TinyGsmClient&>(_clientBuf);

        // ── State ──
        GSMState _state = GSMState::MODEM_OFF;
        bool _initialized = false;
        uint32_t _lastATCheck = 0;
        uint32_t _lastSignalCheck = 0;
        int _lastCSQ = 99;
        int _missedHeartbeats = 0;
        char _operatorName[32] = {0};
        char _localIP[20] = {0};
    };

    extern GSMManager g_gsmManager;

} // namespace prod
