# ESP32 OCPP EV Charger Controller

Production-ready ESP32 firmware for OCPP 1.6 compliant electric vehicle charging station with CAN bus integration.

## 🚀 Features

- ✅ **OCPP 1.6 Protocol** - Full WebSocket communication with central management system
- ✅ **CAN Bus Integration** - Real-time communication with BMS and charger hardware
- ✅ **Remote Control** - Start/Stop charging via OCPP RemoteStart/RemoteStop
- ✅ **Energy Metering** - Accurate voltage, current, and energy tracking
- ✅ **WiFi Auto-Reconnect** - Automatic recovery from network failures
- ✅ **Health Monitoring** - Watchdog timer and system health checks
- ✅ **Transaction Persistence** - Resume charging after power loss
- ✅ **Multi-Core Architecture** - FreeRTOS tasks on dual-core ESP32

## 📋 Hardware Requirements

- **MCU**: ESP32 (dual-core, 240MHz)
- **CAN Interface**: GPIO21 (TX), GPIO22 (RX), 250kbps
- **WiFi**: 2.4GHz 802.11 b/g/n
- **Power**: 5V via USB or external supply
- **Flash**: 4MB minimum

## 🏗️ System Architecture

```
┌─────────────────────────────────────┐
│   OCPP Server (SteVe/Cloud)         │
│   ws://ocpp.rivotmotors.com:8080    │
└──────────────┬──────────────────────┘
               │ WebSocket
┌──────────────▼──────────────────────┐
│   ESP32 OCPP Controller             │
│   ┌─────────────────────────────┐   │
│   │ Core 0: OCPP Task           │   │
│   │ - WebSocket communication   │   │
│   │ - Transaction management    │   │
│   │ - State machine             │   │
│   └─────────────────────────────┘   │
│   ┌─────────────────────────────┐   │
│   │ Core 1: Hardware Tasks      │   │
│   │ - CAN RX (Priority 8)       │   │
│   │ - Charger Comm (Priority 7) │   │
│   │ - UI Console (Priority 2)   │   │
│   └─────────────────────────────┘   │
└──────────────┬──────────────────────┘
               │ CAN Bus (250kbps)
┌──────────────▼──────────────────────┐
│   BMS + Charger Hardware            │
│   - Battery voltage/current/SOC     │
│   - Charger control                 │
│   - Safety monitoring               │
└─────────────────────────────────────┘
```

## 📁 Project Structure

```
microocpp/
├── src/
│   ├── main.cpp                    # Application entry point
│   ├── drivers/
│   │   ├── can_driver.cpp          # CAN/TWAI driver
│   │   ├── bms_interface.cpp       # BMS communication
│   │   └── charger_interface.cpp   # Charger control
│   ├── modules/
│   │   ├── wifi_manager.cpp        # WiFi with auto-reconnect
│   │   ├── health_monitor.cpp      # Watchdog & health checks
│   │   ├── ocpp_state_machine.cpp  # OCPP state management
│   │   ├── security_manager.cpp    # TLS/WSS security
│   │   ├── production_config.cpp   # NVS persistence
│   │   └── ui_console.cpp          # Serial console UI
│   ├── core/
│   │   └── system.cpp              # Core system services
│   └── config/
│       └── config.cpp              # Configuration
├── include/
│   ├── header.h                    # Global declarations
│   ├── secrets.h                   # WiFi & server credentials
│   ├── config/
│   │   ├── hardware.h              # Pin definitions, limits
│   │   ├── timing.h                # Timeouts, intervals
│   │   └── version.h               # Firmware version
│   ├── drivers/
│   │   ├── can_driver.h
│   │   ├── bms_interface.h
│   │   └── charger_interface.h
│   └── modules/
│       ├── wifi_manager.h
│       ├── health_monitor.h
│       ├── ocpp_state_machine.h
│       ├── security_manager.h
│       └── production_config.h
├── lib/
│   └── MicroOcpp/                  # OCPP 1.6 library
├── platformio.ini                  # Build configuration
└── README.md                       # This file
```

## ⚙️ Configuration

### 1. WiFi & Server Settings

Copy `include/secrets.h.example` to `include/secrets.h` and edit:

```cpp
#define SECRET_WIFI_SSID "YourWiFiSSID"
#define SECRET_WIFI_PASS "YourWiFiPassword"
#define SECRET_CHARGER_ID "YOUR_CHARGER_ID"
#define SECRET_CHARGER_MODEL "Your Charger Model"
#define SECRET_CHARGER_VENDOR "Your Company Name"
```

The OCPP server URL is automatically constructed from the charger ID.

### 2. Hardware Configuration

Edit `include/config/hardware.h`:

```cpp
#define CAN_TX_PIN GPIO_NUM_21
#define CAN_RX_PIN GPIO_NUM_22
#define CAN_BAUDRATE 250000

#define MIN_VOLTAGE_V 56.0f
#define MAX_VOLTAGE_V 85.5f
#define MAX_CURRENT_A 300.0f
```

