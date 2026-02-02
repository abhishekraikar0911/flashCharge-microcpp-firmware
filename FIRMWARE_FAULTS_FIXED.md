# Firmware Faults - FIXED

## Summary
All 5 critical firmware faults have been fixed to ensure OCPP-compliant operation.

---

## ✅ Fix 1: Transaction Lock Implemented

### Problem
- `chargingEnabled` could be set without valid OCPP transaction
- Hardware could charge without OCPP authorization

### Solution
Added transaction lock mechanism with validation:

**Location**: `src/modules/ocpp_manager.cpp` lines 33-35

```cpp
// Transaction tracking and lock
static unsigned long txStartTime = 0;
static bool transactionLocked = false;
static int activeTransactionId = -1;
```

**Key Changes**:
- `transactionLocked` flag prevents charging without valid transaction
- `activeTransactionId` stores the OCPP transaction ID
- Charging only enabled when transaction is locked AND has valid ID

---

## ✅ Fix 2: Transaction ID Validation Before Charging

### Problem
- `chargingEnabled = true` happened before transaction validation
- Charging could start even if transaction failed to initialize

### Solution
Validate transaction ID before enabling charging:

**Location**: `src/modules/ocpp_manager.cpp` lines 177-197

```cpp
} else if (notification == TxNotification_StartTx) {
    if (!isChargerModuleHealthy()) {
        Serial.println("[OCPP] ❌ Transaction started but charger OFFLINE - not enabling charging");
        return;
    }
    // CRITICAL: Validate transaction ID before enabling charging
    if (tx && tx->getTransactionId() > 0) {
        activeTransactionId = tx->getTransactionId();
        transactionLocked = true;
        chargingEnabled = true;
        txStartTime = millis();
        sessionSummarySent = false;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            energyWh = 0.0f;
            xSemaphoreGive(dataMutex);
        }
        Serial.printf("[OCPP] ▶️  Transaction %d LOCKED - Charging enabled\n\n", activeTransactionId);
    } else {
        Serial.println("[OCPP] ❌ Transaction invalid (no ID) - NOT enabling charging\n");
    }
}
```

**Key Changes**:
- Check `tx->getTransactionId() > 0` before enabling charging
- Lock transaction with valid ID
- Log transaction ID for debugging
- Reject charging if transaction ID is invalid

---

## ✅ Fix 3: OCPP State Controls Hardware (Not Vice Versa)

### Problem
- Hardware signals (`gunPhysicallyConnected`, `batteryConnected`) directly controlled state
- OCPP state machine was bypassed by hardware

### Solution
**Architectural Change**: Hardware signals now only UPDATE OCPP inputs, OCPP transaction callbacks are the ONLY place that sets `chargingEnabled`.

**Key Principle**:
```
Hardware Signals → OCPP Inputs → OCPP State Machine → chargingEnabled
(READ ONLY)        (UPDATE)      (CONTROL)            (WRITE ONLY by OCPP)
```

**Implementation**:
- Hardware signals update `gunPhysicallyConnected` and `batteryConnected` (read-only for OCPP)
- OCPP callbacks (`setConnectorPluggedInput`, `setEvReadyInput`) read these signals
- ONLY OCPP transaction callbacks (`TxNotification_StartTx`, `TxNotification_StopTx`) modify `chargingEnabled`
- Hardware NEVER directly sets `chargingEnabled`

---

## ✅ Fix 4: StopTransaction Only If Transaction Valid

### Problem
- Plug disconnect triggered StopTransaction without checking transaction validity
- Spurious StopTransaction messages sent to server

### Solution
Check if transaction is running before stopping:

**Location**: `src/main.cpp` lines 279-288

```cpp
// Execute disconnect
if (shouldDisconnect && (gunPhysicallyConnected || batteryConnected))
{
    gunPhysicallyConnected = false;
    batteryConnected = false;
    zeroCurrentStart = 0;
    Serial.println("[PLUG] ✅ Status: DISCONNECTED");
    
    // CRITICAL: Only stop transaction if one is actually running
    if (isTransactionRunning(1)) {
        Serial.println("[PLUG] 🛑 Stopping transaction due to EV disconnect");
        endTransaction(nullptr, "EVDisconnected");
    }
}
```

**Key Changes**:
- Added `isTransactionRunning(1)` check before calling `endTransaction()`
- Only send StopTransaction if transaction is actually active
- Prevents spurious OCPP messages

---

## ✅ Fix 5: Prevent State Reset While EV Connected

### Problem
- State could reset to Available while EV still physically connected
- No validation that EV was disconnected before state transition

### Solution
Check plug connection before resetting to Available:

