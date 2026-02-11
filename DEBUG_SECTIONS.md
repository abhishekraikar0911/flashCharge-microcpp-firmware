# Debug Sections Guide

## Overview
Structured debug logging system with 6 independent sections for easy fault isolation.

## Debug Sections

### 1. BMS <--> MCU (Section: DBG_BMS)
**Purpose**: Monitor BMS communication via CAN2 (MCP2515)
**Messages**:
- CAN RX: 0x1806E5F4 (Vmax/Imax/charging permission)
- CAN RX: 0x160B8001 (ChargingAh response)
- CAN RX: 0x160D8001 (DischargingAh response)
- Charging permission changes
- SOC calculations

**Use when debugging**:
- BMS not responding
- Wrong SOC values
- Charging permission issues
- CAN2 communication problems

### 2. MCU <--> Charger Module (Section: DBG_CHARGER)
**Purpose**: Monitor charger module communication via CAN1 (TWAI)
**Messages**:
- CAN RX: 0x0681817E (Charger control response)
- CAN RX: 0x0681827E (Charger telemetry)
- CAN RX: 0x00433F01 (Terminal power)
- CAN RX: 0x00473F01 (Terminal status)
- Charger voltage/current/temperature
- Module health status

**Use when debugging**:
- Charger not responding
- Wrong voltage/current readings
- Charger module offline
- CAN1 communication problems

### 3. OCPP Client (Section: DBG_OCPP)
**Purpose**: Monitor OCPP protocol communication
**Messages**:
- WebSocket connection status
- RemoteStart/RemoteStop requests
- Transaction start/stop
- MeterValues sending
- DataTransfer messages
- Authorization responses

**Use when debugging**:
- OCPP server connection issues
- Transaction not starting
- MeterValues not sent
- RemoteStart failures

### 4. WiFi (Section: DBG_WIFI)
**Purpose**: Monitor WiFi connection
**Messages**:
- Connection attempts
- Reconnection logic
- Signal strength
- Disconnection events

**Use when debugging**:
- WiFi not connecting
- Frequent disconnections
- Network stability issues

### 5. State Machine (Section: DBG_STATE)
**Purpose**: Monitor OCPP state transitions
**Messages**:
- State changes (Available/Preparing/Charging/Finishing)
- Plug detection events
- Transaction gate status
- Timeout events

**Use when debugging**:
- Wrong connector state
- State stuck in Finishing
- Plug detection issues
- Transaction gate problems

### 6. System (Section: DBG_SYSTEM)
**Purpose**: General system events
**Messages**:
- Boot/initialization
- Memory usage
- Task health
- General errors

**Use when debugging**:
- System crashes
- Memory leaks
- Task failures

## Serial Commands

### Data Display
- `1` - Show BMS Data
- `2` - Show Charger Data
- `3` - Show Output/Temperature
- `4` - Show Terminal Data
- `5` - Show All Data

### Debug Section Control
- `b` - Toggle BMS <--> MCU debug
- `c` - Toggle MCU <--> Charger debug
- `o` - Toggle OCPP Client debug
- `w` - Toggle WiFi debug
- `m` - Toggle State Machine debug
- `y` - Toggle System debug
- `a` - Enable ALL sections
- `n` - Disable ALL sections
- `?` - Show debug status

### Diagnostics
- `d` - MCP2515 Diagnostics (SPI/registers)

### Control
- `s` - Start Charging
- `t` - Emergency Stop
- `0` - Mute Output
- `h` - Show Menu

## Usage Examples

### Example 1: Debug BMS Communication Only
```
Type: n    (disable all)
Type: b    (enable BMS only)
Type: ?    (verify status)
```
Output shows only BMS messages:
```
[BMS] RX: ID=0x1806E5F4 DLC=8
[BMS] Vmax=76.2V Imax=30.5A Switch=ON Heating=NO
[BMS] ✅ Charging permission: GRANTED (byte4=0x00)
```

### Example 2: Debug OCPP + State Machine
```
Type: n    (disable all)
Type: o    (enable OCPP)
Type: m    (enable State Machine)
```
Output shows only OCPP and state transitions:
```
[OCPP] RemoteStart received: idTag=RFID123
[STATE] Available → Preparing (plug connected)
[OCPP] Transaction started: txId=42
[STATE] Preparing → Charging
```

### Example 3: Full Debug (All Sections)
```
Type: a    (enable all)
```
Shows all messages from all sections.

## Code Integration

### Using in Your Code
```cpp
#include "debug_logger.h"

// In setup()
DebugLogger::init();

// In your code
LOG_BMS("Battery voltage: %.1fV", voltage);
LOG_CHARGER("Module temperature: %.1f°C", temp);
LOG_OCPP("Transaction %d started", txId);
LOG_WIFI("Connected to %s", ssid);
LOG_STATE("State changed: %s → %s", oldState, newState);
LOG_SYSTEM("Free heap: %u bytes", ESP.getFreeHeap());
```

### Enable/Disable Programmatically
```cpp
DebugLogger::disable(DBG_BMS);      // Disable BMS debug
DebugLogger::enable(DBG_OCPP);      // Enable OCPP debug
DebugLogger::disableAll();          // Disable all
DebugLogger::enableAll();           // Enable all
```

## Benefits

1. **Focused Debugging**: Only see messages relevant to the problem
2. **Reduced Noise**: Filter out unrelated messages
3. **Easy Troubleshooting**: Quickly isolate communication issues
4. **Production Ready**: Can disable verbose logging in production
5. **Runtime Control**: Change debug levels without recompiling

## Troubleshooting Workflow

### Problem: Charging not starting
1. Type `n` (disable all)
2. Type `o` (enable OCPP)
3. Type `m` (enable State Machine)
4. Observe OCPP RemoteStart and state transitions

### Problem: Wrong SOC displayed
1. Type `n` (disable all)
2. Type `b` (enable BMS)
3. Observe BMS messages and SOC calculations

### Problem: Charger module offline
1. Type `n` (disable all)
2. Type `c` (enable Charger)
3. Type `d` (run MCP2515 diagnostics)
4. Observe CAN1 messages and module health

### Problem: OCPP disconnecting
1. Type `n` (disable all)
2. Type `w` (enable WiFi)
3. Type `o` (enable OCPP)
4. Observe connection status and reconnection attempts
