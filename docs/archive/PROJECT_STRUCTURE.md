# ESP32 OCPP EVSE Controller - Project Structure

## 📁 Directory Layout

```
microocpp/
├── src/                    # Source code
│   ├── main.cpp           # Application entry point
│   ├── drivers/           # Hardware drivers (CAN, BMS, Charger)
│   ├── modules/           # System modules (OCPP, WiFi, Health, OTA)
│   ├── core/              # Core system functions
│   └── config/            # Configuration implementations
│
├── include/               # Header files
│   ├── header.h          # Global declarations
│   ├── secrets.h         # WiFi & OCPP credentials (gitignored)
│   ├── config/           # Hardware, timing, version configs
│   ├── drivers/          # Driver interfaces
│   ├── modules/          # Module interfaces
│   └── ocpp/             # OCPP client interface
│
├── lib/                   # Libraries
│   ├── MicroOcpp/        # OCPP 1.6 library
│   └── mcp2515/          # CAN controller library
│
├── docs/                  # Documentation
│   ├── README.md         # Documentation index
│   ├── HARDWARE_SETUP.md # Hardware wiring guide
│   ├── guides/           # User guides
│   └── troubleshooting/  # Troubleshooting guides
│
├── scripts/               # Utility scripts
│   ├── safe_remote_start_stop.sh  # OCPP testing
│   ├── sign_firmware.py           # OTA signing
│   └── serve_firmware.py          # OTA server
│
├── keys/                  # OTA signing keys
│   ├── ota_private_key.pem
│   └── ota_public_key.pem
│
├── refernece folder/      # Hardware datasheets
│
├── platformio.ini         # Build configuration
└── README.md             # Main project documentation
```

## 🗂️ Key Files

### Essential Configuration
- `platformio.ini` - Build settings, dependencies
- `include/secrets.h` - WiFi & OCPP credentials (create from secrets.h.example)
- `include/config/hardware.h` - Pin definitions, safety limits
- `include/config/timing.h` - Timeouts, intervals
- `include/config/version.h` - Firmware version

### Core Source Files
- `src/main.cpp` - Main application loop
- `src/modules/ocpp_manager.cpp` - OCPP client logic
- `src/modules/ocpp_state_machine.cpp` - State management
- `src/drivers/can_twai_driver.cpp` - Charger CAN bus
- `src/drivers/can_mcp2515_driver.cpp` - BMS CAN bus
- `src/drivers/charger_interface.cpp` - Charger control
- `src/drivers/bms_interface.cpp` - BMS communication

### Documentation
- `README.md` - Quick start guide
- `docs/HARDWARE_SETUP.md` - Wiring diagrams
- `docs/DOCUMENTATION.md` - Complete technical docs

## 🧹 Cleaned Up

Removed 35+ obsolete development documentation files:
- Debug analysis files
- Migration guides (SteVe → CitrineOS completed)
- Bug fix tracking documents
- Implementation plans (completed)
- Test procedure documents (completed)

## 📦 Build Artifacts (Gitignored)

- `.pio/` - PlatformIO build cache
- `.vscode/` - VSCode settings
- `secrets.h` - Credentials (never commit!)

## 🚀 Quick Commands

```bash
# Build production firmware
pio run -e charger_esp32_production

# Upload to ESP32
pio run -e charger_esp32_production --target upload

# Monitor serial output
pio device monitor --baud 115200

# Clean build
pio run --target clean
```

---
**Last Updated**: January 2025  
**Status**: Production Ready ✅
