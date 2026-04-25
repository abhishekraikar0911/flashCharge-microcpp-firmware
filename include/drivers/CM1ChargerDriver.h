/**
 * @file CM1ChargerDriver.h
 * @brief Hardware-independent driver for the Rivot CM1 Charger Module over CAN
 * @layer Device Driver
 *
 * Implements IChargerModule using injected ICan and ITimer interfaces.
 * Replaces old legacycharger_interface.cpp / can_twai_driver.cpp coupling.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "drivers/interfaces/IChargerModule.h"
#include "hal/interfaces/ICan.h"
#include "hal/interfaces/ITimer.h"
#include "hal/interfaces/ILogger.h"

// CM1 Unsolicited IDs (received from charger periodically)
#define CM1_ID_TERM_POWER  0x00433F01UL
#define CM1_ID_TERM_STATUS 0x00473F01UL
#define CM1_ID_HEARTBEAT   0x18FF50E5UL

class CM1ChargerDriver : public IChargerModule {
public:
    /**
     * @param can    Injected CAN HAL instance (must be pre-initialized at 250kbps)
     * @param timer  Injected Timer HAL instance
     * @param logger Injected Logger HAL instance
     */
    CM1ChargerDriver(ICan& can, ITimer& timer, ILogger& logger);
    virtual ~CM1ChargerDriver() = default;

    bool init() override;
    
    bool startCharging(float targetVoltage, float maxCurrent) override;
    void updateLimits(float targetVoltage, float maxCurrent) override;
    bool stopCharging() override;
    
    bool getTelemetry(float& volts, float& amps, float& temp) override;
    
    bool isReady() override;
    bool hasFault() override;

    /**
     * Periodically process incoming CAN frames and dispatch heartbeat/keep-alive msgs
     */
    void update();

private:
    ICan& can;
    ITimer& timer;
    ILogger& logger;

    float currentVmax;
    float currentImax;
    bool  isOutputEnabled;

    // Telemetry state
    float outVolts;
    float outAmps;
    float internalTemp;
    bool  isTermCharging;
    uint32_t lastTelemetryTime;
    uint32_t lastControlSendTime;

    // CAN TX Helpers
    bool sendControlMessage(uint8_t func, uint32_t rawData = 0, uint8_t byte3 = 0);
    
    // Decoding helper for Big-endian floats (Terminal Power)
    float decodeBEFloat(const uint8_t* b);
};
