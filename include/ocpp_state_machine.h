#ifndef OCPP_STATE_MACHINE_H
#define OCPP_STATE_MACHINE_H

#include <Arduino.h>
#include <MicroOcpp.h>

/**
 * @file ocpp_state_machine.h
 * @brief Production-grade OCPP state machine with deadlock prevention
 *
 * Handles:
 * - Transaction persistence across reboots
 * - "Finishing" state timeout (60 seconds) to prevent deadlock
 * - Plug detection for automatic state transitions
 * - Remote Start/Stop validation
 * - Hardware fault checking
 */

namespace prod
{

    enum class ConnectorState
    {
        Unknown = -1,
        Available = 0,
        Preparing = 1,
        Charging = 2,
        SuspendedEVSE = 3,
        SuspendedEV = 4,
        Finishing = 5,
        Reserved = 6,
        Unavailable = 7,
        Faulted = 8
    };

    class OCPPStateMachine
    {
    private:
        static const uint32_t FINISHING_TIMEOUT_MS = 10000; // 10 seconds
        static const uint32_t PLUG_DEBOUNCE_MS = 500;

        ConnectorState currentState = ConnectorState::Available;
        uint32_t stateEnterTime = 0;
        uint32_t lastPlugCheckTime = 0;
        bool plugConnected = false;
        bool lastPlugState = false;

    public:
        /**
         * Initialize state machine and set up handlers
         */
        void init();

        /**
         * Poll state machine (check timeouts, plug status, etc.)
         */
        void poll();

        /**
         * Handle transaction start from OCPP
         */
        void onTransactionStarted(int connectorId, const char *idTag, int transactionId);

        /**
         * Handle transaction stop from OCPP
         */
        void onTransactionStopped(int transactionId);

        /**
         * Remote Start handler - validate before accepting
         */
        bool onRemoteStartTransaction(const char *idTag, int connectorId);

        /**
         * Remote Stop handler - validate before accepting
         */
        bool onRemoteStopTransaction(int transactionId);

        /**
         * Check if plug is physically connected
         * Implement based on your hardware (GPIO, CAN signal, etc.)
         */
        bool isPlugConnected();

        /**
         * Check if hardware is in a safe state for charging
         */
        bool isHardwareSafe();

        /**
         * Get current state
         */
        ConnectorState getState() const { return currentState; }

        /**
         * Get state name as string
         */
        const char *getStateName() const;

        /**
         * Get time in current state
         */
        uint32_t getStateTimeMs() const;

        /**
         * Force state transition (for testing/recovery)
         */
        void forceState(ConnectorState newState);
    };

    extern OCPPStateMachine g_ocppStateMachine;

} // namespace prod

#endif // OCPP_STATE_MACHINE_H
