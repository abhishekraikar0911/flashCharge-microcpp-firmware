# OCPP Charging Station - Production Code Structure

## Overview

This is a **production-grade firmware** for an OCPP (Open Charge Point Protocol) compatible EV charging station based on ESP32. The codebase follows enterprise-level software engineering practices with clear separation of concerns, modular architecture, and comprehensive documentation.

**Hardware**: ESP32 Dev Module @ 240MHz with 4MB Flash, TWAI CAN Bus @ 250kbps

## Quick Start

### Build
```bash
# Production build (optimized)
platformio run -e charger_esp32_production

# Debug build (with logging)
platformio run -e charger_esp32_debug

# Upload to device
platformio run -e charger_esp32_production --target upload

# Monitor serial output
platformio device monitor --baud 115200
```

### Clean Build
```bash
platformio run -e charger_esp32_production --target clean
```

## Project Structure

```
microocpp/
├── include/              # Header files (public interfaces)
│   ├── config/          # Configuration headers
│   ├── core/            # Core system services
│   ├── drivers/         # Hardware driver interfaces
│   └── modules/         # Application module interfaces
├── src/                 # Implementation files
│   ├── config/          # Configuration implementations
│   ├── core/            # Core system implementations
│   ├── drivers/         # Hardware driver implementations
│   ├── modules/         # Application module implementations
│   └── main.cpp         # Application entry point
├── lib/                 # Third-party libraries
│   └── MicroOcpp/       # OCPP 1.6 protocol library
├── docs/                # Project documentation
├── platformio.ini       # Build configuration
└── README.md            # This file
```

### Configuration Layer (`include/config/`)

**Purpose**: Hardware and system settings

- `hardware.h`: Pin definitions, CAN baudrate, safety limits, task priorities
- `timing.h`: Communication timeouts, intervals, feature flags
- `version.h`: Firmware version and build metadata

### Core System (`include/core/`)

**Purpose**: Essential system services

- `logger.h`: Structured logging with log levels and module tags
- `event_system.h`: Event-driven inter-module communication
- `system_health.h`: Health monitoring and diagnostics

### Drivers (`include/drivers/`)

**Purpose**: Hardware abstraction layer

- `can_driver.h`: CAN/TWAI bus communication (250kbps)
  - Ring buffer for received messages
  - Status tracking and error handling
  
- `bms_interface.h`: Battery Management System integration
  - Battery voltage, temperature, SOC monitoring
  - Safety state verification
  
- `charger_interface.h`: Power supply integration
  - Output control and current limiting
  - Terminal status and error codes

### Modules (`include/modules/`)

**Purpose**: Application-level functionality

- `ocpp_manager.h`: OCPP 1.6 protocol implementation
  - Transaction management
  - Meter value reporting
  - Central system communication
  
- `ui_console.h`: Serial console interface
  - Status display
  - User commands
  - Diagnostic output
  
- `ota_manager.h`: Firmware update management
  - Update checking and downloading
  - Installation and verification

## Key Features

### 1. **Modular Architecture**
Each component has a clear interface defined in headers. Implementations are independent and testable.

```cpp
namespace CAN { ... }
namespace BMS { ... }
namespace Charger { ... }
namespace OCPP { ... }
namespace UI { ... }
namespace Logger { ... }
```

### 2. **Safe Global State Management**
Global variables for state are defined once and declared extern elsewhere:
- Centralized in dedicated source files
- Prevents multiple definition errors
- Easy to identify system state

### 3. **Event-Driven Communication**
Components communicate through a central event system instead of direct coupling:
```cpp
EventSystem::post({EVENT_BMS_VOLTAGE_WARNING, ...});
```

### 4. **Production-Grade Logging**
Structured logging with tags and levels:
```cpp
LOG_INFO(LOG_TAG_BMS, "SOC updated: %.1f%%", soc);
LOG_ERROR(LOG_TAG_CAN, "Bus error: 0x%02x", error_code);
```

### 5. **Error Handling**
Consistent error handling patterns:
- Functions return `bool` for success/failure
- Status structures for detailed state
- Error codes for diagnostics

### 6. **Configuration Management**
Hardware and timing parameters are centralized and easily configurable without code changes.

## Build Environments

### Production (`charger_esp32_production`)
- **Optimization**: `-O3` (maximum speed)
- **Logging**: Warning level only
- **Debugging**: Symbols stripped
- **Use case**: Deployment to production hardware

### Debug (`charger_esp32_debug`)
- **Optimization**: `-O0` (no optimization)
- **Logging**: Debug level (verbose)
- **Debugging**: Full debug symbols included
- **Use case**: Development and troubleshooting

Select environment:
```bash
platformio run -e charger_esp32_production    # Production
platformio run -e charger_esp32_debug         # Debug
```

## Build Configuration Details

See `platformio.ini` for:
- Platform: **Espressif32 6.12.0**
- Board: **ESP32 Dev Module**
- Framework: **Arduino**
- Libraries: MicroOcpp 1.2.0, ArduinoJson 6.21.3, WebSockets 2.7.3
- Compiler flags: C++17, Wall, Wextra, optimizations

## System Initialization Flow

```
main()
├─ Serial.begin(115200)          # UI console
├─ CAN::init()                   # CAN driver
├─ BMS::init()                   # Battery monitoring
├─ Charger::init()               # Power supply
├─ Logger::init()                # Logging system
├─ EventSystem::init()           # Event distribution
├─ OCPP::init()                  # OCPP client
├─ SystemHealth::init()          # Health monitoring
├─ UI::init()                    # User interface
├─ xTaskCreate() × 5             # FreeRTOS tasks
└─ Start scheduler
```

