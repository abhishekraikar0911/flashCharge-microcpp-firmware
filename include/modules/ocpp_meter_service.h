#pragma once

#include <Arduino.h>

namespace prod {

/**
 * @file ocpp_meter_service.h
 * @brief Manages OCPP MeterValues, measurands, and high-frequency compact telemetry.
 * 
 * Strategy:
 *   - Standard OCPP MeterValues at 60s (compliance)
 *   - Compact DataTransfer "LiveTelemetry" at 5-10s (real-time monitoring)
 *   - Queue guard to prevent overflow
 */
class OcppMeterService {
public:
    void begin();
    void poll(); // Handles dynamic scaling and period checks
    
    // Meter Registration
    void registerMeters();
    
    // Scaled Metering Control
    void resetScaling(unsigned long txStartTime);
    void triggerManualMeterValue();

    // Compact telemetry interval (seconds)
    void setCompactInterval(int seconds) { _compactInterval = seconds; }
    int  getCompactInterval() const { return _compactInterval; }

private:
    unsigned long _lastMeterValueSent = 0;
    bool _firstMeterValueSent = false;
    unsigned long _lastScaledElapsed = 0;
    int _currentMvInterval = 5;
    int _meterStep = 0; // 0: Start, 1: 5s sent, 2: 15s sent (steady state)
    unsigned long _txStartTime = 0;
    
    // ── Compact Telemetry ──
    int _compactInterval = 10;             // Default 10s for safety; can be set to 5
    unsigned long _lastCompactSent = 0;
    bool _compactPending = false;          // Rate-limit: wait for ACK before next send
    void sendCompactTelemetry();           // Sends ~30-byte DataTransfer
    
    // Measurand providers
    static float getEnergy();
    static int getPower();
    static float getSoC();
    static float getVoltage();
    static float getCurrent();
    static float getMaxCurrent();
    static float getTemperature();
};

extern OcppMeterService g_meterService;

} // namespace prod
