# Debug Menu Guide

## Serial Monitor Menu

When you open the serial monitor (115200 baud), you'll see:

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

## How to Use

### Debug Specific Section
Press the number (1-6) to see debug messages for that section only.

**Example**: Press `1` to debug BMS communication
```
✅ Debug: BMS <--> MCU ENABLED

[BMS] RX: ID=0x1806E5F4 DLC=8
[BMS] Vmax=76.2V Imax=30.5A Switch=ON Heating=NO
[BMS] ✅ Charging permission: GRANTED (byte4=0x00)
```

### Debug All Sections
Press `0` to see all debug messages from all sections.

```
✅ Debug ALL sections ENABLED

[BMS] RX: ID=0x1806E5F4 DLC=8
[CHARGER] Terminal voltage: 76.2V
[OCPP] RemoteStart received
[STATE] Available → Preparing
[WIFI] Connected to network
[SYSTEM] Free heap: 180KB
```

### Stop Debug
Press `9` to stop all debug output.

```
❌ Debug STOPPED
```

### Show Menu Again
Press `h` or `?` to show the menu again.

## Debug Sections Explained

### 1. BMS <--> MCU
- CAN2 (MCP2515) messages from BMS
- Voltage, current, SOC data
- Charging permission status
- BMS safety flags

**Use when**:
- BMS not responding
- Wrong SOC values
- Charging permission issues

### 2. MCU <--> Charger Module
- CAN1 (TWAI) messages from charger
- Terminal voltage/current
- Charger module health
- Temperature readings

**Use when**:
- Charger not responding
- Wrong voltage/current
- Module offline

### 3. OCPP Client
- WebSocket connection
- RemoteStart/RemoteStop
- Transaction events
- MeterValues sending

**Use when**:
- OCPP server issues
- Transaction not starting
- MeterValues not sent

### 4. WiFi
- Connection status
- Reconnection attempts
- Signal strength
- Network events

**Use when**:
- WiFi not connecting
- Frequent disconnections

### 5. State Machine
- State transitions
- Plug detection
- Transaction gate
- Timeout events

**Use when**:
- Wrong connector state
- State stuck
- Plug detection issues

### 6. System
- Boot/initialization
- Memory usage
- Task health
- General errors

**Use when**:
- System crashes
- Memory issues
- Task failures

## Additional Commands

- `d` - Run MCP2515 diagnostics (check SPI communication and registers)

## Troubleshooting Examples

### Problem: Charging not starting
1. Press `3` (OCPP Client)
2. Press `5` (State Machine)
3. Watch for RemoteStart and state transitions

### Problem: Wrong SOC
1. Press `1` (BMS)
2. Watch BMS messages and SOC calculations

### Problem: Charger offline
1. Press `2` (Charger Module)
2. Press `d` (MCP2515 diagnostics)
3. Watch CAN messages

### Problem: Everything broken
1. Press `0` (Debug ALL)
2. See all messages to find the issue