## Task Architecture

The system runs 5 concurrent FreeRTOS tasks:

| Task | Priority | Stack | Interval | Purpose |
|------|----------|-------|----------|---------|
| CAN RX | 5 | 4KB | 1000ms | Receive BMS/Charger messages |
| Charger Comm | 4 | 4KB | 500ms | Charger control commands |
| OCPP Client | 3 | 8KB | Event-driven | Central system communication |
| Watchdog | 6 | 2KB | 5000ms | System health monitoring |
| UI Console | 2 | 4KB | Async | Serial user interface |

## Communication Protocols

### CAN Bus (250kbps)
- **BMS → Charger**: Battery state, SOC, voltage, temperature
- **Charger → Charger Logic**: Status, current limits, faults
- **Ring buffer**: 64-message queue for burst handling

### OCPP (WebSocket)
- **Central System URL**: Configurable at runtime
- **Charge Point ID**: Identifier in configuration
- **Meter values**: Reported every 10 seconds
- **Transactions**: Start/stop for charge sessions

### Serial Console (115200 baud)
- **Status display**: Real-time system monitoring
- **Commands**: Start/stop charging, diagnostics, configuration
- **Logging output**: All system events and diagnostics

## Example: Adding New Functionality

### 1. Define Interface
Create header in appropriate module:
```cpp
// include/modules/my_feature.h
namespace MyFeature {
    bool init();
    bool doSomething();
}
```

### 2. Implement
Create source file:
```cpp
// src/modules/my_feature.cpp
namespace MyFeature {
    bool init() { ... }
    bool doSomething() { ... }
}
```

### 3. Integrate
Call from `main.cpp` or via event system:
```cpp
MyFeature::init();
EventSystem::subscribe(EVENT_TYPE, [](const SystemEvent& e) {
    MyFeature::doSomething();
});
```

## Performance Metrics

- **Build time**: ~28 seconds
- **Firmware size**: ~1.2 MB
- **Flash usage**: ~30% of 4MB
- **RAM at startup**: ~150KB free
- **CAN throughput**: Up to 500 msgs/sec
- **Typical power**: 150mA @ 3.3V

## Security Considerations

1. **WPA2 WiFi**: For OCPP central system communication
2. **TLS/WSS**: WebSocket over HTTPS to central system
3. **Authentication**: Charge point ID and OCPP token validation
4. **OTA Updates**: Signature verification before installation
5. **Watchdog**: Hardware watchdog with crash recovery

## Debugging

### Enable Verbose Logging
1. Switch to debug environment: `-e charger_esp32_debug`
2. Serial monitor shows all LOG_DEBUG messages
3. Stack traces on exceptions

### Use Serial Console
```
Status          - Display all subsystem states
Diagnostics     - Run health checks
Logs            - View recent log entries
Reset           - Soft reset system
```

### Monitor Performance
```cpp
LOG_INFO(LOG_TAG_SYS, "Free heap: %d KB", esp_get_free_heap_size() / 1024);
LOG_INFO(LOG_TAG_SYS, "Uptime: %u seconds", esp_timer_get_time() / 1000000);
```

## Testing

### Unit Testing
```bash
platformio test -e test
```

### Integration Testing
Deploy to hardware and run through:
1. Boot sequence validation
2. CAN message reception
3. BMS communication
4. Charger control
5. OCPP connection
6. Transaction flow
7. OTA update process

## Production Deployment

### Pre-Deployment Checklist
- [ ] All warning/error logs cleared
- [ ] Performance benchmarks verified
- [ ] OTA server configured
- [ ] Watchdog timeout validated
- [ ] Safety limits verified
- [ ] Documentation updated

### Release Process
1. Update version in `include/config/version.h`
2. Build production firmware: `platformio run -e charger_esp32_production`
3. Generate firmware: `.pio/build/charger_esp32_production/firmware.bin`
4. Create release notes
5. Archive firmware and documentation

## Troubleshooting

### Build Fails
```bash
# Clean build
platformio run -e charger_esp32_production --target clean
platformio run -e charger_esp32_production
```

### Serial Monitor Issues
- Check COM port: `platformio device list`
- Reset board: Hold RESET button 1 second
- Baud rate: 115200 (fixed)

### CAN Bus Errors
- Verify termination resistors (120Ω each end)
- Check TX/RX connections to pin 21/22
- Verify voltage: 3.3V for signal, 5V for power

### OCPP Connection Fails
- Check WiFi SSID/password in configuration
- Verify central system URL format (ws:// or wss://)
- Check firewall rules for WebSocket port (80 or 443)

## Resources

- **OCPP 1.6 Spec**: [Open Charge Alliance](https://www.openchargealliance.org/)
- **ESP32 Reference**: [Espressif Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/)
- **Arduino Core**: [Arduino Reference](https://www.arduino.cc/reference/en/)
- **MicroOcpp**: [GitHub Repository](https://github.com/matth-x/MicroOcpp)

## Support

For issues or questions:
1. Check existing documentation in `docs/`
2. Review logs for error codes
3. Contact development team with firmware version and error logs

## License

© 2026 Rivot Motors. All rights reserved.

---

**Last Updated**: 2026
**Firmware Version**: 1.0.0
**Status**: Production Ready
