# ESP32 OCPP Charger - Project Structure

## 📁 Directory Tree

```
microocpp/
│
├── 📁 src/                          # Source Code
│   ├── main.cpp                     # ✅ Application entry point
│   │
│   ├── 📁 drivers/                  # Hardware Drivers
│   │   ├── can_driver.cpp           # ✅ CAN/TWAI driver (250kbps)
│   │   ├── bms_interface.cpp        # ✅ BMS communication (SOC, voltage, current)
│   │   └── charger_interface.cpp    # ✅ Charger control (setpoints, status)
│   │
│   ├── 📁 modules/                  # Feature Modules
│   │   ├── ocpp_manager.cpp         # ✅ OCPP protocol handler (terminal values)
│   │   ├── wifi_manager.cpp         # ✅ WiFi with auto-reconnect
│   │   ├── health_monitor.cpp       # ✅ Watchdog & health checks
│   │   ├── ocpp_state_machine.cpp   # ✅ OCPP state management
│   │   ├── security_manager.cpp     # ✅ TLS/WSS security
│   │   ├── production_config.cpp    # ✅ NVS persistence
│   │   └── ui_console.cpp           # ✅ Serial console UI
│   │
│   └── 📁 core/                     # Core Utilities
│       ├── globals.cpp              # ✅ Global variable definitions
│       ├── system.cpp               # ✅ System services
│       └── 📁 config/
│           └── config.cpp           # ✅ Configuration management
│
├── 📁 include/                      # Header Files
│   ├── header.h                     # ✅ Global declarations
│   ├── secrets.h                    # ⚠️  Credentials (gitignored)
│   ├── secrets.h.example            # ✅ Template for secrets
│   │
│   ├── 📁 config/                   # Configuration Headers
│   │   ├── hardware.h               # ✅ Pin definitions, limits
│   │   ├── timing.h                 # ✅ Timeouts, intervals
│   │   └── version.h                # ✅ Firmware version (v2.4.0)
│   │
│   ├── 📁 drivers/                  # Driver Headers
│   │   ├── can_driver.h             # ✅ CAN driver interface
│   │   ├── bms_interface.h          # ✅ BMS interface
│   │   └── charger_interface.h      # ✅ Charger interface
│   │
│   ├── 📁 modules/                  # Module Headers
│   │   └── ui_console.h             # ✅ UI console interface
│   │
│   ├── 📁 ocpp/                     # OCPP Interface
│   │   └── ocpp_client.h            # ✅ OCPP client API
│   │
│   ├── wifi_manager.h               # ✅ WiFi manager
│   ├── health_monitor.h             # ✅ Health monitor
│   ├── ocpp_state_machine.h         # ✅ OCPP state machine
│   ├── security_manager.h           # ✅ Security manager
│   └── production_config.h          # ✅ Production config
│
├── 📁 lib/                          # External Libraries
│   ├── MicroOcpp/                   # ✅ OCPP 1.6/2.0.1 library (v1.2.0)
│   └── README                       # Library info
│
├── 📁 docs/                         # Documentation
│   ├── HARDWARE_SETUP.md            # ✅ Hardware wiring guide
│   ├── CHANGELOG.md                 # ✅ Version history
│   ├── DOCUMENTATION.md             # ✅ Quick start guide
│   ├── PROJECT_STRUCTURE.md         # ✅ Project overview
│   ├── OCPP_REFACTOR.md            # ✅ OCPP refactoring notes
│   └── PRODUCTION_READINESS_ASSESSMENT.md  # ✅ Production checklist
│
├── 📁 .pio/                         # PlatformIO Build (auto-generated, gitignored)
│
├── .gitignore                       # ✅ Git ignore rules
├── platformio.ini                   # ✅ Build configuration
├── README.md                        # ✅ Main project documentation
└── 27dec.code-workspace            # VS Code workspace

```

---

## 🎯 Key Files

### Entry Point
- **src/main.cpp** - Application entry, task creation, main loop

### OCPP Logic
- **src/modules/ocpp_manager.cpp** - All OCPP initialization and callbacks
- **include/ocpp/ocpp_client.h** - OCPP API interface

### Hardware Communication
- **src/drivers/can_driver.cpp** - CAN bus (TWAI) driver
- **src/drivers/bms_interface.cpp** - BMS data parsing (CAN ID 0x00433F02)
- **src/drivers/charger_interface.cpp** - Charger control (CAN ID 0x00433F03)

### Configuration
- **include/secrets.h** - WiFi & OCPP credentials (gitignored)
- **include/config/hardware.h** - Pin definitions, voltage/current limits
- **include/config/version.h** - Firmware version

---

## 📊 Data Flow

```
CAN Bus (250kbps)
    ↓
┌─────────────────────────────────────┐
│  BMS (0x00433F02)                   │
│  - batteryAh, BMS_Imax              │
│  - SOC calculation                  │
│  - Vehicle model detection          │
└─────────────────────────────────────┘
    ↓
┌─────────────────────────────────────┐
│  Charger (0x00433F03)               │
│  - chargerTemp                      │
└─────────────────────────────────────┘
    ↓
┌─────────────────────────────────────┐
│  Terminal Values (0x00433F01)       │
│  - terminalVolt, terminalCurr       │
│  - Real measured values             │
└─────────────────────────────────────┘
    ↓
┌─────────────────────────────────────┐
│  OCPP Manager                       │
│  - Power = terminalVolt × terminalCurr
│  - Energy accumulation              │
│  - MeterValues (auto-sent 60s)      │
└─────────────────────────────────────┘
    ↓
OCPP Server (WebSocket)
```

---

## 🔧 Build Commands

```bash
# Production build
pio run -e charger_esp32_production

# Upload firmware
pio run -e charger_esp32_production --target upload

# Monitor serial
pio device monitor --baud 115200

# Clean build
pio run --target clean
```

---

## 📝 MeterValues Sent to OCPP

| Measurand | Value | Unit | Interval |
|-----------|-------|------|----------|
| Energy.Active.Import.Register | energyWh | Wh | 60s |
| Power.Active.Import | terminalVolt × terminalCurr | W | 60s |
| Energy.Active.Import.Register | batteryAh | Ah | 60s |
| Current.Import | BMS_Imax | A | 60s |
| Temperature | chargerTemp | °C | 60s |

---

## ✅ Clean Structure Benefits

- **No unused files** - All headers have implementations
- **Logical grouping** - drivers/, modules/, core/
- **Clear documentation** - All docs in docs/ folder
- **Easy navigation** - Consistent naming and structure
- **Scalable** - Easy to add new modules/drivers

---

**Version**: v2.4.0  
**Last Updated**: January 2025  
**Status**: ✅ Production Ready
