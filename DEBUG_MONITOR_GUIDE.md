# 🔍 Debug Monitor System - Usage Guide

## Overview

A comprehensive debug monitoring system with:
- ✅ Color-coded logging (5 levels)
- ✅ Structured diagnostics display
- ✅ Interactive command interface
- ✅ Real-time dashboard
- ✅ Memory tracking
- ✅ Transaction gate monitoring

---

## 🚀 Quick Start

### 1. Add to your main.cpp

```cpp
#include "debug_monitor.h"
#include "diagnostics.h"
#include "debug_commands.h"

void setup() {
    Serial.begin(115200);
    
    // Initialize debug system
    Debug.init(LOG_INFO, true, true);  // Level, Colors, Timestamp
    DebugCommands::printBanner();
    DebugCommands::CommandProcessor::init();
    
    // Your existing setup code...
}

void loop() {
    // Process debug commands
    if (Serial.available()) {
        char cmd = Serial.read();
        DebugCommands::CommandProcessor::process(cmd);
    }
    
    // Auto-update dashboard every 10s
    if (DebugCommands::CommandProcessor::shouldUpdateDashboard()) {
        Diagnostics::printFullDashboard(
            millis() / 1000,           // uptime
            WiFi.isConnected(),        // wifi
            ocpp::isConnected(),       // ocpp
            g_ocppStateMachine.getStateName(),  // state
            terminalVolt,              // voltage
            terminalCurr,              // current
            chargerTemp,               // temperature
            socPercent,                // soc
            rangeKm,                   // range
            energyWh,                  // energy
            transactionActive,         // txActive
            activeTransactionId,       // txId
            remoteStartAccepted,       // remoteStart
            chargingEnabled,           // charging
            1,                         // canState
            0,                         // txErr
            0                          // rxErr
        );
    }
    
    // Your existing loop code...
}
```

---

## 📝 Logging System

### Log Levels

```cpp
LOG_D("TAG", "Debug message: %d", value);     // Gray - Detailed debug info
LOG_I("TAG", "Info message: %s", text);       // Cyan - General information
LOG_W("TAG", "Warning: %.2f", value);         // Yellow - Warnings
LOG_E("TAG", "Error: %s", error);             // Red - Errors
LOG_C("TAG", "Critical: %d", code);           // Magenta - Critical errors
```

### Example Usage

```cpp
// In your code:
LOG_I("OCPP", "RemoteStart received");
LOG_W("GATE", "Transaction gate closed: txId=%d", txId);
LOG_E("CAN", "Bus-off detected, recovering...");
LOG_C("SAFETY", "Emergency stop triggered!");
```

### Output Format

```
[  12.345] [INFO ] [OCPP    ] RemoteStart received
[  12.456] [WARN ] [GATE    ] Transaction gate closed: txId=0
[  12.567] [ERROR] [CAN     ] Bus-off detected, recovering...
```

---

## 📊 Diagnostics Display

### Full Dashboard

```cpp
Diagnostics::printFullDashboard(
    uptime, wifi, ocpp, state,
    volt, curr, temp, soc, range, energy,
    txActive, txId, remoteStart, charging,
    canState, txErr, rxErr
);
```

**Output**:
```
╔════════════════════════════════════════════════════════════════════════════╗
║                          SYSTEM STATUS DASHBOARD                           ║
╚════════════════════════════════════════════════════════════════════════════╝

⏱️  Uptime: 120s | 📡 WiFi: ✅ | 🔌 OCPP: ✅ | 🔋 State: Charging

┌─ TRANSACTION GATE ─────────────────────────────────────────────────────────┐
│ Active: TRUE  │ TxID: 123    │ RemoteStart: TRUE  │ Status: 🟢 OPEN
└────────────────────────────────────────────────────────────────────────────┘

┌─ HARDWARE METRICS ─────────────────────────────────────────────────────────┐
│ Voltage:  76.40V │ Current:  12.50A │ Power:  955.00W │ Temp:  25.6°C │
│ SOC:    83.5%   │ Range:   225.4km │ Energy:  342.50Wh │              │
└────────────────────────────────────────────────────────────────────────────┘

┌─ OCPP STATUS ──────────────────────────────────────────────────────────────┐
│ Connection: 🟢 ONLINE  │ State: Charging    │ TX: ACTIVE │ Running: YES  │
└────────────────────────────────────────────────────────────────────────────┘

┌─ CAN BUS STATUS ───────────────────────────────────────────────────────────┐
│ State: RUNNING │ TX_Err:   0 │ RX_Err:   0 │ TX_Q:   0 │ RX_Q:   0 │
└────────────────────────────────────────────────────────────────────────────┘

┌─ MEMORY USAGE ─────────────────────────────────────────────────────────────┐
│ Free Heap:  180432 bytes │ Min Free:  165248 bytes │ Largest Block:  110592 │
└────────────────────────────────────────────────────────────────────────────┘
```

