# State Stuck in Preparing After StartTransaction - Debug Guide

**Issue:** After RemoteStart + StartTransaction, state shows `Preparing` instead of `Charging`

**Root Cause Hypothesis:** `onTransactionStarted()` is not properly transitioning the state from `Preparing` → `Charging`

---

## What Was Changed (Build #2)

### 1. StartTx Handler Entry Point
Added marker at [ocpp_manager.cpp line 394]:
```cpp
[OCPP] 🎯 *** TxNotification_StartTx CALLBACK FIRED ***
```

### 2. State Transition Logging
- Added to `forceState()` [ocpp_state_machine.cpp line 244]:
  ```cpp
  [OCPP_SM_DIAG] 🔄 forceState() called: Preparing → Charging (check if same)
  [OCPP_SM] 🔄 State: Preparing → Charging ✅ TRANSITION APPLIED
  [OCPP_SM_DIAG] ✅ currentState updated to: 2 (Charging), time=12345
  ```

### 3. onTransactionStarted() Entry Points
Added at [ocpp_state_machine.cpp line 113]:
```cpp
[OCPP_SM_DIAG] 🎯 onTransactionStarted() CALLED with:
[OCPP_SM_DIAG]    connectorId=1, idTag=TEST_TAG, txId=123
[OCPP_SM_DIAG]    Current state BEFORE transition: Preparing
[OCPP_SM_DIAG] 🔄 Calling forceState(Charging)...
[OCPP_SM_DIAG] ✅ After forceState: current state is now Charging
```

### 4. Status Output
Added at [main.cpp line 631]:
```cpp
[DIAGNOSTIC] 🔍 State Machine Status: Charging | TX_Active=1 | TX_Running=1
```

---

## Test Procedure

### Step 1: Baseline
Monitor serial for 15 seconds and look for:
```
[OCPP]   📡 Waiting for RemoteStart command from CSMS...
[OCPP_DIAG] ✅ CallbackActive (remoteStartAccepted=0, txActive=0)
[DIAGNOSTIC] 🔍 State Machine Status: Preparing | TX_Active=0 | TX_Running=0
```

This confirms diagnostics are active.

---

### Step 2: Send RemoteStart from Server

```bash
curl -X POST http://localhost:8000/api/stations/250822008C06/remote-start \
  -H "Content-Type: application/json" \
  -d '{"idTag":"TEST_TAG","connectorId":1}' \
  -v
```

---

### Step 3: Monitor Critical Logs (30 seconds after curl)

#### GOOD PATH - State Should Transition
```
[OCPP_CALLBACK] 🔔 TxNotification fired: type=0 connectorId=1 txId=valid
[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***
[OCPP] 📥 RemoteStart received
[OCPP] ✅ RemoteStart accepted (latching)

[OCPP] 🎯 *** TxNotification_StartTx CALLBACK FIRED ***     ← KEY: Is this here?
[OCPP] 📥 StartTransaction notification
[OCPP]   tx reported id=123

[OCPP_SM_DIAG] 🎯 onTransactionStarted() CALLED with:     ← KEY: Is this called?
[OCPP_SM_DIAG]    connectorId=1, idTag=TEST_TAG, txId=123
[OCPP_SM_DIAG]    Current state BEFORE transition: Preparing
[OCPP_SM_DIAG] 🔄 Calling forceState(Charging)...
[OCPP_SM_DIAG] 🔄 forceState() called: Preparing → Charging (check if same)
[OCPP_SM] 🔄 State: Preparing → Charging ✅ TRANSITION APPLIED     ← KEY: Did it transition?
[OCPP_SM_DIAG] ✅ currentState updated to: 2 (Charging), time=12345
[OCPP_SM_DIAG] ✅ After forceState: current state is now Charging
[OCPP] ▶️  Transaction STARTED - Charging ENABLED (txId=123)
[GATE] ✅ HARD GATE OPEN

[DIAGNOSTIC] 🔍 State Machine Status: Charging | TX_Active=1 | TX_Running=1  ← KEY: Now shows Charging
[Status] Uptime: XXs | State: Charging     ← KEY: Status line shows Charging
```

**If you see all these:** State transition is working. Problem is elsewhere.

---

#### BAD PATH - Possible Issues

**Issue #1: StartTx Handler Not Called**
```
[OCPP] ✅ RemoteStart accepted (latching)
(No [OCPP_SM_DIAG] 🎯 onTransactionStarted() marker)
[Status] State: Still Preparing
```
→ **Reason:** MicroOcpp not calling TxNotification_StartTx. Check if StartTransaction.req was sent to server.

