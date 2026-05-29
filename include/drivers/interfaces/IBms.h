/**
 * @file IBms.h
 * @brief Device driver interface for Battery Management Systems (e.g., Daly)
 * @layer Device Driver
 *
 * Implementations: DalyBmsDriver
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include <stdint.h>

class IBms {
public:
    virtual ~IBms() = default;

    /** Initialize the BMS driver and underlying communications (e.g. CAN) */
    virtual bool init() = 0;

    /** @return Pack voltage reported by BMS (Volts) */
    virtual float getPackVoltage() = 0;

    /** @return State of Charge reported by BMS (0.0 to 100.0 %) */
    virtual float getSoc() = 0;

    /** @return Maximum charge current dynamically requested by BMS (Amps) */
    virtual float getMaxChargeCurrent() = 0;

    /** @return true if BMS allows charging (no internal alarms/faults) */
    virtual bool isSafeToCharge() = 0;

    /** @return true if BMS is actively communicating */
    virtual bool isConnected() = 0;

    /** @return Milliseconds since the last valid telemetry frame was received */
    virtual uint32_t getLastMessageAgeMs() = 0;

    /** @return true if the underlying hardware controller (e.g. CAN chip) is healthy */
    virtual bool isHardwareHealthy() = 0;

    /**
     * Periodic driver tick — must be called from the application loop.
     * Handles: CAN RX queue draining, telemetry decoding.
     */
    virtual void update() = 0;

    /**
     * Push external state to the driver so it can build heartbeats
     */
    virtual void updateSystemStatus(float terminalVolt, float terminalCurr, uint8_t statusFlags) {}
};
