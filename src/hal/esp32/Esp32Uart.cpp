#include "hal/esp32/Esp32Uart.h"

Esp32Uart::Esp32Uart(int uartNum, int txPin, int rxPin) 
    : serial(uartNum), txPin(txPin), rxPin(rxPin) {
}

void Esp32Uart::begin(uint32_t baud) {
    // SERIAL_8N1 is the default and standard for SIM modems
    serial.begin(baud, SERIAL_8N1, rxPin, txPin);
}

size_t Esp32Uart::write(const uint8_t* buf, size_t len) {
    return serial.write(buf, len);
}

size_t Esp32Uart::writeStr(const char* str) {
    return serial.print(str);
}

int Esp32Uart::available() {
    return serial.available();
}

int Esp32Uart::read() {
    return serial.read();
}

void Esp32Uart::flush() {
    serial.flush();
}

HardwareSerial& Esp32Uart::getNativeSerial() {
    return serial;
}
