# Structured Logging - Quick Reference

## 📚 Include Files

```cpp
#include "utils/log_macros.h"           // Basic logging
#include "utils/can_status_logger.h"    // CAN-specific
#include "utils/ocpp_status_logger.h"   // OCPP-specific
```

## 🎯 Basic Usage

### Simple Messages
```cpp
LOG_INFO(SYSTEM, "System initialized");
LOG_WARN(CAN, "High error count detected");
LOG_ERROR(OCPP, "Connection failed");
LOG_CRITICAL(SAFETY, "Emergency stop triggered");
```

### Formatted Messages
```cpp
LOG_INFO_F(SYSTEM, "Uptime: %lu seconds", uptime);
LOG_ERROR_F(CAN, "TX errors: %d", errorCount);
LOG_WARN_F(CHARGER, "Temperature: %.1f°C", temp);
```

## 📊 Sections & Tables

### Status Report
```cpp
LOG_SECTION_START("SYSTEM STATUS");
LOG_DATA("WiFi", "Connected");
LOG_DATA("OCPP", "Connected");
LOG_DATA_UNIT("Uptime", 3600, "s");
LOG_DATA_UNIT("Temperature", 45.2, "°C");
LOG_SECTION_END();
```

**Output:**
```
╔═══════════════════════════════════════════════════════════════╗
║  SYSTEM STATUS                                                 ║
╚═══════════════════════════════════════════════════════════════╝
  WiFi                      : Connected
  OCPP                      : Connected
  Uptime                    : 3600 s
  Temperature               : 45.20 °C
╚═══════════════════════════════════════════════════════════════╝
```

## 🚌 CAN Bus Logging

### Status Report
```cpp
twai_status_info_t status;
twai_get_status_info(&status);
CANStatusLogger::printStatusReport(status);
```

### Diagnostics
```cpp
CANStatusLogger::printDiagnostics(status);
```

### Recovery Progress
```cpp
CANStatusLogger::printRecoveryStep(1, 4, "Deinitializing TWAI driver");
CANStatusLogger::printRecoveryStep(2, 4, "Reinitializing TWAI driver");
CANStatusLogger::printRecoveryComplete(true);
```

## 🔌 OCPP Logging

### System Status
```cpp
OCPPStatusLogger::printSystemStatus(
    millis(),           // uptime
    true,               // wifi connected
    true,               // ocpp connected
    "Preparing",        // state machine
    false,              // transaction active
    false               // charging enabled
);
```

### Vehicle Metrics
```cpp
OCPPStatusLogger::printVehicleMetrics(
    76.1,               // voltage
    15.5,               // current
    82.0,               // soc
    132.8,              // range
    35.2,               // temperature
    1250.5,             // energy
    "Pro"               // model
);
```

### Transaction Events
```cpp
OCPPStatusLogger::printTransactionEvent("START", 521, "PAY_178");
OCPPStatusLogger::printTransactionEvent("STOP", 521);
OCPPStatusLogger::printTransactionEvent("REJECTED", 521);
```

### Remote Commands
```cpp
OCPPStatusLogger::printRemoteCommand("RemoteStartTransaction", true);
OCPPStatusLogger::printRemoteCommand("RemoteStopTransaction", false, "No active transaction");
```

## 🎨 Log Categories

| Category | Code | Use For |
|----------|------|---------|
| SYSTEM | `LOG_INFO(SYSTEM, ...)` | Boot, init, general system |
| CAN | `LOG_INFO(CAN, ...)` | CAN bus operations |
| OCPP | `LOG_INFO(OCPP, ...)` | OCPP protocol messages |
| WIFI | `LOG_INFO(WIFI, ...)` | WiFi connection events |
| BMS | `LOG_INFO(BMS, ...)` | Battery management |
| CHARGER | `LOG_INFO(CHARGER, ...)` | Charger module |
| SAFETY | `LOG_INFO(SAFETY, ...)` | Safety-critical events |
| HEALTH | `LOG_INFO(HEALTH, ...)` | Watchdog, health checks |

## 🎭 Log Levels

| Level | Icon | Macro | Use For |
|-------|------|-------|---------|
| DEBUG | 🔍 | `LOG_DEBUG()` | Detailed debugging info |
| INFO | ℹ️ | `LOG_INFO()` | Normal operations |
| WARN | ⚠️ | `LOG_WARN()` | Warnings, non-critical |
| ERROR | ❌ | `LOG_ERROR()` | Errors, failures |
| CRITICAL | 🚨 | `LOG_CRITICAL()` | Critical failures |

## 🔄 Migration Examples

### Before
```cpp
Serial.println("[CAN] 🚨 BUS-OFF detected, initiating recovery...");
Serial.println("[CAN_RECOVERY] Step 1: Deinitializing TWAI driver...");
Serial.println("[CAN_RECOVERY] ✅ Deinit successful");
```

### After
```cpp
LOG_CRITICAL(CAN, "Bus-off detected, initiating recovery");
CANStatusLogger::printRecoveryStep(1, 4, "Deinitializing TWAI driver");
LOG_INFO(CAN, "Deinit successful");
```

---

### Before
```cpp
Serial.printf("[OCPP] 📤 Sending VehicleInfo (Pre-Tx):\n");
Serial.printf("  SOC=%.1f%% | Model=%s | Range=%.1fkm\n", soc, model, range);
```

### After
```cpp
OCPPStatusLogger::printVehicleInfoSent(soc, model, range, maxCurrent, vin);
```

---

### Before
```cpp
Serial.println("╔═════════════════════════════════════════════════════════╗");
Serial.println("║  CAN BUS STATUS REPORT (Every 10s)                            ║");
Serial.println("╚═════════════════════════════════════════════════════════╝");
Serial.printf("[CAN_STATUS] State: %d\n", state);
```

### After
```cpp
CANStatusLogger::printStatusReport(status);
```

## 💡 Tips

1. **Use specific loggers** for CAN and OCPP (cleaner code)
2. **Group related logs** in sections
3. **Use appropriate levels** (don't overuse CRITICAL)
4. **Keep messages concise** (details in data rows)
5. **Test output** in serial monitor before committing

## 🚀 Next Steps

1. Include headers in your files
2. Replace old Serial.printf() calls
3. Test in serial monitor
4. Adjust formatting as needed
5. Commit changes

**Result:** Clean, professional, easy-to-debug serial output! ✨
