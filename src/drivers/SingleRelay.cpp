#include "drivers/SingleRelay.h"

SingleRelay::SingleRelay(IGpio& gpio, int pin, bool activeHigh) 
    : gpio(gpio), pin(pin), activeHigh(activeHigh), closedState(false) {
}

void SingleRelay::init() {
    gpio.setMode(pin, IGpio::GPIO_OUTPUT);
    open(); // Ensure safe initial state
}

void SingleRelay::close() {
    gpio.write(pin, activeHigh);
    closedState = true;
}

void SingleRelay::open() {
    gpio.write(pin, !activeHigh);
    closedState = false;
}

bool SingleRelay::isClosed() {
    return closedState;
}