**Location**: `src/modules/ocpp_state_machine.cpp` lines 83-99

```cpp
// Finishing state timeout (10 seconds max) - ONLY reset if EV disconnected
if (currentState == ConnectorState::Finishing && stateAge > FINISHING_TIMEOUT_MS)
{
    // CRITICAL: Only reset to Available if EV is actually disconnected
    if (!isPlugConnected())
    {
        Serial.printf("[OCPP_SM] ⏱️  Finishing timeout (%.0f sec) - EV disconnected, transitioning Available\n",
                      FINISHING_TIMEOUT_MS / 1000.0f);
        forceState(ConnectorState::Available);
        g_persistence.clearTransaction();
        g_healthMonitor.onTransactionEnded();
    }
    else
    {
        Serial.printf("[OCPP_SM] ⚠️  Finishing timeout but EV still connected - keeping Finishing state\n");
    }
}
```

**Key Changes**:
- Added `!isPlugConnected()` check before transitioning to Available
- State remains in Finishing if EV still connected
- Prevents premature state reset

---

## Transaction Flow (After Fixes)

### Correct Flow:
```
1. EV Plugged → gunPhysicallyConnected = true (hardware signal)
2. OCPP reads plug state via setConnectorPluggedInput()
3. OCPP state: Available → Preparing
4. RemoteStart received from server
5. OCPP validates: charger healthy, BMS safe, plug connected
6. OCPP sends StartTransaction
7. Server responds with transactionId
8. TxNotification_StartTx callback:
   - Validate tx->getTransactionId() > 0
   - Set activeTransactionId
   - Set transactionLocked = true
   - Set chargingEnabled = true ✅ (ONLY place this happens)
9. Charging starts (hardware controlled by chargingEnabled)
10. EV Unplugged → gunPhysicallyConnected = false (hardware signal)
11. Check isTransactionRunning(1) → true
12. Call endTransaction("EVDisconnected")
13. TxNotification_StopTx callback:
    - Set transactionLocked = false
    - Set activeTransactionId = -1
    - Set chargingEnabled = false ✅ (ONLY place this happens)
14. State: Charging → Finishing
15. Check !isPlugConnected() → true
16. State: Finishing → Available ✅
```

---

## Testing Checklist

### Test 1: Transaction Lock
- [ ] Verify `chargingEnabled` only set when `transactionLocked = true`
- [ ] Verify `activeTransactionId` stored correctly
- [ ] Verify charging rejected if transaction ID invalid

### Test 2: Transaction ID Validation
- [ ] Start transaction with valid ID → charging enabled
- [ ] Start transaction with invalid ID → charging rejected
- [ ] Verify transaction ID logged in serial output

### Test 3: OCPP Controls Hardware
- [ ] Verify hardware signals don't directly set `chargingEnabled`
- [ ] Verify only OCPP callbacks modify `chargingEnabled`
- [ ] Verify hardware signals only update OCPP inputs

### Test 4: Valid Transaction Check
- [ ] Unplug EV without transaction → no StopTransaction sent
- [ ] Unplug EV with transaction → StopTransaction sent
- [ ] Verify `isTransactionRunning()` check works

### Test 5: State Reset Prevention
- [ ] Finishing timeout with EV connected → stays in Finishing
- [ ] Finishing timeout with EV disconnected → transitions to Available
- [ ] Verify `isPlugConnected()` check works

---

## Files Modified

1. **src/modules/ocpp_manager.cpp**
   - Added transaction lock mechanism
   - Added transaction ID validation
   - Added transaction unlock on stop

2. **src/main.cpp**
   - Added transaction validity check before StopTransaction

3. **src/modules/ocpp_state_machine.cpp**
   - Added plug connection check before state reset

---

## Behavioral Changes

### Before Fixes:
- ❌ Charging could start without valid transaction
- ❌ Hardware signals directly controlled charging
- ❌ StopTransaction sent even without active transaction
- ❌ State reset to Available while EV still connected

### After Fixes:
- ✅ Charging ONLY starts with valid, locked transaction
- ✅ OCPP state machine controls charging, hardware provides signals
- ✅ StopTransaction ONLY sent if transaction is running
- ✅ State ONLY resets to Available when EV disconnected

---

## OCPP Compliance

These fixes ensure compliance with OCPP 1.6 specification:

1. **Transaction Integrity**: Charging only occurs within valid OCPP transaction
2. **State Machine**: OCPP state controls hardware, not vice versa
3. **Message Validity**: StopTransaction only sent for valid transactions
4. **State Consistency**: Connector state matches physical reality

---

**Status**: ✅ All firmware faults fixed
**Date**: January 2025
**Firmware Version**: v2.4.1+transaction-lock
