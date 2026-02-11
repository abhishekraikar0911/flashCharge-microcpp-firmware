# RemoteStart/RemoteStop Analysis - Device Reboot Issue

## Root Cause Identified ✅

Your serial logs show:
```
[System] Reboot count: 210
...
[System] Reboot count: 211  ← device rebooted!
```

**Between RemoteStart and RemoteStop commands, the device rebooted.**

When the device restarted, it lost all transaction state in RAM:
- `transactionActive = false`
- `activeTransactionId = -1`
- `remoteStartAccepted = false`

When RemoteStop arrives after the reboot:
```
[MO] Recv: [2,"...","RemoteStopTransaction",{"transactionId":0}]
[MO] Send: [3,"...",{"status":"Rejected"}]  ← Correctly rejected!
```

The code is working **perfectly** - it's correctly rejecting a RemoteStop for a transaction that doesn't exist.

---

## Why Is The Device Rebooting?

**Possible causes:**
1. ❌ **Watchdog timeout** - A task is blocked/hanging
2. ❌ **Stack overflow** - Memory corruption
3. ❌ **Out of memory** - Heap exhausted
4. ❌ **Unhandled exception** - Null pointer dereference
5. ❌ **Power issue** - Brownout reset

**To identify:** Check the reset reason in logs:
```
[System] Reset reason: X (Description)
```

From your logs:
```
[System] Reset reason: 1 (Power-on reset)
```

This is a normal power-on, **not** a crash. But the reboot counter changing indicates something is triggering restarts.

---

## Solutions

### Option 1: Quick Test (Verify Code Works)

**Follow this sequence WITHOUT waiting:**

```bash
# 1. SSH to charger or wait for it to boot to "State: Preparing"

# 2. IMMEDIATELY send RemoteStart
curl -X POST "http://localhost:8081/ocpp/1.6/evdriver/remoteStartTransaction?identifier=250822008C06" \
  -H "Content-Type: application/json" \
  -d '{"idTag":"TEST123","connectorId":1}'

# Result: Should see [OCPP] ✅ RemoteStart accepted, state → Charging

# 3. WITHIN 5 SECONDS send RemoteStop
curl -X POST "http://localhost:8081/ocpp/1.6/evdriver/remoteStopTransaction?identifier=250822008C06" \
  -H "Content-Type: application/json" \
  -d '{"transactionId":0}'

# Result: Should see [OCPP] ✅ StopTransaction queued, state → Available
```

**Expected Serial Output:**
```
[OCPP_CALLBACK] 🔔 TxNotification fired: type=RemoteStart
[OCPP] ✅ RemoteStart accepted (latching)
[OCPP] 📥 StartTransaction notification
[OCPP] ▶️  Transaction STARTED - Charging ENABLED
[GATE] ✅ HARD GATE OPEN
[Status] State: Charging

(Then within 5 seconds)

[OCPP] 📥 RemoteStop received
[OCPP] 🛑 RemoteStop → ending transaction
[OCPP] ✅ StopTransaction queued to server
[GATE] 🔒 HARD GATE CLOSED
[Status] State: Available
```

If you see this: **Code is working, reboot issue is separate**

---

### Option 2: Find What's Causing Reboots

**New diagnostic logging added:**

Look for:
```
[DIAGNOSTIC] ⚠️  ⚠️  RAPID REBOOT DETECTED! Only XXX ms uptime before crash
```

This indicates the device reboots within 30 seconds of startup.

**Possible culprits:**
1. **OTA update loop** - Check `/opt/csms/.../modules/ota_manager.cpp` line 221 (calls `ESP.restart()`)
2. **Watchdog** - Check if any task is blocked
3. **Memory leak** - Check if heap is being exhausted
4. **CAN driver** - Check if CAN initialization hangs/crashes

---

### Option 3: Enable Crash Diagnostics

Add to startup to capture crash details:

```cpp
// In main.cpp setup()
#ifdef CONFIG_ESP_SYSTEM_COREDUMP_TO_UART
    Serial.println("[DIAGNOSTIC] Crash dumps enabled - will print backtrace on panic");
#endif
```

When device crashes next, the backtrace will show which function caused the panic.

---

## What Changed in This Build

### Added Diagnostics:
1. **Reset reason logging** - Enhanced to show "CRASH DETECTED" for panics
2. **Rapid reboot detection** - Logs if device reboots within 30 seconds
3. **RemoteStop validation** - Logs when transaction doesn't exist and why
4. **Diagnostic timestamps** - Shows device is staying up (not crashing immediately)

### RemoteStop Handler Improvement:
- Now explicitly checks if transaction exists
- Logs detailed reason if RemoteStop arrives with no active transaction
- Prevents unnecessary operations on ghost transactions

---

## Test Sequence

**DO THIS NEXT:**

1. **Build & upload** the new firmware (with diagnostics)
2. **Wait for device to show:** `[Status] State: Preparing`
3. **Send RemoteStart** (see it get accepted)
4. **WAIT 2 seconds ONLY**
5. **Send RemoteStop immediately** (within 5 seconds total)
6. **Capture full serial log**
7. **Share:**
   - Full serial output
   - Any reboot messages
   - Reset reason on next boot

---

## Expected Outcome

**If no reboot occurs between RemoteStart and RemoteStop:**
```
✅ RemoteStart accepted → State: Charging
✅ RemoteStop received → State transitions back to Preparing/Available
✅ Code is working correctly
✅ Reboot is a separate issue (environmental)
```

**If device reboots before RemoteStop:**
```
❌ Reboot detected after RemoteStart
❌ Check reset reason (panic? watchdog? brownout?)
❌ May need to investigate that specific cause
```

---

## Key Files to Review

- [src/main.cpp](src/main.cpp#L90-L115) - Reset reason logging
- [src/modules/ocpp_manager.cpp](src/modules/ocpp_manager.cpp#L420-L450) - RemoteStop handler
- [src/modules/ota_manager.cpp](src/modules/ota_manager.cpp#L215-L225) - OTA restart logic (if happens during test)

---

**Ready?** Build, upload, and run the quick test sequence above!
