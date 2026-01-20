# ESP32 OCPP EV Charger - v2.4.0

## 🎯 Quick Start

### Flash Firmware
```bash
pio run -e charger_esp32_production --target upload
pio device monitor --baud 115200
```

### Configure
Edit `include/secrets.h`:
```cpp
#define SECRET_WIFI_SSID "YourWiFi"
#define SECRET_WIFI_PASS "YourPassword"
#define SECRET_CHARGER_ID "RIVOT_100A_01"
```

---

## 📊 OCPP MeterValues Sent to Server

| MeterValue | Unit | Source | Frequency |
|-----------|------|--------|-----------|
| Energy | Wh | Session accumulation | Every 60s |
| Power | W | terminalVolt × terminalCurr | Every 60s |
| Battery Ah | Ah | BMS (current capacity) | Every 60s |
| BMS Imax | A | BMS (max current) | Every 60s |
| Temperature | °C | Charger module | Every 60s |

---

## 🔧 Server-Side Calculations

### Vehicle Model Detection
```javascript
if (BMS_Imax <= 30) → NX-100 Classic (30Ah, 81km)
if (BMS_Imax <= 60) → NX-100 Pro (60Ah, 162km)
if (BMS_Imax > 60)  → NX-100 Max (90Ah, 243km)
```

### SOC Calculation
```javascript
SOC = (currentAh / maxCapacity) × 100%
```

### Range Calculation
```javascript
Range = currentAh × 2.7 km/Ah  // Company tested
```

---

## 📁 Key Files

### Active Code
- `src/main.cpp` - Main application, OCPP initialization
- `src/drivers/bms_interface.cpp` - BMS communication, SOC calculation
- `src/drivers/charger_interface.cpp` - Charger communication
- `src/drivers/can_driver.cpp` - CAN bus driver
- `include/header.h` - Global declarations
- `src/core/globals.cpp` - Global variables

### Configuration
- `include/secrets.h` - WiFi & OCPP credentials
- `include/config/version.h` - Firmware version
- `include/config/hardware.h` - Hardware definitions

---

## 🚀 Features

- ✅ OCPP 1.6 compliant
- ✅ Auto-detect vehicle model (Classic/Pro/Max)
- ✅ Server-side SOC/Range calculation
- ✅ CAN bus integration (250kbps)
- ✅ WiFi auto-reconnect
- ✅ Charger health monitoring
- ✅ Remote start/stop
- ✅ Terminal value metering (real measurements)

---

## 📝 Version History

- **v2.4.0** - Server-side calculation (send Ah + Imax)
- **v2.3.0** - Auto-detection + vehicle model
- **v2.2.0** - Auto-detection feature
- **v2.1.0** - OCPP status synchronization
- **v2.0.0** - Production-ready release

---

## 📞 Support

For issues: support@rivotmotors.com

**Status**: ✅ Production Ready  
**Last Updated**: January 2025
