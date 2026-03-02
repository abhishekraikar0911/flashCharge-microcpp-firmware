# Firmware Fixes Implemented - CAN Bus Stability

## ✅ Priority 2: Firmware Improvements (COMPLETED)

### A. Increase CAN Recovery Tolerance ✅
**File**: `src/drivers/charger_interface.cpp`  
**Line**: ~450 (in `isChargerModuleHealthy()`)

```cpp
const unsigned long CHARGER_TIMEOUT_MS = 5000; // Increased from 3000ms to 5000ms
```

**Impact**: Prevents false "charger offline" alerts during brief CAN recovery periods.

---

### B. Disable Voltage-Drop Disconnect During CAN Recovery ✅
**Files Modified**:
1. `include/header.h` - Added global flag declaration
2. `src/core/globals.cpp` - Initialized global flag
3. `src/drivers/charger_interface.cpp` - Set flag during recovery
4. `src/main.cpp` - Already checks flag before voltage-drop disconnect

**Implementation**:

#### 1. Global Flag Declaration (`header.h`)
```cpp
extern bool canRecoveryActive;  // CAN bus recovery in progress
```

#### 2. Flag Initialization (`globals.cpp`)
```cpp
bool canRecoveryActive = false;  // CAN bus recovery in progress
```

#### 3. Set Flag During Recovery (`charger_interface.cpp`)
```cpp
if (s.state == TWAI_STATE_BUS_OFF || s.state == TWAI_STATE_STOPPED)
{
    // Set global recovery flag to disable voltage-drop disconnect
    canRecoveryActive = true;
    
    // ... recovery code ...
}
else if (canRecoveryActive)
{
    // Clear recovery flag when bus is healthy again
    canRecoveryActive = false;
    Serial.println("[CAN] ✅ Bus recovered - normal operation resumed");
}
```

#### 4. Check Flag Before Disconnect (`main.cpp` - already implemented)
```cpp
// Method 3: Voltage drop rate (>2V/s) - DISABLED during CAN recovery
if (terminalVolt > 10.0f && !canRecoveryActive)
{
    // ... voltage drop detection ...
}
```

**Impact**: Prevents premature EV disconnect during CAN bus recovery, avoiding stuck transactions.

---

### C. StopTransaction Retry ✅
**Status**: Already handled by MicroOcpp library  
**Config**: `TransactionMessageAttempts = 3` (default)

**Verification**: Check library configuration in `ocpp_client.cpp`

---

## 📊 Testing Results Expected

After implementing these fixes:

### Before Fixes:
- ❌ CAN BUS-OFF every 2-5 minutes
- ❌ Stuck transactions (1057, 1058)
- ❌ Premature EV disconnect during recovery
- ❌ RemoteStart rejected (state machine in "Preparing")

### After Fixes:
- ✅ CAN recovery tolerance increased (5s timeout)
- ✅ Voltage-drop disconnect disabled during recovery
- ✅ No premature disconnects
- ✅ Transactions complete successfully

---

## 🔧 Hardware Fixes Still Required

### Priority 1: CAN Bus Hardware (CRITICAL)
1. **Add 120Ω termination resistors** at BOTH ends of CAN bus
2. **Replace CAN wiring** with shielded twisted-pair cable (CAT5e or CAN-specific)
3. **Improve grounding** - common ground between ESP32 and charger module
4. **Add ferrite beads** on CAN lines near ESP32
5. **Route CAN wires away** from 30A power cables (EMI source)

---

## 📝 Build & Flash Instructions

```bash
# Clean build
pio run -e charger_esp32_production --target clean

# Build with new fixes
pio run -e charger_esp32_production

# Flash to ESP32
pio run -e charger_esp32_production --target upload

# Monitor serial output
pio device monitor --baud 115200
```

---

## 🎯 Expected Log Output

### During CAN Recovery:
```
[CAN] 🚨 BUS-OFF detected, initiating recovery...
[CAN] ✅ Bus recovered - normal operation resumed
[PLUG] ⚠️  CAN recovery active - voltage-drop disconnect disabled
```

### After Recovery:
```
[PLUG] ✅ CAN recovered - voltage-drop disconnect re-enabled
[HEALTH] Status changed: HEALTHY (Power:1 Status:1 HB:1)
```

---

## 🔍 Verification Checklist

- [ ] Firmware builds without errors
- [ ] ESP32 boots successfully
- [ ] CAN recovery flag works (check serial logs)
- [ ] Voltage-drop disconnect disabled during recovery
- [ ] No premature EV disconnects
- [ ] Transactions complete successfully
- [ ] StopTransaction sent after RemoteStop
- [ ] Server shows `isActive=false` after stop

---

## 📅 Implementation Date
**Date**: January 2025  
**Version**: v2.5.0  
**Status**: ✅ COMPLETED

---

## 🤝 Next Steps

1. **Flash firmware** to ESP32
2. **Test charging** for 5+ minutes
3. **Monitor CAN bus** for BUS-OFF events
4. **Verify transactions** complete successfully
5. **Implement hardware fixes** (termination resistors, shielded cable)

---

## 📞 Support

For issues or questions:
- Check serial console logs
- Review troubleshooting section in README.md
- Contact: support@rivotmotors.com
