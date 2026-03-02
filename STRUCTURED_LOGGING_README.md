# 📊 Structured Logging System

Clean, organized, professional serial monitor output for ESP32 OCPP firmware.

## 🎯 Problem Solved

**Before:** Messy, inconsistent logs that are hard to read and debug
```
[MO] debug (FilesystemAdapter.cpp:382): File open successful: /tx-1-521.json
[CAN] 🚨 BUS-OFF detected, initiating recovery...
[CAN_RECOVERY] Step 1: Deinitializing TWAI driver...
[OCPP_SM] 🔌 cle detection: gun=1 voltage=76.1V  CONNECTED
```

**After:** Clean, structured logs with consistent formatting
```
╔═══════════════════════════════════════════════════════════════╗
║  CAN BUS RECOVERY                                              ║
╚═══════════════════════════════════════════════════════════════╝
[CAN] 🚨 Bus-off detected, initiating recovery
[CAN] ℹ️  Recovery 1/4: Deinitializing TWAI driver
[CAN] ℹ️  Recovery 2/4: Reinitializing TWAI driver
[CAN] ℹ️  Recovery complete
╚═══════════════════════════════════════════════════════════════╝
```

## 📦 What's Included

### Core Files
- `include/utils/log_formatter.h` - Core logging functions
- `src/utils/log_formatter.cpp` - Implementation
- `include/utils/log_macros.h` - Convenient macros

### Specialized Loggers
- `include/utils/can_status_logger.h` - CAN bus reporting
- `include/utils/ocpp_status_logger.h` - OCPP status reporting

### Documentation
- `docs/guides/STRUCTURED_LOGGING.md` - Complete guide
- `docs/guides/LOGGING_QUICK_REF.md` - Quick reference
- `REFACTORING_EXAMPLE.cpp` - Practical examples

## 🚀 Quick Start

### 1. Include Headers
```cpp
#include "utils/log_macros.h"
```

### 2. Use Structured Logs
```cpp
// Simple message
LOG_INFO(SYSTEM, "WiFi connected");

// Formatted message
LOG_ERROR_F(CAN, "TX errors: %d", errorCount);

// Section with data
LOG_SECTION_START("SYSTEM STATUS");
LOG_DATA("WiFi", "Connected");
LOG_DATA_UNIT("Uptime", 3600, "s");
LOG_SECTION_END();
```

### 3. Use Specialized Loggers
```cpp
// CAN status report (one line!)
CANStatusLogger::printStatusReport(status);

// OCPP vehicle info (one line!)
OCPPStatusLogger::printVehicleInfoSent(soc, model, range, maxI, vin);
```

## 📚 Documentation

| Document | Description |
|----------|-------------|
| [STRUCTURED_LOGGING.md](docs/guides/STRUCTURED_LOGGING.md) | Complete guide with examples |
| [LOGGING_QUICK_REF.md](docs/guides/LOGGING_QUICK_REF.md) | Quick reference for developers |
| [REFACTORING_EXAMPLE.cpp](REFACTORING_EXAMPLE.cpp) | Copy-paste examples from your code |
| [STRUCTURED_LOGGING_SUMMARY.md](STRUCTURED_LOGGING_SUMMARY.md) | Implementation summary |

## 🎨 Features

### Log Categories
- **SYSTEM** - Boot, initialization
- **CAN** - CAN bus operations
- **OCPP** - OCPP protocol
- **WIFI** - WiFi connection
- **BMS** - Battery management
- **CHARGER** - Charger module
- **SAFETY** - Safety-critical events
- **HEALTH** - Watchdog, health checks

### Log Levels
- **DEBUG** 🔍 - Detailed debugging
- **INFO** ℹ️ - Normal operations
- **WARN** ⚠️ - Warnings
- **ERROR** ❌ - Errors
- **CRITICAL** 🚨 - Critical failures

### Formatting Options
- Section headers with borders
- Data tables with aligned columns
- Status boxes
- Separators
- Consistent spacing

## 💡 Usage Examples

