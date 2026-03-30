/**
 * @file IUart.h
 * @brief HAL interface for UART serial peripheral
 * @layer HAL — MCU peripheral abstraction
 *
 * Implementations: Esp32Uart (wraps HardwareSerial / Serial2)
 * Used by: SIM800ModemDriver
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

class IUart {
public:
    virtual ~IUart() = default;

    /** Initialize UART at the given baud rate. */
    virtual void begin(uint32_t baud) = 0;

    /**
     * Write a buffer of bytes.
     * @return number of bytes actually written
     */
    virtual size_t write(const uint8_t* buf, size_t len) = 0;

    /** Write a null-terminated string. */
    virtual size_t writeStr(const char* str) = 0;

    /** @return number of bytes available to read */
    virtual int available() = 0;

    /** Read one byte (-1 if none available). */
    virtual int read() = 0;

    /** Block until all output has been transmitted. */
    virtual void flush() = 0;
};
