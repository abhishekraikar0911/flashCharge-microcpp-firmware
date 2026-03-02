# CRITICAL FIX: Charger Fault at Startup

## 🚨 Problem Description

**Symptom**: Charger module goes into "Faulted" state immediately after MCU power-on or reset, rejecting RemoteStart commands.

**Server Feedback**:
```
StatusNotification: Faulted / OtherError at 04:42 & 04:47 UTC
A faulted charger always rejects RemoteStart
```

**Root Cause**: Charger module expects BOTH CAN IDs (Group 1 and Group 2) to send initialization messages at startup. The firmware was only sending Group 1 immediately, and Group 2 was conditional on `gunPhysicallyConnected`, causing the charger to timeout and enter fault state.

---

## 🔍 Analysis

### Original Code Behavior:
```cpp
// Group 1 (0x068181FE) - sent immediately ✅
sendGroupRequest(groups[0]);

// Group 2 (0x068182FE) - only sent if gun connected ❌
if (gunPhysicallyConnected) {
    sendGroupRequest(groups[1]);
}
```

### Why This Caused Faults:

1. **At startup**: `gunPhysicallyConnected = false`
2. **Group 1 sends**: 0x068181FE → Charger receives ✅
3. **Group 2 blocked**: 0x068182FE → Charger never receives ❌
4. **Charger timeout**: Expects both IDs, enters fault state after ~3 seconds
5. **OCPP reports**: `StatusNotification: Faulted`
6. **RemoteStart rejected**: Faulted charger cannot start

### CANalyzer Evidence:
- Only Group 1 (0x068181FE) visible at startup
- Group 2 (0x068182FE) missing until gun physically connected
- Charger module sends fault indication after timeout

---

## ✅ Solution Implemented

### 1. Startup Initialization Sequence

Added explicit initialization at task start:

```cpp
void chargerCommTask(void *arg)
{
    static bool startupInitComplete = false;

    // CRITICAL: Send initial messages to both groups at startup
    Serial.println("[CHARGER] Sending startup initialization sequence...");
    vTaskDelay(pdMS_TO_TICKS(500)); // Wait for CAN bus to stabilize
    
    // Send Group 1 (Control) - all 3 functions
    for (int i = 0; i < 3; i++) {
        sendGroupRequest(groups[0]);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Send Group 2 (Telemetry) - all 5 functions
    for (int i = 0; i < 5; i++) {
        sendGroupRequest(groups[1]);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    Serial.println("[CHARGER] ✅ Startup initialization complete");
    startupInitComplete = true;
```

**Timing**:
- 500ms delay for CAN bus stabilization
- Group 1: 3 functions × 100ms = 300ms
- Group 2: 5 functions × 100ms = 500ms
- **Total**: ~1.3 seconds initialization time

### 2. Continuous Communication (Both Groups)

Removed conditional check for Group 2:

```cpp
// OLD (WRONG):
if (gunPhysicallyConnected) {
    sendGroupRequest(groups[1]);
}

// NEW (CORRECT):
sendGroupRequest(groups[1]);  // Always send, regardless of gun state
```

**Why**: Charger module needs continuous heartbeat from both CAN IDs to stay healthy.

### 3. Re-initialization After CAN Recovery

Added re-init sequence after BUS-OFF recovery:

```cpp
if (!startupInitComplete) {
    Serial.println("[CHARGER] Re-sending initialization sequence after recovery...");
    // Send full init sequence again
    startupInitComplete = true;
}
```

---

## 📊 Expected Results

### Before Fix:
```
[CHARGER] TX: 0x068181FE (Group 1) ✅
[CHARGER] TX: 0x068182FE (Group 2) ❌ MISSING
[CHARGER] ⏱️  Timeout after 3 seconds
[OCPP] StatusNotification: Faulted
[OCPP] RemoteStart: REJECTED
```

### After Fix:
```
[CHARGER] Sending startup initialization sequence...
[CHARGER] TX: 0x068181FE Func=0x32 (Status)
[CHARGER] TX: 0x068181FE Func=0x00 (Vmax)
[CHARGER] TX: 0x068181FE Func=0x03 (Imax)
[CHARGER] TX: 0x068182FE Func=0x84 (Battery V)
[CHARGER] TX: 0x068182FE Func=0x82 (Current)
[CHARGER] TX: 0x068182FE Func=0x79 (Metric79)
[CHARGER] TX: 0x068182FE Func=0x80 (Temp)
[CHARGER] TX: 0x068182FE Func=0x83 (Metric83)
[CHARGER] ✅ Startup initialization complete
[OCPP] StatusNotification: Available
[OCPP] RemoteStart: ACCEPTED ✅
```

