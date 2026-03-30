/**
 * @file NtcSensor.h
 * @brief Hardware-independent driver for an NTC Thermistor
 * @layer Device Driver
 *
 * Implements ISensor using an injected IGpio interface.
 * Converts raw ADC counts to Celsius using the Steinhart-Hart equation or a beta model.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "drivers/interfaces/ISensor.h"
#include "hal/interfaces/IGpio.h"

class NtcSensor : public ISensor {
public:
    /**
     * @param gpio      Injected GPIO HAL instance
     * @param pin       Physical ADC pin number
     * @param rSeries   Series resistor value (Ohms)
     * @param nominalR  Thermistor nominal resistance at 25C (Ohms)
     * @param beta      Thermistor Beta coefficient
     * @param adcMax    Maximum ADC value (e.g. 4095 for 12-bit)
     */
    NtcSensor(IGpio& gpio, int pin, float rSeries = 10000.0f, float nominalR = 10000.0f, 
              float beta = 3950.0f, int adcMax = 4095);
    virtual ~NtcSensor() = default;

    bool init() override;
    float read() override;
    bool isValid() override;

private:
    IGpio& gpio;
    int pin;
    float rSeries;
    float nominalR;
    float beta;
    int adcMax;
    float lastValidTemp;
};