### Compact Status (for frequent updates)

```cpp
Diagnostics::printCompactStatus(
    uptime, wifi, ocpp, state, volt, curr, soc, charging
);
```

**Output**:
```
[  120s] WiFi:✓ OCPP:✓ State:Charging   V:76.4 I:12.5 SOC:83% Charge:ON
```

### Individual Components

```cpp
// Transaction gate
Diagnostics::printGateStatus(txActive, txId, remoteStart);

// Hardware metrics
Diagnostics::printHardwareStatus(volt, curr, temp, soc, range);

// OCPP status
Diagnostics::printOCPPStatus(connected, state, txActive, txRunning);

// CAN bus
Diagnostics::printCANStatus(state, txErr, rxErr, txQ, rxQ);

// Memory
Diagnostics::printMemoryStats();
```

---

## 🎮 Interactive Commands

### Available Commands

| Command | Description |
|---------|-------------|
| `h` | Show help menu |
| `s` | Show full system status |
| `m` | Show memory statistics |
| `r` | Reboot system |
| `t` | Show transaction gate status |
| `v` | Show voltage/current/power |
| `c` | Show CAN bus status |
| `o` | Show OCPP connection status |
| `0-3` | Set log level (0=DEBUG, 3=ERROR) |
| `d` | Toggle auto-dashboard |
| `+` | Increase update interval |
| `-` | Decrease update interval |

### Usage Example

```
Type 'h' for help
> h
[Shows help menu]

> s
[Shows full system status]

> 0
[Sets log level to DEBUG]

> d
[Toggles auto-dashboard OFF]

> +
[Increases update interval to 15s]
```

---

## 🎨 Color Scheme

| Level | Color | Use Case |
|-------|-------|----------|
| DEBUG | Gray | Detailed debugging info |
| INFO | Cyan | Normal operations |
| WARN | Yellow | Warnings, non-critical issues |
| ERROR | Red | Errors, failures |
| CRITICAL | Magenta | Critical failures, safety issues |

---

## 📋 Integration Examples

### Example 1: Log Transaction Events

```cpp
// In ocpp_manager.cpp
if (notification == TxNotification_StartTx) {
    LOG_I("OCPP", "Transaction %d started", txId);
    Diagnostics::printTransactionEvent("START", txId, "RemoteStart");
}
```

### Example 2: Log Safety Events

```cpp
// In main.cpp
if (!bmsSafeToCharge) {
    LOG_C("SAFETY", "BMS charging disabled!");
    Diagnostics::printError("BMS", "Charging permission revoked");
}
```

### Example 3: Log CAN Bus Events

```cpp
// In charger_interface.cpp
if (s.state == TWAI_STATE_BUS_OFF) {
    LOG_E("CAN", "Bus-off detected, recovering...");
}
```

---

## 🔧 Configuration

### Change Log Level at Runtime

```cpp
Debug.setLevel(LOG_DEBUG);  // Show all logs
Debug.setLevel(LOG_WARN);   // Only warnings and above
```

### Disable Colors (for file logging)

```cpp
Debug.init(LOG_INFO, false, true);  // No colors
```

### Disable Timestamps

```cpp
Debug.init(LOG_INFO, true, false);  // No timestamps
```

---

## 📈 Performance Impact

- **Memory**: ~2KB RAM overhead
- **CPU**: <1% average (with 10s dashboard updates)
- **Serial**: 115200 baud recommended

---

## 🎯 Best Practices

1. **Use appropriate log levels**:
   - DEBUG: Detailed flow, variable values
   - INFO: State changes, events
   - WARN: Unexpected but handled conditions
   - ERROR: Failures, retries
   - CRITICAL: Safety issues, system failures

2. **Keep messages concise**:
   ```cpp
   // Good
   LOG_I("OCPP", "TX %d started", txId);
   
   // Bad
   LOG_I("OCPP", "The transaction with ID %d has been successfully started and is now active", txId);
   ```

3. **Use structured tags**:
   - "OCPP", "CAN", "BMS", "GATE", "SAFETY", "WIFI", etc.

4. **Dashboard updates**:
   - Use 10s interval for production
   - Use 5s for debugging
   - Use compact status for <5s updates

---

## 🐛 Troubleshooting

### Colors not showing?
- Use a terminal that supports ANSI colors (PlatformIO Serial Monitor, PuTTY, etc.)
- Arduino IDE Serial Monitor doesn't support colors

### Dashboard too slow?
- Increase update interval: Press `+` key
- Disable auto-dashboard: Press `d` key

### Too much output?
- Increase log level: Press `2` (WARN) or `3` (ERROR)
- Disable auto-dashboard: Press `d`

---

**Created**: January 2025  
**Version**: 1.0  
**Status**: Production Ready