### 3. CAN Bus Configuration

The firmware uses **terminal values** (real measured values from CAN ID `0x00433F01`) for all OCPP metering:

- **Terminal Voltage**: Big-endian float, bytes 0-3
- **Terminal Current**: Big-endian float, bytes 4-7
- **Scaling**: Current uses `/10.0f` scaling factor
- **Valid Ranges**: 56-85.5V, 0-300A

These values are automatically sent to OCPP server every 60 seconds during charging.

## 🔨 Building & Flashing

### Prerequisites

1. Install [PlatformIO](https://platformio.org/install)
2. Install USB drivers for ESP32

### Build Commands

```bash
# Production build (optimized)
pio run -e charger_esp32_production

# Debug build (verbose logging)
pio run -e charger_esp32_debug

# Upload to ESP32
pio run -e charger_esp32_production --target upload

# Monitor serial output
pio device monitor --baud 115200
```

## 🎮 Usage

### Debug Menu (NEW!)

Serial monitor now shows a numbered debug menu:

```
========== DEBUG MENU ==========
1 - BMS <--> MCU
2 - MCU <--> Charger Module
3 - OCPP Client
4 - WiFi
5 - State Machine
6 - System
0 - Debug ALL
9 - Stop Debug
================================
```

**Quick Start**:
- Press `1-6` to debug specific section
- Press `0` to debug everything
- Press `9` to stop debug output
- Press `h` to show menu again

See [Debug Menu Guide](docs/guides/DEBUG_MENU.md) for detailed guide.

### Serial Console Commands

Once running, use serial monitor (115200 baud):

```
1 - Show BMS data
2 - Show charger data
3 - Show output voltage/current/temperature
4 - Show terminal data
5 - Show all data
s - Start charging (manual)
t - Stop charging (manual)
0 - Mute output
```

### OCPP Remote Control

From your OCPP server (e.g., SteVe):

1. **Start Charging**: Send `RemoteStartTransaction` with RFID tag
2. **Stop Charging**: Send `RemoteStopTransaction` with transaction ID
3. **Monitor**: View real-time MeterValues (voltage, current, energy)

## 📊 System Status

The system reports status every 10 seconds:

```
[Status] Uptime: 120s | WiFi: ✅ | OCPP: Connected | State: Available
[Metrics] V=76.2V I=1.8A SOC=83% Energy=3.42Wh
```

**Status Indicators:**
- `WiFi: ✅` - Connected to WiFi network
- `OCPP: Connected` - WebSocket connected to OCPP server
- `State: Available/Preparing/Charging/Finishing` - Connector state

## 📚 Documentation

Comprehensive documentation is available in the [`docs/`](docs/) directory:

- **[API Reference](docs/api/)** - OCPP messages and data transmission
- **[Guides](docs/guides/)** - Setup, deployment, and testing guides  
- **[Troubleshooting](docs/troubleshooting/)** - Common issues and fixes

See [Documentation Index](docs/README.md) for complete list.

## 🔧 Troubleshooting

### Charger Fault at Startup (NEW - CRITICAL!)

```
[OCPP] StatusNotification: Faulted / OtherError
[OCPP] RemoteStart: REJECTED
```
**Root Cause**: Charger module expects both CAN IDs (0x068181FE and 0x068182FE) at startup. Previous firmware only sent Group 2 when gun was connected.

**Solution**: Firmware v2.5.1 fixes this by:
1. Sending both groups at startup (initialization sequence)
2. Always sending both groups continuously (not conditional)
3. Re-initializing after CAN recovery

**See**: [CHARGER_FAULT_FIX.md](CHARGER_FAULT_FIX.md) for detailed explanation

### CAN Bus Stability Issues

```
[CAN] 🚨 BUS-OFF detected, initiating recovery...
```
**Solution**: See comprehensive fix guides:
- **[CAN Bus Fixes Summary](CAN_BUS_FIXES_SUMMARY.md)** - Quick reference for all fixes
- **[Firmware Fixes](FIRMWARE_FIXES_IMPLEMENTED.md)** - Detailed firmware changes (v2.5.0)
- **[Hardware Fixes Guide](HARDWARE_FIXES_GUIDE.md)** - Step-by-step hardware improvements

**Quick Fixes**:
1. Add 120Ω termination resistors at both ends of CAN bus
2. Replace with shielded twisted-pair cable (CAT5e)
3. Route CAN wires away from 30A power cables
4. Verify common ground between ESP32 and charger module

### WiFi Connection Issues

```
[WiFi] ❌ Initial connection failed
```
**Solution**: Check SSID/password in `secrets.h`, verify 2.4GHz network

### OCPP Connection Issues

```
[MO] info (Connection.cpp:74): Disconnected
```
**Solution**: 
- Verify server URL in `secrets.h`
- Check firewall allows port 8080
- Ensure charger ID is registered in OCPP server

### CAN Bus Issues

```
📊 CAN Status: State=BUS_OFF
```
**Solution**: 
- Check CAN wiring (CANH, CANL, GND)
- Verify 120Ω termination resistors
- Check baud rate matches hardware (250kbps)

### Zero Current Readings

```
[Metrics] V=83.8V I=0.0A
```
**Solution**: 
- Firmware now uses terminal values (CAN ID 0x00433F01) instead of charger values
- Current scaling fixed to `/10.0f`
- If still zero, check CAN bus connection and verify hardware is sending terminal data

### Memory Issues

```
[System] ❌ NVS Flash init failed
```
**Solution**: Erase flash and reflash firmware:
```bash
pio run --target erase
pio run --target upload
```

### Invalid Transaction ID (-1)

```
[PERSIST] Restored transaction: -1
```
**Solution**: One-time NVS cleanup (already fixed in v2.4.0+):
```bash
pio run --target erase  # Clear old NVS data
pio run -e charger_esp32_production --target upload
```

## 🔐 Security Notes

**v2.6.0 Security Improvements:**
- ✅ **Memory Safety**: All buffer operations bounds-checked
- ✅ **Input Validation**: CAN messages validated before processing
- ✅ **Secure Storage**: Credentials encrypted in NVS (migration required)
- ✅ **Concurrency**: Race conditions eliminated with timeout mutexes
- ✅ **Error Handling**: All critical paths have proper error checks

**Migration to Secure Credentials:**
```cpp
// First boot - store credentials securely
SecureCredentials::g_secureCredentials.init();
SecureCredentials::g_secureCredentials.storeWiFiCredentials("SSID", "Password");
SecureCredentials::g_secureCredentials.storeOCPPCredentials("host", 443, "charger_id");
```

**For Production Deployment:**
1. ✅ Enable WSS (secure WebSocket) with valid SSL certificate
2. ✅ Migrate credentials to encrypted NVS (see SECURITY_FIXES_v2.6.0.md)
3. ✅ Enable flash encryption on ESP32
4. ✅ Implement certificate pinning
5. ✅ Enable OTA signature verification
6. ✅ Disable debug logging in production builds

**Security Documentation:**
- [SECURITY_FIXES_v2.6.0.md](SECURITY_FIXES_v2.6.0.md) - Complete security guide
- [SECURITY_QUICK_REFERENCE.md](SECURITY_QUICK_REFERENCE.md) - Developer quick reference

## 📈 Performance

- **Boot Time**: ~5 seconds to OCPP connection
- **CAN Latency**: <10ms message processing
- **OCPP Latency**: ~100-200ms round-trip
- **Memory Usage**: ~180KB RAM, ~1.2MB Flash
- **CPU Usage**: ~15% average (dual-core)

## 🛠️ Development

### Task Priorities

```
Priority 8: CAN RX (safety-critical)
Priority 7: Charger Communication
Priority 3: OCPP Loop
Priority 2: UI Console
```

### Adding New Features

1. Create module in `src/modules/` and `include/modules/`
2. Add task in `setup()` with appropriate priority
3. Update this README

## 📝 Version History

- **v2.6.0** (January 2025) - CRITICAL: Security & Memory Safety Fixes
  - ✅ Fixed all buffer overflow vulnerabilities with SafeString utilities
  - ✅ Added comprehensive CAN message validation (CANValidator)
  - ✅ Implemented secure credential storage (encrypted NVS)
  - ✅ Fixed race conditions with timeout-based mutex acquisition
  - ✅ Added input validation for all CAN data (voltage, current, SOC)
  - ✅ Eliminated hardcoded credentials security risk
  - See [SECURITY_FIXES_v2.6.0.md](SECURITY_FIXES_v2.6.0.md) for complete details
  - See [SECURITY_QUICK_REFERENCE.md](SECURITY_QUICK_REFERENCE.md) for developer guide
- **v2.5.1** (January 2025) - CRITICAL: Charger fault fix
  - Fixed charger entering fault state at startup
  - Send both CAN groups (0x068181FE and 0x068182FE) at initialization
  - Removed conditional Group 2 sending (always send both groups)
  - Added re-initialization after CAN recovery
  - See [CHARGER_FAULT_FIX.md](CHARGER_FAULT_FIX.md) for details
- **v2.5.0** (January 2025) - CAN bus stability improvements
  - Increased CAN recovery tolerance (5s timeout)
  - Disable voltage-drop disconnect during CAN recovery
  - Global CAN recovery flag for cross-task coordination
  - See [FIRMWARE_FIXES_IMPLEMENTED.md](FIRMWARE_FIXES_IMPLEMENTED.md) for details
- **v2.0** - Production-ready with OCPP 1.6, CAN bus, WiFi auto-reconnect
- **v1.0** - Initial prototype

## 📄 License

Copyright © 2025 Rivot Motors. All rights reserved.

## 🤝 Support

For issues or questions:
- Check troubleshooting section above
- Review serial console logs
- Contact: support@rivotmotors.com

---

**Status**: ✅ Production Ready | **Last Updated**: January 2025
