# Structured Logging System

## Overview
Clean, organized serial monitor output with consistent formatting across all modules.

## Features
- **Categorized logs**: SYSTEM, CAN, OCPP, WIFI, BMS, CHARGER, SAFETY, HEALTH
- **Log levels**: DEBUG, INFO, WARN, ERROR, CRITICAL with icons
- **Formatted sections**: Headers, footers, boxes, tables
- **Easy-to-read**: Aligned columns, clear separators

## Quick Start

### Include Headers
```cpp
#include "utils/log_macros.h"
```

### Basic Logging
```cpp
// Simple messages
LOG_INFO(SYSTEM, "WiFi connected");
LOG_ERROR(CAN, "Bus-off detected");
LOG_WARN(CHARGER, "Temperature high");

// Formatted messages
LOG_INFO_F(OCPP, "Transaction started: ID=%d", txId);
LOG_ERROR_F(CAN, "TX errors: %d", errorCount);
```

### Section Headers
```cpp
LOG_SECTION_START("CAN BUS STATUS REPORT");
LOG_DATA("State", "RUNNING");
LOG_DATA_UNIT("TX Errors", 0, "");
LOG_DATA_UNIT("Temperature", 45.2, "°C");
LOG_SECTION_END();
```

## Before & After Examples

### BEFORE (Messy)
```
[CAN] 🚨 BUS-OFF detected, initiating recovery...
[CAN_RECOVERY] Step 1: Deinitializing TWAI driver...
[CAN_RECOVERY] ✅ Deinit successful
[CAN_RECOVERY] Step 2: Reinitializing TWAI driver...
[CAN1] Initializing TWAI...
[CAN1] ✅ TWAI initialized successfully
```

### AFTER (Clean)
```
╔═══════════════════════════════════════════════════════════════╗
║  CAN BUS RECOVERY                                              ║
╚═══════════════════════════════════════════════════════════════╝
[CAN] 🚨 Bus-off detected, initiating recovery
[CAN] ℹ️  Step 1/4: Deinitializing TWAI driver
[CAN] ℹ️  Step 2/4: Reinitializing TWAI driver
[CAN] ℹ️  Step 3/4: Disabling charging for safety
[CAN] ℹ️  Step 4/4: Marking for re-initialization
[CAN] ℹ️  Recovery complete
╚═══════════════════════════════════════════════════════════════╝
```

## Refactoring Examples

### 1. CAN Status Report
**BEFORE:**
```cpp
Serial.println("╔═════════════════════════════════════════════════════════╗");
Serial.println("║  CAN BUS STATUS REPORT (Every 10s)                            ║");
Serial.println("╚═════════════════════════════════════════════════════════╝");
Serial.printf("[CAN_STATUS] State: %d\n", state);
Serial.printf("[CAN_STATUS] TX Errors: %d | RX Errors: %d\n", txErr, rxErr);
```

**AFTER:**
```cpp
LOG_SECTION_START("CAN BUS STATUS REPORT");
LOG_DATA("State", getStateStr(state));
LOG_DATA("TX Errors", txErr);
LOG_DATA("RX Errors", rxErr);
LOG_DATA("Bus Errors", busErr);
LOG_SECTION_END();
```

### 2. OCPP Connection
**BEFORE:**
```cpp
Serial.println("[OCPP] Connection status changed: CONNECTED");
Serial.println("[OCPP] 🔄 RECONNECTED! Forcing BootNotification sync...");
Serial.println("[OCPP]   ✅ BootNotification queued for sync");
```

**AFTER:**
```cpp
LOG_INFO(OCPP, "Connection established");
LOG_INFO(OCPP, "Forcing BootNotification sync");
LOG_INFO(OCPP, "BootNotification queued");
```

### 3. Vehicle Info
**BEFORE:**
```cpp
Serial.printf("[OCPP] 📤 Sending VehicleInfo (Pre-Tx):\n");
Serial.printf("  SOC=%.1f%% | Model=%s | Range=%.1fkm\n", soc, model, range);
```

**AFTER:**
```cpp
LOG_SECTION_START("VEHICLE INFO (Pre-Transaction)");
LOG_DATA_UNIT("State of Charge", soc, "%");
LOG_DATA("Model", model);
LOG_DATA_UNIT("Range", range, "km");
LOG_DATA_UNIT("Max Current", maxI, "A");
LOG_DATA("VIN", vin);
LOG_SECTION_END();
```

### 4. Safety Alerts
**BEFORE:**
```cpp
Serial.println("\\n╔═══════════════════════════════════════════════════════════════╗");
Serial.println("║  🚨 EMERGENCY STOP TRIGGERED - BMS SAFETY CHECK FAILED 🚨    ║");
Serial.println("╚═══════════════════════════════════════════════════════════════╝");
Serial.printf("[SAFETY] ⏱️  Timestamp: %lu ms\n", millis());
```

**AFTER:**
```cpp
LOG_SECTION_START("EMERGENCY STOP - BMS SAFETY FAILURE");
LOG_CRITICAL_F(SAFETY, "Timestamp: %lu ms", millis());
LOG_DATA("BMS State", bmsSafeToCharge ? "SAFE" : "UNSAFE");
LOG_DATA_UNIT("Voltage", voltage, "V");
LOG_DATA_UNIT("Current", current, "A");
LOG_SECTION_END();
```

## Output Examples

### System Startup
```
╔═══════════════════════════════════════════════════════════════╗
║  ESP32 OCPP EVSE CONTROLLER - v2.5.1                          ║
╚═══════════════════════════════════════════════════════════════╝
[SYS] ℹ️  Build: 2025-01-15 10:30:00
[SYS] ℹ️  Station ID: 250822008C06
[SYS] ℹ️  CSMS: ocpp.rivotmotors.com:8080
───────────────────────────────────────────────────────────────
[SYS] ℹ️  Initializing NVS Flash
[SYS] ℹ️  Initializing CAN buses
[SYS] ℹ️  Initializing WiFi
[SYS] ℹ️  All systems initialized
╚═══════════════════════════════════════════════════════════════╝
```

### Periodic Status
```
╔═══════════════════════════════════════════════════════════════╗
║  SYSTEM STATUS (Every 10s)                                     ║
╚═══════════════════════════════════════════════════════════════╝
  Uptime                    : 86102 ms
  WiFi Status               : Connected
  OCPP Status               : Connected
  State Machine             : Preparing
  Transaction               : Idle
  Charging Enabled          : No
───────────────────────────────────────────────────────────────
  Battery Voltage           : 76.10 V
  Battery Current           : 0.00 A
  State of Charge           : 82.00 %
  Temperature               : 35.20 °C
╚═══════════════════════════════════════════════════════════════╝
```

## Integration Steps

1. **Add to platformio.ini** (already included in src/)
2. **Include in main files:**
   ```cpp
   #include "utils/log_macros.h"
   ```
3. **Replace existing logs** with structured versions
4. **Test output** in serial monitor

## Benefits

✅ **Consistent formatting** - All logs follow same structure
✅ **Easy to scan** - Clear categories and levels
✅ **Better debugging** - Grouped related information
✅ **Professional output** - Clean, organized appearance
✅ **Less clutter** - Reduced repetitive formatting code

## Configuration

Disable verbose MicroOcpp logs to reduce noise:
```cpp
// In main.cpp setup()
mocpp_set_console_out([](const char* msg) {
    // Filter out verbose MicroOcpp logs
    if(strstr(msg, "verbose") == nullptr) {
        Serial.print(msg);
    }
});
```
