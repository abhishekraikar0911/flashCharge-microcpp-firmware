/**
 * @file A7670ModemDriver.h
 * @brief Hardware-independent driver for a generic AT-based cellular modem
 * @layer Device Driver
 *
 * Implements IModem using injected IUart, IGpio, and ITimer interfaces.
 * Issues raw AT commands to the modem instead of relying on Arduino-specific libraries.
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once
#include "drivers/interfaces/IModem.h"
#include "hal/interfaces/IUart.h"
#include "hal/interfaces/IGpio.h"
#include "hal/interfaces/ITimer.h"

class A7670ModemDriver : public IModem {
public:
    /**
     * @param uart   Injected UART HAL instance
     * @param gpio   Injected GPIO HAL instance
     * @param timer  Injected Timer HAL instance
     * @param rstPin physical reset pin
     */
    A7670ModemDriver(IUart& uart, IGpio& gpio, ITimer& timer, int rstPin);
    virtual ~A7670ModemDriver() = default;

    bool init() override;
    bool connect(const char* apn) override;
    bool isConnected() override;
    int  getSignalQuality() override;
    const char* getLocalIp() override;
    void disconnect() override;

private:
    IUart&  uart;
    IGpio&  gpio;
    ITimer& timer;
    int     resetPin;
    
    char ipAddress[16];
    bool connected;

    // Internal AT helpers
    void flushUart();
    bool sendATCommand(const char* cmd, const char* expected_response, uint32_t timeoutMs);
    bool readUntil(const char* expected, uint32_t timeoutMs);
};
