/**
 * @file Esp32Can.h
 * @brief ESP32 implementation for ICan wrapper using internal TWAI controller
 * @layer HAL
 *
 * Used for the internal CAN peripheral (typically connected to the charger CM)
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "hal/interfaces/ICan.h"
#include <driver/twai.h>
#include <freertos/semphr.h>

class Esp32Can : public ICan {
public:
    /**
     * @param txPin     GPIO pin for CAN TX
     * @param rxPin     GPIO pin for CAN RX
     * @param rxQueue   Size of the FreeRTOS receive queue
     * @param txQueue   Size of the FreeRTOS transmit queue
     */
    Esp32Can(int txPin, int rxPin, int rxQueue = 64, int txQueue = 16);
    virtual ~Esp32Can() = default;

    bool init(uint32_t baudrate) override;
    bool send(const CanFrame& frame) override;
    bool receive(CanFrame& frame) override;
    bool isHealthy() override;
    void reset() override;

private:
    int txPin;
    int rxPin;
    int rxQueueSize;
    int txQueueSize;
    bool isInit;
    uint32_t currentBaud;
    SemaphoreHandle_t mutex;

    twai_timing_config_t getTimingConfig(uint32_t baudrate);
};