**Issue #2: StartTx Called But State Not Transitioning**
```
[OCPP_SM_DIAG] 🎯 onTransactionStarted() CALLED...
[OCPP_SM_DIAG] 🔄 forceState() called: Preparing → Charging (check if same)
(No [OCPP_SM] 🔄 State: Preparing → Charging marker)
[OCPP_SM_DIAG] ℹ️  Already in Preparing state, no change
```
→ **Reason:** `currentState == newState` check returned true (shouldn't happen). State already in wrong state.

**Issue #3: forceState() Called But State Shows Still Preparing**
```
[OCPP_SM] 🔄 State: Preparing → Charging ✅ TRANSITION APPLIED
(But next [Status] line still shows State: Preparing)
```
→ **Reason:** `getStateName()` might have a bug or is reading from different state variable.

---

## Root Cause Identification Matrix

| Symptom | Likely Cause |  |
|---------|--------------|---|
| No `[OCPP] 🎯 *** TxNotification_StartTx` | StartTransaction.req not sent or MicroOcpp not invoking callback | Check server received it |
| `[OCPP_SM_DIAG] ℹ️  Already in Preparing state` | currentState already = Preparing when forceState() tries to transition | State variable corruption |
| `[OCPP_SM] 🔄 State: Preparing → Charging` appears but Status still `Preparing` | getStateName() reading stale state or wrong instance | Check g_ocppStateMachine reference |
| No `[OCPP_SM_DIAG]` markers at all | State machine not being called at all | Check call path: ocpp_manager → g_ocppStateMachine |

---

## Next Analysis Steps

### If StartTx Handler IS Being Called:

1. **Verify forceState() is in the transition path:**
   ```
   Look for: [OCPP_SM] 🔄 State: Preparing → Charging ✅ TRANSITION APPLIED
   ```
   → If you see this, state transition IS happening but not being reported

2. **Check getStateName() implementation:**
   ```cpp
   // In ocpp_state_machine.h/cpp
   const char* getStateName() {
       return STATE_NAMES[currentState];  // Verify currentState is updated
   }
   ```

3. **Verify status line is reading correct state:**
   - Look for: `[DIAGNOSTIC] 🔍 State Machine Status: Charging`
   - If this shows `Charging` but `[Status]` still shows `Preparing`, getStateName() has a bug

---

## Emergency Mitigation

If state transition still stuck after analysis:

**Option 1: Check if getStateName() is reading wrong state variable**
```cpp
// In ocpp_state_machine.cpp getStateName()
const char* OCPPStateMachine::getStateName() const {
    // Add diagnostic
    Serial.printf("[OCPP_SM_GETSTATE] currentState=%d, name=%s\n", 
                 static_cast<int>(currentState), 
                 STATE_NAMES[static_cast<int>(currentState)]);
    return STATE_NAMES[static_cast<int>(currentState)];
}
```

**Option 2: Verify g_ocppStateMachine is the correct instance**
```cpp
// In main.cpp when printing status
extern prod::OCPPStateMachine g_ocppStateMachine;
const char* state = g_ocppStateMachine.getStateName();
Serial.printf("[DEBUG] Using g_ocppStateMachine instance at %p, state=%s\n", 
             &g_ocppStateMachine, state);
```

---

## Success Criteria

✅ All these logs appear in sequence:
1. `[OCPP] 🎯 *** TxNotification_StartTx CALLBACK FIRED ***`
2. `[OCPP_SM_DIAG] 🎯 onTransactionStarted() CALLED`
3. `[OCPP_SM] 🔄 State: Preparing → Charging ✅ TRANSITION APPLIED`
4. `[DIAGNOSTIC] 🔍 State Machine Status: Charging`
5. `[Status]` line shows `State: Charging`

If **any** of these is missing, that's where the chain breaks.

---

## Files to Share in Response

1. **Full ESP32 serial output** during RemoteStart test
2. **curl response** from server
3. **Server logs** (docker logs csms-citrineos-core --tail 100)
4. **Database state:**
   ```bash
   docker exec csms-postgres psql -U citrine -d citrine -c \
     "SELECT transactionId, isActive, startTime FROM Transactions ORDER BY id DESC LIMIT 1;"
   ```

---

**Ready?** Monitor serial, send RemoteStart, and capture the full 60-second window showing all the state machine diagnostics.
