#include "drivers/NtcSensor.h"
#include <math.h>

#define KELVIN_OFFSET 273.15f
#define NOMINAL_TEMP_C 25.0f

NtcSensor::NtcSensor(IGpio& gpio, int pin, float rSeries, float nominalR, float beta, int adcMax)
    : gpio(gpio), pin(pin), rSeries(rSeries), nominalR(nominalR), beta(beta), adcMax(adcMax), lastValidTemp(25.0f) {}

bool NtcSensor::init() {
    gpio.setMode(pin, IGpio::GPIO_INPUT);
    return true;
}

float NtcSensor::read() {
    int rawAdc = gpio.analogRead(pin);
    
    // Prevent division by zero or infinite resistance
    if (rawAdc <= 0 || rawAdc >= adcMax) {
        return lastValidTemp; // Fallback to last known good value or a safe default
    }

    // Convert ADC to Resistance based on a typical voltage divider:
    // V_out = V_cc * (R_ntc / (R_ntc + R_series))   -- if NTC is tied to ground
    // Or V_out = V_cc * (R_series / (R_ntc + R_series)) -- if NTC is tied to Vcc
    // Assuming NTC is tied to ground:
    float resistance = rSeries / ((float)adcMax / rawAdc - 1.0f);

    // Steinhart-Hart equation (Beta parameter equation)
    float steinhart;
    steinhart = resistance / nominalR;                  // (R/Ro)
    steinhart = log(steinhart);                         // ln(R/Ro)
    steinhart /= beta;                                  // 1/B * ln(R/Ro)
    steinhart += 1.0f / (NOMINAL_TEMP_C + KELVIN_OFFSET); // + (1/To)
    steinhart = 1.0f / steinhart;                       // Invert
    steinhart -= KELVIN_OFFSET;                         // Convert Kelvin to Celsius

    lastValidTemp = steinhart;
    return steinhart;
}

bool NtcSensor::isValid() {
    int rawAdc = gpio.analogRead(pin);
    // basic sanity check: not shorted to ground and not open circuit
    if (rawAdc <= 10 || rawAdc >= (adcMax - 10)) {
        return false;
    }
    float temp = read();
    // Valid operating range for typical EV chargers
    if (temp < -40.0f || temp > 120.0f) {
        return false;
    }
    return true;
}
