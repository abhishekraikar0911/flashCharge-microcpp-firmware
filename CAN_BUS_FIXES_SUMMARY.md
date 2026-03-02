# CAN Bus Stability Fixes - Quick Reference

## 🎯 Problem Summary
- CAN BUS-OFF events every 2-5 minutes
- Stuck transactions (1057, 1058) on server
- Premature EV disconnect during CAN recovery
- RemoteStart rejected (state machine issues)

---

## ✅ Firmware Fixes (COMPLETED)

### 1. Increased CAN Recovery Tolerance
**File**: `src/drivers/charger_interface.cpp`
```cpp
const unsigned long CHARGER_TIMEOUT_MS = 5000; // Was 3000ms
```

### 2. Disable Voltage-Drop Disconnect During CAN Recovery
**Files**: `header.h`, `globals.cpp`, `charger_interface.cpp`, `main.cpp`
```cpp
extern bool canRecoveryActive;  // Global flag

// In charger_interface.cpp:
if (s.state == TWAI_STATE_BUS_OFF) {
    canRecoveryActive = true;  // Disable voltage-drop disconnect
}

// In main.cpp:
if (terminalVolt > 10.0f && !canRecoveryActive) {
    // Voltage drop detection (disabled during recovery)
}
```

### 3. StopTransaction Retry
Already handled by MicroOcpp library (3 attempts)

---

## 🔧 Hardware Fixes (TODO)

### Priority 1: Add Termination Resistors (30 min)
```
CANH ----[120Ω]---- CANL  (at ESP32 end)
CANH ----[120Ω]---- CANL  (at Charger end)
```
**Verify**: Measure ~60Ω between CANH and CANL

### Priority 2: Replace CAN Wiring (1 hour)
- Use shielded twisted-pair cable (CAT5e or CAN-specific)
- Route away from 30A power cables (minimum 10cm separation)
- Keep cable < 3 meters
- Connect shield to ground at ONE end only

### Priority 3: Improve Grounding (30 min)
- Star grounding topology (all grounds to single point)
- Verify ground voltage < 0.1V between ESP32 and charger
- No ground loops

### Priority 4: Add Ferrite Beads (15 min)
- Install on CANH and CANL near ESP32
- 100-300Ω @ 100MHz impedance

---

## 🖥️ Server-Side Fixes (IMMEDIATE)

### Close Stuck Transactions
```bash
docker exec -it csms-postgres psql -U citrine -d citrine -c \
"UPDATE \"Transactions\" 
 SET \"isActive\" = false, 
     \"stopTime\" = NOW(), 
     \"stopReason\" = 'Other'
 WHERE \"transactionId\" IN (1057, 1058);"
```

### Add Transaction Timeout (Recommended)
```sql
-- Run as cron job every hour
UPDATE "Transactions" 
SET "isActive" = false, 
    "stopTime" = NOW(), 
    "stopReason" = 'Timeout'
WHERE "isActive" = true 
  AND "startTime" < NOW() - INTERVAL '24 hours';
```

---

## 📦 Build & Flash

```bash
# Clean build
pio run -e charger_esp32_production --target clean

# Build with fixes
pio run -e charger_esp32_production

# Flash to ESP32
pio run -e charger_esp32_production --target upload

# Monitor
pio device monitor --baud 115200
```

---

## 🔍 Expected Results

### Before Fixes:
- ❌ CAN BUS-OFF every 2-5 minutes
- ❌ Stuck transactions
- ❌ Premature EV disconnect
- ❌ RemoteStart rejected

### After Firmware Fixes:
- ✅ CAN recovery tolerance increased
- ✅ Voltage-drop disconnect disabled during recovery
- ⚠️ Still may see BUS-OFF (hardware fixes needed)

### After Hardware Fixes:
- ✅ No CAN BUS-OFF events
- ✅ Stable charging for full session
- ✅ Transactions complete successfully
- ✅ RemoteStart/Stop working reliably

---

## 📊 Testing Checklist

### Firmware Testing:
- [ ] Firmware builds without errors
- [ ] ESP32 boots successfully
- [ ] CAN recovery flag works (check logs)
- [ ] No premature EV disconnects during recovery
- [ ] Transactions complete successfully

### Hardware Testing:
- [ ] Termination resistors installed (measure 60Ω)
- [ ] Shielded cable installed
- [ ] Ground voltage < 0.1V
- [ ] Ferrite beads installed
- [ ] No CAN BUS-OFF for 30+ minutes
- [ ] Charging stable for full session

### Server Testing:
- [ ] Stuck transactions closed
- [ ] New transactions complete successfully
- [ ] Server shows `isActive=false` after stop
- [ ] No transaction timeout errors

---

## 🎯 Implementation Priority

1. **NOW**: Close stuck transactions on server (5 min)
2. **TODAY**: Flash firmware with fixes (15 min)
3. **TODAY**: Add termination resistors (30 min)
4. **THIS WEEK**: Replace CAN wiring (1 hour)
5. **THIS WEEK**: Improve grounding (30 min)
6. **OPTIONAL**: Add ferrite beads (15 min)

**Total Time**: ~3 hours (excluding server fixes)

---

## 📞 Support

- **Firmware Issues**: Check `FIRMWARE_FIXES_IMPLEMENTED.md`
- **Hardware Issues**: Check `HARDWARE_FIXES_GUIDE.md`
- **Server Issues**: Contact server admin
- **General Support**: support@rivotmotors.com

---

## 📅 Status

**Date**: January 2025  
**Firmware Version**: v2.5.0  
**Status**: 
- ✅ Firmware fixes COMPLETED
- ⏳ Hardware fixes PENDING
- ⏳ Server fixes PENDING

---

## 🔗 Related Documents

- [FIRMWARE_FIXES_IMPLEMENTED.md](FIRMWARE_FIXES_IMPLEMENTED.md) - Detailed firmware changes
- [HARDWARE_FIXES_GUIDE.md](HARDWARE_FIXES_GUIDE.md) - Step-by-step hardware guide
- [README.md](README.md) - Project overview and troubleshooting
- [docs/troubleshooting/](docs/troubleshooting/) - Common issues and fixes