### Basic Logging
```cpp
LOG_INFO(SYSTEM, "System initialized");
LOG_WARN(CAN, "High error count");
LOG_ERROR(OCPP, "Connection failed");
LOG_CRITICAL(SAFETY, "Emergency stop");
```

### Formatted Logging
```cpp
LOG_INFO_F(SYSTEM, "Uptime: %lu seconds", uptime);
LOG_ERROR_F(CAN, "TX errors: %d", errors);
```

### Status Reports
```cpp
LOG_SECTION_START("SYSTEM STATUS");
LOG_DATA("WiFi", "Connected");
LOG_DATA("OCPP", "Connected");
LOG_DATA_UNIT("Temperature", 45.2, "°C");
LOG_SECTION_END();
```

### CAN Bus Logging
```cpp
twai_status_info_t status;
twai_get_status_info(&status);
CANStatusLogger::printStatusReport(status);
CANStatusLogger::printDiagnostics(status);
```

### OCPP Logging
```cpp
OCPPStatusLogger::printSystemStatus(uptime, wifi, ocpp, state, tx, charging);
OCPPStatusLogger::printVehicleMetrics(v, i, soc, range, temp, energy, model);
OCPPStatusLogger::printTransactionEvent("START", txId, idTag);
```

## 🔧 Integration

### No Build Changes Needed
Files are in `src/utils/` and `include/utils/` - PlatformIO compiles automatically.

### Gradual Migration
Migrate logs gradually:
1. Critical sections (CAN recovery, safety)
2. Periodic reports (status, metrics)
3. Debug logs

### Backward Compatible
Old `Serial.printf()` calls still work!

## 📊 Output Examples

### System Startup
```
╔═══════════════════════════════════════════════════════════════╗
║  ESP32 OCPP EVSE CONTROLLER - v2.5.1                          ║
╚═══════════════════════════════════════════════════════════════╝
[SYS] ℹ️  Build: 2025-01-15 10:30:00
[SYS] ℹ️  Station ID: 250822008C06
[SYS] ℹ️  Initializing systems
[SYS] ℹ️  All systems initialized
╚═══════════════════════════════════════════════════════════════╝
```

### CAN Status
```
╔═══════════════════════════════════════════════════════════════╗
║  CAN BUS STATUS                                                ║
╚═══════════════════════════════════════════════════════════════╝
  State                     : RUNNING
  TX Error Counter          : 0
  RX Error Counter          : 0
  TX Failed Count           : 0
  RX Missed Count           : 0
  Bus Error Count           : 0
╚═══════════════════════════════════════════════════════════════╝
```

### Vehicle Metrics
```
╔═══════════════════════════════════════════════════════════════╗
║  VEHICLE METRICS                                               ║
╚═══════════════════════════════════════════════════════════════╝
  Voltage                   : 76.10 V
  Current                   : 15.50 A
  State of Charge           : 82.00 %
  Range                     : 132.84 km
  Temperature               : 35.20 °C
  Energy Delivered          : 1250.50 Wh
  Model                     : Pro
╚═══════════════════════════════════════════════════════════════╝
```

## ✅ Benefits

- ✅ **Consistent formatting** across all modules
- ✅ **Easy to scan** with clear categories
- ✅ **Better debugging** with grouped information
- ✅ **Professional output** for production systems
- ✅ **Less code** with specialized loggers
- ✅ **Maintainable** with centralized formatting

## 🎓 Learn More

1. Read [STRUCTURED_LOGGING.md](docs/guides/STRUCTURED_LOGGING.md) for complete guide
2. Check [LOGGING_QUICK_REF.md](docs/guides/LOGGING_QUICK_REF.md) for quick reference
3. See [REFACTORING_EXAMPLE.cpp](REFACTORING_EXAMPLE.cpp) for practical examples
4. Start refactoring your logs!

## 🤝 Contributing

When adding new logs:
1. Use appropriate category and level
2. Keep messages concise
3. Use sections for grouped data
4. Test output formatting
5. Update documentation if needed

---

**Result:** Clean, professional, easy-to-debug serial output! 🎉
