/**
 * @file DalyBmsDriver.h
 * @brief Hardware-independent driver for Daly BMS over CAN
 * @layer Device Driver
 *
 * Implements IBms using injected ICan and ITimer interfaces.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "drivers/interfaces/IBms.h"
#include "hal/interfaces/ICan.h"
#include "hal/interfaces/ITimer.h"
#include "hal/interfaces/ILogger.h"

class DalyBmsDriver : public IBms {
public:
    /**
     * @param can    Injected CAN HAL instance (must be pre-initialized at 250kbps)
     * @param timer  Injected Timer HAL instance for message age tracking
     * @param logger Injected Logger HAL instance
     */
    DalyBmsDriver(ICan& can, ITimer& timer, ILogger& logger);
    virtual ~DalyBmsDriver() = default;

    bool init() override;
    
    float getPackVoltage() override;
    float getSoc() override;
    float getMaxChargeCurrent() override;
    bool  isSafeToCharge() override;
    bool  isConnected() override;
    uint32_t getLastMessageAgeMs() override;

    /**
     * Call this periodically to process incoming CAN frames from the queue.
     * Since we aren't using interrupts directly in the driver, we must poll.
     */
    void update() override;

    /**
     * Push external state to the driver so it can build heartbeats
     */
    void updateSystemStatus(float _terminalVolt, float _terminalCurr, uint8_t _statusFlags);

private:
    ICan& can;
    ITimer& timer;
    ILogger& logger;

    float packVoltage;
    float soc;
    float maxCurrent;
    uint8_t faultFlags;
    
    uint32_t lastMessageTimeMs;

    // External status data for heartbeat
    float sysTerminalVolt;
    float sysTerminalCurr;
    uint8_t sysStatusFlags;

    // TX helpers
    void sendHeartbeat(float terminalVolt, float terminalCurr, uint8_t statusFlags);
    void sendSocRequest();

    // CAN ID constants — Rivot Motors BMS protocol
    static const uint32_t BMS_REQUEST_ID  = 0x1806E5F4; // BMS sends charge request → MCU RX
    static const uint32_t HEARTBEAT_TX_ID = 0x18FF50E5; // MCU → BMS: charger feedback
    static const uint32_t SOC_REQUEST_ID  = 0x18900140; // MCU → BMS: SOC poll
    static const uint32_t SOC_RESPONSE_ID = 0x18904001; // BMS → MCU: SOC reply
    static const uint32_t TIMEOUT_MS      = 3000;

    uint32_t lastTxTimeMs;
    static const uint32_t TX_INTERVAL_MS = 1000; // Send heartbeat + SOC request every 1s
};
