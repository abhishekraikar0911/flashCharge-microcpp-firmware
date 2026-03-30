/**
 * @file Esp32Uart.h
 * @brief ESP32 implementation for IUart interface
 * @layer HAL
 *
 * Wraps Arduino HardwareSerial. Used primarily for GSM Modem.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "hal/interfaces/IUart.h"
#include <HardwareSerial.h>

class Esp32Uart : public IUart {
public:
    /**
     * @param uartNum  Hardware UART index (0, 1, or 2). ESP32 usually uses 2 for modems.
     * @param txPin    TX GPIO pin
     * @param rxPin    RX GPIO pin
     */
    Esp32Uart(int uartNum, int txPin, int rxPin);
    virtual ~Esp32Uart() = default;

    void   begin(uint32_t baud) override;
    size_t write(const uint8_t* buf, size_t len) override;
    size_t writeStr(const char* str) override;
    int    available() override;
    int    read() override;
    void   flush() override;

    /**
     * Get the underlying HardwareSerial instance.
     * Used ONLY as a bridge during migration for libraries like TinyGSM.
     */
    HardwareSerial& getNativeSerial();

private:
    HardwareSerial serial;
    int txPin;
    int rxPin;
};
