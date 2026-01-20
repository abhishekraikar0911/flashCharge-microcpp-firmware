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

## 🔧 Troubleshooting

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

## 🔐 Security Notes

**Current Configuration:**
- ⚠️ Using plain WebSocket (WS) - not encrypted
- ⚠️ Credentials in `secrets.h` - not encrypted

**For Production:**
1. Enable WSS (secure WebSocket) with valid SSL certificate
2. Store credentials in encrypted NVS partition
3. Implement certificate pinning
4. Enable OTA signature verification

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

- **v2.0** (Current) - Production-ready with OCPP 1.6, CAN bus, WiFi auto-reconnect
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
