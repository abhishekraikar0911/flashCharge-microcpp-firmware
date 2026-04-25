/**
 * @file IChargerModule.h
 * @brief Device driver interface for power modules (e.g., CM1, CM2)
 * @layer Device Driver
 *
 * Implementations: CM1ChargerDriver
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once

class IChargerModule {
public:
    virtual ~IChargerModule() = default;

    /** Initialize the charger module driver and underlying communications (e.g. CAN) */
    virtual bool init() = 0;

    /**
     * Request the charger module to output power.
     * @param targetVoltage  Desired output voltage in Volts (e.g. 84.0)
     * @param maxCurrent     Maximum output current in Amps (e.g. 50.0)
     * @return true if command sent successfully
     */
    virtual bool startCharging(float targetVoltage, float maxCurrent) = 0;

    /**
     * Dynamically update the current/voltage limits while a transaction is running.
     * @param targetVoltage  Desired output voltage in Volts
     * @param maxCurrent     Maximum output current in Amps
     */
    virtual void updateLimits(float targetVoltage, float maxCurrent) = 0;

    /** Request the charger module to stop outputting power */
    virtual bool stopCharging() = 0;

    /**
     * Get the latest telemetry from the charger module.
     * @param volts [out] Present output voltage (V)
     * @param amps  [out] Present output current (A)
     * @param temp  [out] Highest internal temperature (C)
     * @return true if data is fresh, false if timed out
     */
    virtual bool getTelemetry(float& volts, float& amps, float& temp) = 0;

    /** @return true if the module is internally ready to charge (no alarms) */
    virtual bool isReady() = 0;

    /** @return true if the module is reporting an unrecoverable hardware fault */
    virtual bool hasFault() = 0;

    /**
     * Periodic driver tick — must be called from the application loop.
     * Handles: CAN keep-alive transmits, RX queue draining, telemetry decoding.
     */
    virtual void update() = 0;
};
