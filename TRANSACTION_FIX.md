# Transaction Premature Termination - Root Cause & Fix

## Problem Summary
Transactions were starting and stopping within 1-2 seconds with invalid meter values (`meterStart: -1`, `meterStop: -1`).

## Root Causes Identified

### 1. **Invalid Energy Meter Values**
- `energyWh` was not being reset at transaction start
- Energy meter callback could return negative values if `energyWh` became negative
- No sanity checks on energy accumulation

### 2. **Missing Transaction State Separation**
- `TxNotification_RemoteStart` and `TxNotification_StartTx` were treated identically
- Charging was enabled on RemoteStart instead of waiting for StartTx
- Energy counter wasn't reset at the right time

### 3. **Insufficient State Monitoring**
- No detailed logging of plug state changes (gun vs battery)
- No logging of EV ready state changes
- No visibility into why MicroOcpp was auto-stopping transactions

## Fixes Applied

### Fix 1: Energy Meter Validation (ocpp_manager.cpp)
```cpp
setEnergyMeterInput([]() {
    // Ensure energyWh is never negative
    if (energyWh < 0.0f) {
        energyWh = 0.0f;
    }
    // Return as integer Wh with double-check
    int energyInt = (int)energyWh;
    if (energyInt < 0) energyInt = 0;
    return energyInt;
});
```

### Fix 2: Proper Transaction Lifecycle (ocpp_manager.cpp)
```cpp
setTxNotificationOutput([](MicroOcpp::Transaction *tx, TxNotification notification) {
    if (notification == TxNotification_RemoteStart) {
        // Don't enable charging yet - wait for StartTx
        Serial.println("[OCPP] 📥 RemoteStart received");
    } else if (notification == TxNotification_StartTx) {
        // NOW enable charging and reset energy
        chargingEnabled = true;
        txStartTime = millis();
        energyWh = 0.0f;  // CRITICAL: Reset at transaction start
        Serial.println("[OCPP] ▶️  Transaction started");
    }
    // ... rest of handlers
});
```

### Fix 3: Energy Accumulation Sanity Checks (main.cpp)
```cpp
if (chargingEnabled && ocppAllowsCharge && 
    terminalVolt > 56.0f && terminalVolt < 85.5f && 
    terminalCurr > 0.0f && terminalCurr < 300.0f)
{
    unsigned long now = millis();
    float dt_hours = (now - lastEnergyTime) / 3600000.0f;
    float energyDelta = terminalVolt * terminalCurr * dt_hours;
    
    // Only add positive energy increments with sanity check
    if (energyDelta > 0.0f && energyDelta < 1000.0f) {
        energyWh += energyDelta;
    }
    lastEnergyTime = now;
}
```

### Fix 4: Enhanced State Monitoring (ocpp_manager.cpp)
Added detailed logging for:
- Gun physical connection state changes
- Battery connection state changes
- Combined plug state (gun AND battery)
- Terminal voltage changes
- EV ready state changes

This helps identify exactly why MicroOcpp auto-stops transactions.

## Expected Behavior After Fix

### Normal Transaction Flow:
1. **RemoteStartTransaction** received → Accepted
2. **StartTransaction** sent with `meterStart: 0` (or valid value)
3. Status: Available → Charging
4. **MeterValues** sent every 10s with valid voltage, current, energy
5. Transaction continues until:
   - RemoteStopTransaction received, OR
   - Plug disconnected, OR
   - Charger module goes offline
6. **StopTransaction** sent with `meterStop: <valid_value>`
7. Status: Charging → Available

### What to Monitor:
```
[OCPP] 📥 RemoteStart received
[OCPP] ▶️  Transaction started - Charging enabled
[Metrics] Energy=0.00Wh (meter=0)
[Metrics] Energy=12.50Wh (meter=12)
[Metrics] Energy=25.30Wh (meter=25)
... charging continues ...
[OCPP] ⏹️  Transaction stopped
```

### Red Flags (Indicates Problem):
```
[OCPP]   ⚡ Plug state: DISCONNECTED (gun=0, battery=1)
[OCPP]   ⚡ EV ready: NO (battery=1, V=45.2V)
[OCPP]   EVSE ready: NO
[Charger] Module=OFFLINE
```

## Testing Checklist

- [ ] Verify `meterStart` is 0 or positive (not -1)
- [ ] Verify `meterStop` is >= `meterStart` (not -1)
- [ ] Transaction lasts at least 10+ seconds
- [ ] MeterValues show increasing energy
- [ ] Check serial logs for state change warnings
- [ ] Test with vehicle plugged in entire session
- [ ] Test with charger module online entire session

## Additional Recommendations

### 1. Add Transaction Minimum Duration
Prevent transactions shorter than 5 seconds (likely errors):
```cpp
if (notification == TxNotification_StopTx) {
    float duration = (millis() - txStartTime) / 1000.0f;
    if (duration < 5.0f) {
        Serial.printf("[OCPP] ⚠️  Very short transaction: %.1fs\n", duration);
    }
    // ... rest of handler
}
```

### 2. Monitor CAN Bus Health
Ensure terminal voltage/current messages are received regularly:
```cpp
if (millis() - lastTerminalPower > 5000) {
    Serial.println("[OCPP] ⚠️  No terminal data for 5s - may cause transaction stop");
}
```

### 3. Add Transaction State Persistence
If ESP32 reboots during charging, resume transaction:
- Already implemented in `ocpp_state_machine.cpp`
- Verify it's working by checking NVS after reboot

## Debugging Commands

### Check Current State:
```
[Status] TX=ACTIVE/RUNNING | Current=FLOWING | OCPP=PERMITS
[Metrics] Energy=45.20Wh (meter=45)
```

### Force Stop (if stuck):
Press `t` in serial console to manually stop charging

### Check Energy Meter:
Look for `(meter=X)` in debug output - should match `Energy=X.XXWh`

## Related Files Modified
- `src/modules/ocpp_manager.cpp` - Transaction lifecycle, energy meter, state logging
- `src/main.cpp` - Energy accumulation sanity checks, debug output
- `src/core/globals.cpp` - Energy variable initialization (already correct)

## Version
- Fixed: 2025-01-22
- Firmware: v2.0+
- MicroOcpp: Latest from lib/

---
**Status**: ✅ Ready for Testing