---

## 🧪 Testing Checklist

### Test 1: Cold Boot
- [ ] Power cycle ESP32
- [ ] Monitor serial output
- [ ] Verify both Group 1 and Group 2 sent at startup
- [ ] Check OCPP status: Should be "Available" (not "Faulted")
- [ ] Send RemoteStart: Should be accepted

### Test 2: Reset Button
- [ ] Press ESP32 reset button
- [ ] Verify initialization sequence
- [ ] Check OCPP status: "Available"
- [ ] Send RemoteStart: Accepted

### Test 3: CAN Recovery
- [ ] Trigger CAN BUS-OFF (disconnect wire briefly)
- [ ] Verify recovery sequence
- [ ] Check re-initialization sent
- [ ] Verify charger stays healthy

### Test 4: CANalyzer Verification
- [ ] Connect CANalyzer to CAN bus
- [ ] Power on ESP32
- [ ] Verify both 0x068181FE and 0x068182FE visible immediately
- [ ] Check message timing: ~100ms intervals during init
- [ ] Verify continuous messages after init

---

## 📝 Code Changes Summary

**File**: `src/drivers/charger_interface.cpp`

**Changes**:
1. Added `startupInitComplete` flag
2. Added startup initialization sequence (8 messages total)
3. Removed `if (gunPhysicallyConnected)` condition for Group 2
4. Added re-initialization after CAN recovery
5. Reduced health timeout from 12s to 5s (no longer needed)

**Lines Changed**: ~50 lines
**Impact**: CRITICAL - Fixes charger fault at startup

---

## 🎯 Root Cause Summary

| Issue | Cause | Fix |
|-------|-------|-----|
| Charger Faulted at startup | Group 2 not sent (conditional on gun) | Send both groups at startup |
| RemoteStart rejected | Faulted charger blocks commands | Initialize charger properly |
| Intermittent faults | Group 2 stops when gun disconnected | Always send both groups |
| Fault after CAN recovery | No re-initialization | Re-send init sequence |

---

## ⚠️ Important Notes

1. **Both CAN IDs Required**: Charger module firmware expects both 0x068181FE and 0x068182FE to be active at all times
2. **Initialization Timing**: 500ms delay before first message is critical for CAN bus stabilization
3. **Continuous Communication**: Both groups must send continuously, not just at startup
4. **Recovery Handling**: After CAN BUS-OFF, full re-initialization is required

---

## 🚀 Deployment

### Build & Flash:
```bash
pio run -e charger_esp32_production --target clean
pio run -e charger_esp32_production
pio run -e charger_esp32_production --target upload
pio device monitor --baud 115200
```

### Expected Boot Log:
```
[System] 🚌 Initializing dual CAN buses...
[CHARGER] Sending startup initialization sequence...
[CHARGER] TX: 0x068181FE Func=0x32 ...
[CHARGER] TX: 0x068181FE Func=0x00 ...
[CHARGER] TX: 0x068181FE Func=0x03 ...
[CHARGER] TX: 0x068182FE Func=0x84 ...
[CHARGER] TX: 0x068182FE Func=0x82 ...
[CHARGER] TX: 0x068182FE Func=0x79 ...
[CHARGER] TX: 0x068182FE Func=0x80 ...
[CHARGER] TX: 0x068182FE Func=0x83 ...
[CHARGER] ✅ Startup initialization complete
[OCPP] StatusNotification: Available
```

---

## 📞 Support

If charger still shows "Faulted" after this fix:
1. Check CAN bus wiring (CANH, CANL, GND)
2. Verify termination resistors (120Ω at both ends)
3. Check charger module power supply
4. Review CANalyzer logs for missing messages
5. Contact: support@rivotmotors.com

---

## 📅 Version

**Fix Version**: v2.5.1  
**Date**: January 2025  
**Priority**: CRITICAL  
**Status**: ✅ IMPLEMENTED

---

**End of Document**
