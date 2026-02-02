# Plug Detection Issue - Capacitor Discharge Delay

## Problem Description

**Issue**: When customer disconnects charging gun, the system continues to show "gunPhysicallyConnected = true" for over 1 minute, preventing the next customer from starting a new charging session.

**Root Cause**: Capacitors in the charger module hold voltage after physical disconnection. Voltage drops slowly (step-by-step) instead of immediately, taking 60+ seconds to fall below the 56V detection threshold.

## Current Detection Logic

### Location: `src/drivers/charger_interface.cpp`

```cpp
// Gun detection based on voltage thresholds
if (Charger_Vmax > 56.0f && Charger_Vmax < 85.5f) {
    batteryConnected = true;
    gunPhysicallyConnected = true;
}

if (chargerVolt > 56.0f && chargerVolt < 84.5f) {
    batteryConnected = true;
    gunPhysicallyConnected = true;
}

if (terminalVolt > 56.0f && terminalVolt < 85.5f) {
    batteryConnected = true;
    gunPhysicallyConnected = true;
}
```

### Problem Timeline

1. **T=0s**: Customer stops charging, disconnects gun
2. **T=0-60s**: Capacitor voltage slowly drops from 83V → 56V
3. **T=60s+**: Voltage finally drops below 56V
4. **T=60s+**: System detects gun disconnected
5. **Result**: Next customer must wait 60+ seconds before "Available" status

## Suggested Solutions

### Option 1: Current Drop Detection (RECOMMENDED)
**Detect disconnection by monitoring current instead of voltage**

- **Logic**: If voltage is present BUT current = 0A for 3-5 seconds → Gun disconnected
- **Advantage**: Immediate detection (3-5s delay only)
- **Implementation**: Add current monitoring to plug detection logic
- **CAN Data**: Use `terminalCurr` from CAN ID `0x00433F01`

```
Pseudo-code:
if (terminalVolt > 56V && terminalCurr < 0.5A for 5 seconds) {
    gunPhysicallyConnected = false;
}
```

### Option 2: Voltage Drop Rate Detection
**Monitor rate of voltage decrease**

- **Logic**: If voltage dropping faster than 2V/second → Gun disconnected
- **Advantage**: Detects disconnection within 5-10 seconds
- **Implementation**: Track voltage change over time
- **Complexity**: Requires voltage history buffer

```
Pseudo-code:
if ((lastVoltage - currentVoltage) / deltaTime > 2.0V/s) {
    gunPhysicallyConnected = false;
}
```

### Option 3: BMS Communication Loss
**Detect when BMS stops responding**

- **Logic**: If no BMS messages (CAN ID `0x1806E5F4`) for 3 seconds → Gun disconnected
- **Advantage**: Direct detection of vehicle communication loss
- **Implementation**: Monitor `lastBMS` timestamp
- **Already tracked**: `lastBMS` variable exists in code

```
Pseudo-code:
if (millis() - lastBMS > 3000) {
    batteryConnected = false;
    gunPhysicallyConnected = false;
}
```

### Option 4: Hybrid Approach (MOST ROBUST)
**Combine multiple detection methods**

- **Logic**: Gun disconnected if ANY of these conditions:
  1. Voltage present + Zero current for 5s
  2. No BMS messages for 3s
  3. Voltage dropping > 2V/s
- **Advantage**: Fastest and most reliable detection
- **Implementation**: Use all three methods with OR logic

## Recommended Implementation

**Use Option 4 (Hybrid Approach)** for production reliability:

### Detection Priority:
1. **BMS Timeout** (3s) - Fastest, most reliable
2. **Current Drop** (5s) - Handles capacitor issue directly
3. **Voltage Rate** (10s) - Backup detection method

### Implementation Files:
- `src/drivers/charger_interface.cpp` - Add detection logic
- `src/drivers/bms_interface.cpp` - Monitor BMS timeout
- `include/config/hardware.h` - Add threshold constants

### Configuration Constants Needed:
```cpp
#define PLUG_DISCONNECT_CURRENT_THRESHOLD 0.5f  // Amps
#define PLUG_DISCONNECT_CURRENT_TIMEOUT 5000    // ms
#define PLUG_DISCONNECT_BMS_TIMEOUT 3000        // ms
#define PLUG_DISCONNECT_VOLTAGE_RATE 2.0f       // V/s
```

## Impact on System

### Current Behavior:
- Customer 1 disconnects → 60+ second wait
- Customer 2 sees "Finishing" status for 60s
- Poor user experience, looks like system is frozen

### After Fix:
- Customer 1 disconnects → 3-5 second detection
- Customer 2 sees "Available" within 10 seconds (Finishing timeout)
- Professional, responsive system behavior

## Testing Requirements

1. **Normal Disconnect**: Verify detection within 5 seconds
2. **Slow Voltage Drop**: Test with large capacitor bank
3. **BMS Communication**: Verify BMS timeout detection
4. **False Positives**: Ensure no false disconnects during charging
5. **Edge Cases**: Test with voltage fluctuations, CAN errors

## Next Steps

1. Review suggested solutions
2. Choose implementation approach
3. Add configuration constants
4. Implement detection logic
5. Test with real hardware
6. Validate with multiple disconnect scenarios

---

**Document Created**: January 2025  
**Status**: Awaiting approval for implementation  
**Priority**: HIGH - Affects customer experience
