# Hardware Setup Guide

## ESP32 Pinout

```
ESP32 DevKit
┌─────────────────┐
│                 │
│  GPIO21 ────────┼──> CAN TX
│  GPIO22 ────────┼──> CAN RX
│  GND    ────────┼──> CAN GND
│  3V3    ────────┼──> (Optional: CAN transceiver power)
│                 │
│  USB    ────────┼──> Programming & Power
│                 │
└─────────────────┘
```

## CAN Bus Wiring

### Option 1: Using MCP2515 CAN Module

```
ESP32          MCP2515 Module
GPIO21  ──────> TX
GPIO22  ──────> RX
3V3     ──────> VCC
GND     ──────> GND

MCP2515        CAN Bus
CANH    ──────> CANH (Yellow/White)
CANL    ──────> CANL (Green/Blue)
GND     ──────> GND  (Black)
```

### Option 2: Using SN65HVD230 Transceiver

```
ESP32          SN65HVD230
GPIO21  ──────> TX
GPIO22  ──────> RX
3V3     ──────> VCC
GND     ──────> GND

SN65HVD230     CAN Bus
CANH    ──────> CANH
CANL    ──────> CANL
```

## CAN Bus Termination

**Important**: Add 120Ω resistor between CANH and CANL at both ends of the bus.

```
Device 1 ──[120Ω]── CANH ════════ CANH ──[120Ω]── Device 2
                    CANL ════════ CANL
```

## Power Supply

- **USB Power**: 5V/1A minimum (for development)
- **External Power**: 5-12V DC (for production)
- **Current Draw**: ~200mA typical, 500mA peak (WiFi transmit)

## LED Indicators (Optional)

Add LEDs for visual status:

```cpp
#define LED_WIFI_PIN GPIO_NUM_2    // Blue LED - WiFi status
#define LED_OCPP_PIN GPIO_NUM_4    // Green LED - OCPP connected
#define LED_CHARGE_PIN GPIO_NUM_5  // Red LED - Charging active
```

## Testing Hardware

1. **Power On**: Blue LED should blink (WiFi connecting)
2. **WiFi Connected**: Blue LED solid
3. **OCPP Connected**: Green LED solid
4. **Charging**: Red LED solid

## Safety Notes

⚠️ **WARNING**: 
- CAN bus operates at 5V logic - use level shifters if needed
- Do not connect/disconnect CAN while powered
- Ensure proper grounding to prevent ground loops
- Use shielded twisted-pair cable for CAN bus
- Maximum CAN bus length: 40m @ 250kbps

## Troubleshooting

### No CAN Communication
- Check wiring (TX/RX not swapped)
- Verify 120Ω termination resistors
- Measure voltage: CANH ~3.5V, CANL ~1.5V (idle)

### WiFi Not Connecting
- Check antenna is attached (if external)
- Verify 2.4GHz network (ESP32 doesn't support 5GHz)
- Move closer to router

### ESP32 Keeps Rebooting
- Insufficient power supply
- Check USB cable quality
- Try external 5V power supply
