/**
 * @file Esp32MCP2515.h
 * @brief ESP32 implementation for ICan wrapper using external MCP2515 over SPI
 * @layer HAL
 *
 * Used for the external SPI CAN peripheral (typically connected to the BMS)
 * Wraps the autowp/mcp2515 library.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "hal/interfaces/ICan.h"
#include <mcp2515.h>

class Esp32MCP2515 : public ICan {
public:
    /**
     * @param csPin   SPI Chip Select pin
     * @param freq    Oscillator frequency (e.g. MCP_8MHZ)
     */
    Esp32MCP2515(int csPin, CAN_CLOCK freq = MCP_8MHZ);
    virtual ~Esp32MCP2515() = default;

    bool init(uint32_t baudrate) override;
    bool send(const CanFrame& frame) override;
    bool receive(CanFrame& frame) override;
    bool isHealthy() override;
    void reset() override;

private:
    MCP2515* mcp;
    CAN_CLOCK clockFreq;
    bool isInit;
    uint32_t currentBaud;
    int csPin;

    CAN_SPEED getMcpSpeed(uint32_t baudrate);
};
