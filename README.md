# MicroOCPP EV Charger Controller

An ESP32-based OCPP 1.6 compliant electric vehicle charger controller implementing communication with BMS (Battery Management System), charger hardware, and central management system.

## Features

- **OCPP 1.6 Compliance**: Full Open Charge Point Protocol implementation for central system communication
- **CAN Bus Communication**: Robust communication with BMS and charger hardware via TWAI (CAN)
- **Real-time Monitoring**: Voltage, current, temperature, and energy metering
- **Remote Control**: Support for remote start/stop transactions
- **Safety Features**: Hardware validation and error handling
- **Telemetry**: Real-time data reporting to central management system

## Hardware Requirements

- ESP32 microcontroller
- CAN bus interface (TWAI)
- EV charger hardware with BMS integration
- WiFi connectivity

## Software Architecture

```
src/
├── main.cpp              # Application entry point and task initialization
├── header.h              # Global declarations and shared state
├── bms_mcu.cpp           # BMS communication and SOC handling
├── mcu_cm.cpp            # Charger communication and CAN message processing
├── print_darta.cpp       # UI and data visualization
├── twai_init.cpp         # CAN bus initialization and RX handling
└── ocpp/
    ├── ocpp_client.cpp   # OCPP protocol implementation
    └── ocpp_client.h     # OCPP client interface

include/
├── header.h              # Global declarations
└── secrets.h             # Configuration secrets (WiFi, server URLs)

lib/
└── MicroOcpp/           # MicroOcpp library
```

## Configuration

### Network Settings
Edit `include/secrets.h` to configure:
- WiFi credentials
- OCPP central system URL
- Charger identification

### Build Configuration
Modify `platformio.ini` for:
- Board settings
- Library dependencies
- Build flags

## Building and Flashing

1. Install PlatformIO
2. Clone this repository
3. Configure secrets in `include/secrets.h`
4. Run `pio run` to build
5. Run `pio run --target upload` to flash

## Usage

1. Power on the ESP32
2. Connect to configured WiFi network
3. System initializes CAN bus and OCPP connection
4. Use serial interface for local control and monitoring
5. Charger responds to OCPP commands from central system

## Serial Commands

- `1-5`: Display different data views
- `s`: Start charging (when conditions met)
- `t`: Stop charging
- `0`: Mute output

## Dependencies

- Arduino Framework
- MicroOcpp library
- ArduinoJson
- WebSockets

## License

[Add appropriate license]

## Contributing

[Add contribution guidelines]