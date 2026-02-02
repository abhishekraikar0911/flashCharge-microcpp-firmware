# Hybrid Plug Detection - Implementation Plan

## Current Status Analysis

From your serial monitor output:
```
[Status] State: Available
[Metrics] V=0.0V I=0.0A SOC=98.2%
[Charger] Module=OFFLINE
```

**Problem**: Charger module is OFFLINE, so voltage-based detection won't work. Need hybrid approach.

## Implementation Plan - Option 4 (Hybrid)

### Step 1: Add Plug Disconnect Detection Task
**File**: `src/main.cpp`

**What to add**: New monitoring task in `loop()` that checks 3 conditions every 500ms:

```cpp
// Hybrid plug disconnect detection (runs every 500ms)
static unsigned long lastPlugCheck = 0;

if (millis() - lastPlugCheck >= 500) {
    bool shouldDisconnect = false;
    
    // Method 1: BMS timeout (3 seconds)
    if (batteryConnected && (millis() - lastBMS > 3000)) {
        Serial.println("[PLUG] 🔌 Disconnected: BMS timeout");
        shouldDisconnect = true;
    }
    
    // Method 2: Zero current with voltage present (5 seconds)
    static unsigned long zeroCurrentStart = 0;
    if (terminalVolt > 56.0f && terminalCurr < 0.5f) {
        if (zeroCurrentStart == 0) {
            zeroCurrentStart = millis();
        } else if (millis() - zeroCurrentStart > 5000) {
            Serial.println("[PLUG] 🔌 Disconnected: Zero current timeout");
            shouldDisconnect = true;
        }
    } else {
        zeroCurrentStart = 0;
    }
    
    // Method 3: Voltage drop rate (>2V/s for 3 seconds)
    static float lastVoltageCheck = 0.0f;
    static unsigned long lastVoltageTime = 0;
    if (lastVoltageTime > 0) {
        float deltaV = lastVoltageCheck - terminalVolt;
        float deltaT = (millis() - lastVoltageTime) / 1000.0f;
        if (deltaT > 0 && (deltaV / deltaT) > 2.0f) {
            Serial.println("[PLUG] 🔌 Disconnected: Fast voltage drop");
            shouldDisconnect = true;
        }
    }
    lastVoltageCheck = terminalVolt;
    lastVoltageTime = millis();
    
    // Execute disconnect
    if (shouldDisconnect) {
        gunPhysicallyConnected = false;
        batteryConnected = false;
        Serial.println("[PLUG] ✅ Plug detection: DISCONNECTED");
    }
    
    lastPlugCheck = millis();
}
```

### Step 2: Update Plug Detection in OCPP Manager
**File**: `src/modules/ocpp_manager.cpp`

**Current code**:
```cpp
setConnectorPluggedInput([]() {
    bool plugged = gunPhysicallyConnected && batteryConnected;
    return plugged;
});
```

**No change needed** - This already uses both flags correctly.

### Step 3: Ensure Finishing → Available Transition
**File**: `src/modules/ocpp_state_machine.cpp`

**Already done**: Timeout is 10 seconds (you already changed this).

**Verify in poll() function**:
```cpp
// Finishing state timeout (10 seconds max)
if (currentState == ConnectorState::Finishing && stateAge > FINISHING_TIMEOUT_MS) {
    Serial.printf("[OCPP_SM] ⏱️ Finishing timeout (10 sec) - forcing Available\n");
    forceState(ConnectorState::Available);
    g_persistence.clearTransaction();
    g_healthMonitor.onTransactionEnded();
}
```

### Step 4: Add Plug Connection Detection (Available → Preparing)
**File**: `src/modules/ocpp_state_machine.cpp`

**What to add in poll() function**:
```cpp
// Auto-transition Available → Preparing when plug connected
if (currentState == ConnectorState::Available && isPlugConnected()) {
    static bool lastPlugConnected = false;
    bool currentPlugConnected = isPlugConnected();
    
    if (!lastPlugConnected && currentPlugConnected) {
        Serial.println("[OCPP_SM] 🔌 Plug connected, transitioning to Preparing");
        forceState(ConnectorState::Preparing);
    }
    
    lastPlugConnected = currentPlugConnected;
}
```

## Expected State Flow

### Scenario: Customer 1 → Customer 2

1. **Customer 1 charging**: `Charging` state
2. **Customer 1 stops**: `Charging` → `Finishing` (transaction ends)
3. **Customer 1 unplugs**: 
   - Hybrid detection triggers (3-5 seconds)
   - `gunPhysicallyConnected = false`
4. **After 10 seconds**: `Finishing` → `Available` (timeout)
5. **Customer 2 plugs in**:
   - `gunPhysicallyConnected = true`
   - `Available` → `Preparing` (auto-transition)
6. **Customer 2 starts charging**: `Preparing` → `Charging` (RemoteStart)
7. **Repeat cycle**

## Files to Modify

1. ✅ **include/config/hardware.h** - Already added constants
2. ✅ **include/ocpp_state_machine.h** - Already changed timeout to 10s
3. ⏳ **src/main.cpp** - Add hybrid plug detection logic
4. ⏳ **src/modules/ocpp_state_machine.cpp** - Add Available→Preparing transition
5. ✅ **src/drivers/charger_interface.cpp** - Already fixed current scaling

## Testing Checklist

After implementation, test these scenarios:

- [ ] Unplug during charging → Detects within 5 seconds
- [ ] Unplug after charging → Finishing→Available within 10 seconds
- [ ] Plug in when Available → Transitions to Preparing immediately
- [ ] RemoteStart when Preparing → Starts charging
- [ ] Stop charging → Finishing state
- [ ] Unplug + Replug quickly → Next customer can charge within 15 seconds total

## Summary of Changes Needed

**Total changes**: 2 files

1. **src/main.cpp**: Add 30 lines for hybrid plug detection
2. **src/modules/ocpp_state_machine.cpp**: Add 10 lines for Available→Preparing transition

**Result**: 
- Plug disconnect detection: 3-5 seconds (instead of 60+ seconds)
- Finishing → Available: 10 seconds (already done)
- Available → Preparing: Immediate when plug detected
- Total time between customers: ~15 seconds maximum

---

**Ready to implement?** Reply "yes" to proceed with code changes.
