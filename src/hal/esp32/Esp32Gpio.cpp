#include "hal/esp32/Esp32Gpio.h"
#include <Arduino.h>

Esp32Gpio::Esp32Gpio() {
    // Standard initialization for ESP32 Arduino framework GPIO handled per-pin
}

void Esp32Gpio::setMode(int pin, Mode mode) {
    uint8_t hardwareMode = INPUT;
    switch (mode) {
        case GPIO_INPUT:
            hardwareMode = INPUT;
            break;
        case GPIO_OUTPUT:
            hardwareMode = OUTPUT;
            break;
        case GPIO_INPUT_PULLUP:
            hardwareMode = INPUT_PULLUP;
            break;
        case GPIO_INPUT_PULLDOWN:
            hardwareMode = INPUT_PULLDOWN;
            break;
    }
    ::pinMode(pin, hardwareMode);
}

void Esp32Gpio::write(int pin, bool high) {
    ::digitalWrite(pin, high ? HIGH : LOW);
}

bool Esp32Gpio::read(int pin) {
    return ::digitalRead(pin) == HIGH;
}

int Esp32Gpio::analogRead(int pin) {
    return ::analogRead(pin);
}
