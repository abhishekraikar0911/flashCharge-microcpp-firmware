/**
 * @file ISensor.h
 * @brief Device driver interface for generic analog/digital sensors
 * @layer Device Driver
 *
 * Implementations: NtcSensor
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once

class ISensor {
public:
    virtual ~ISensor() = default;

    /** Initialize the sensor (e.g. configure ADC pins) */
    virtual bool init() = 0;

    /**
     * Read the processed sensor value.
     * @return Value in standard engineering units (e.g. Celsius for temp sensors)
     */
    virtual float read() = 0;

    /** @return true if the sensor is functioning and readings are within physical limits */
    virtual bool isValid() = 0;
};
