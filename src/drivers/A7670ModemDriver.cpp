#include "drivers/A7670ModemDriver.h"
#include <string.h>

A7670ModemDriver::A7670ModemDriver(IUart& uart, IGpio& gpio, ITimer& timer, int rstPin)
    : uart(uart), gpio(gpio), timer(timer), resetPin(rstPin), connected(false) {
    ipAddress[0] = '\0';
}

bool A7670ModemDriver::init() {
    // 1. Hardware Reset sequence
    gpio.setMode(resetPin, IGpio::GPIO_OUTPUT);
    gpio.write(resetPin, true); // Active High reset 
    timer.delayMs(2500);
    gpio.write(resetPin, false);
    
    // 2. Wait for boot
    timer.delayMs(3000); // Wait for modem to boot

    // 3. Software Initialization
    flushUart();

    // Disable echo
    if (!sendATCommand("ATE0\r\n", "OK", 2000)) return false;

    // Check SIM card
    if (!sendATCommand("AT+CPIN?\r\n", "+CPIN: READY", 2000)) return false;

    return true;
}

bool A7670ModemDriver::connect(const char* apn) {
    connected = false;

    // Check Network Registration
    if (!sendATCommand("AT+CREG?\r\n", "+CREG: 0,1", 5000)) {
        if (!sendATCommand("AT+CREG?\r\n", "+CREG: 0,5", 5000)) {
            return false; // Not registered
        }
    }

    // Set APN
    char cmd[64];
    // This is a generic PDP context setup (AT+CGDCONT=1,"IP","apn_name")
    // Simplified for A7670 / Generic modules
    strncpy(cmd, "AT+CGDCONT=1,\"IP\",\"", sizeof(cmd));
    strncat(cmd, apn, sizeof(cmd) - strlen(cmd) - 1);
    strncat(cmd, "\"\r\n", sizeof(cmd) - strlen(cmd) - 1);

    if (!sendATCommand(cmd, "OK", 2000)) return false;

    // Activate PDP Context
    if (!sendATCommand("AT+CGACT=1,1\r\n", "OK", 10000)) return false;

    // Obtain IP Address (stub logic)
    // In reality we'd parse AT+CGPADDR=1 to extract the IP
    strncpy(ipAddress, "10.0.0.2", sizeof(ipAddress));

    connected = true;
    return true;
}

bool A7670ModemDriver::isConnected() {
    // Ping modem to check if still attached
    if (connected && sendATCommand("AT+CGATT?\r\n", "+CGATT: 1", 2000)) {
        return true;
    }
    connected = false;
    return false;
}

int A7670ModemDriver::getSignalQuality() {
    flushUart();
    uart.writeStr("AT+CSQ\r\n");
    
    // Very simplified parser for +CSQ: 21,99
    uint32_t startAt = timer.millis();
    int csq = 99;
    char buffer[64];
    int bufPos = 0;

    while (timer.millis() - startAt < 2000) {
        if (uart.available() > 0) {
            char c = (char)uart.read();
            if (bufPos < (int)sizeof(buffer) - 1) {
                buffer[bufPos++] = c;
                buffer[bufPos] = '\0';
                
                // Check if we got +CSQ:
                char* p = strstr(buffer, "+CSQ: ");
                if (p) {
                    p += 6;
                    // Simple ascii to int
                    int val = 0;
                    while (*p >= '0' && *p <= '9') {
                        val = val * 10 + (*p - '0');
                        p++;
                    }
                    csq = val;
                }
                if (strstr(buffer, "OK\r\n")) break;
            }
        } else {
            timer.delayMs(10);
        }
    }
    return csq;
}

const char* A7670ModemDriver::getLocalIp() {
    return connected ? ipAddress : "";
}

void A7670ModemDriver::disconnect() {
    sendATCommand("AT+CGACT=0,1\r\n", "OK", 5000);
    connected = false;
}

// ------ Internal Helpers ------

void A7670ModemDriver::flushUart() {
    while (uart.available() > 0) {
        uart.read();
    }
}

bool A7670ModemDriver::sendATCommand(const char* cmd, const char* expected_response, uint32_t timeoutMs) {
    flushUart();
    uart.writeStr(cmd);
    return readUntil(expected_response, timeoutMs);
}

bool A7670ModemDriver::readUntil(const char* expected, uint32_t timeoutMs) {
    uint32_t startAt = timer.millis();
    char buffer[128];
    int bufPos = 0;
    
    while (timer.millis() - startAt < timeoutMs) {
        if (uart.available() > 0) {
            char c = (char)uart.read();
            if (bufPos < (int)sizeof(buffer) - 1) {
                buffer[bufPos++] = c;
                buffer[bufPos] = '\0';
                
                if (strstr(buffer, expected) != nullptr) {
                    return true;
                }
                if (strstr(buffer, "ERROR") != nullptr) {
                    return false;
                }
            }
        } else {
            timer.delayMs(10);
        }
    }
    return false; // Timeout
}
