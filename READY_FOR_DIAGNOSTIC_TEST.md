# RemoteStart State Stuck in Preparing - DIAGNOSIS & NEXT STEPS

**Date:** Feb 9, 2026  
**Status:** Firmware rebuilt with diagnostic logging. Ready for test.

---

## Problem Summary

Your logs show:
```
[Status] Uptime: 100s | WiFi: ✅ | OCPP: Connected | State: Preparing
```

**Even after RemoteStart was sent from server, state is STILL `Preparing` (should be `Charging`).**

This means: **RemoteStart command is NOT reaching the client's transaction handler.**

---

## Root Causes We're Testing

### Possible Cause #1: Message Never Arrived at Client
- Server sent RemoteStart, but WebSocket dropped it
- Check: Did curl get response from server? Was it "Accepted"?

### Possible Cause #2: At MicroOcpp Level
- WebSocket received RemoteStart but MicroOcpp didn't invoke the callback
- Check: Will we see `[OCPP_CALLBACK]` logs?

### Possible Cause #3: Connector ID Mismatch
- Server sending to connector 0, your handler listening to connector 1
- Check: Will logs show which connector?

### Possible Cause #4: Safety Gate Check Failed
- BMS not ready, or charger offline
- Check: Will we see rejection reason?

---

## What Changed in Firmware (Build Just Completed)

### ✅ Added Diagnostic Markers:

1. **At callback entry** (`[OCPP_CALLBACK] 🔔 TxNotification fired`)
   - Shows type, connector, txId status
   - Proves callback is being invoked

2. **At RemoteStart handler** (`[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***`)
   - CRITICAL marker
   - ONLY appears if MicroOcpp called the handler for RemoteStart specifically

3. **Periodic status every 30s** (`[OCPP_DIAG] ✅ CallbackActive`)
   - Confirms callback is still registered and listening
   - Shows remoteStartAccepted, transactionActive, activeTxId

4. **At handler registration** (`📡 Waiting for RemoteStart command from CSMS...`)
   - Confirms callback setup succeeded

---

## Test: What You Need To Do NOW

### Phase 1: Baseline (Verify Diagnostics Work)

1. **Monitor serial output** for next 15-20 seconds:
   ```
   Look for:
   - [OCPP]   📡 Waiting for RemoteStart command from CSMS...  ✅ GOOD
   - [OCPP_DIAG] ✅ CallbackActive ...  (appears every 30s)  ✅ GOOD
   ```

   If you see these → diagnostic logging is working.

---

### Phase 2: Send RemoteStart from Server

**On your remote server:**

```bash
curl -X POST http://localhost:8000/api/stations/250822008C06/remote-start \
  -H "Content-Type: application/json" \
  -d '{"idTag":"TEST_TAG","connectorId":1}' \
  -v
```

**Important:** Save the response (look for status: accepted/rejected)

---

### Phase 3: Monitor Client Output (CRITICAL)

**Immediately after the curl command, watch ESP32 serial output for the next 10 seconds.**

#### Scenario A: GOOD - Callback working
```
[OCPP_CALLBACK] 🔔 TxNotification fired: type=0 connectorId=1 txId=valid
[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***
[OCPP] 📥 RemoteStart received
[OCPP]   snapshot: txId=123 txRunning=0 permitsCharge=1
[OCPP]   flags: chargerHealthy=1 bmsSafe=1 batteryConnected=1 gunPhys=1
[OCPP] ✅ RemoteStart accepted (latching)

[MO] verbose (Connector.cpp:...): issuing StartTransaction
[OCPP] 📥 StartTransaction notification
[GATE] ✅ HARD GATE OPEN

[Status] Uptime: XXs | State: Charging  ← STATE CHANGED!
```

**If you see this:** Fix is working! Move to Phase 4.  
**Next step:** Test RemoteStop behavior.

---

#### Scenario B: BAD - Callback NOT being called
```
(Several seconds pass after curl) 
(State still shows: Preparing)
(No [OCPP_CALLBACK] or [OCPP] 🎯 markers appear)
```

**If this happens:** RemoteStart message is lost between server and MicroOcpp.  
**Next step:** Investigate server/network issue (see Debugging section below).

---

#### Scenario C: BAD - Callback called but rejected
```
[OCPP_CALLBACK] 🔔 TxNotification fired: ...
[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***
[OCPP] ❌ REJECTING: Charger module OFFLINE   ← REASON
         (OR) BMS disallows charging
         (OR) transaction already active
```

**If this happens:** Check the rejection reason.  
**Next step:** Fix the underlying safety condition (see Debugging section below).

---

## Phase 4: After Successful RemoteStart

If Phase 3 shows the callback IS working and State changes to `Charging`:

1. **Verify database transaction created:**
   ```bash
   docker exec csms-postgres psql -U citrine -d citrine -c \
     "SELECT id, transactionId, isActive, startTime FROM Transactions WHERE stationId='250822008C06' ORDER BY id DESC LIMIT 1;"
   ```
   Expected: `isActive = true`, `startTime = NOW()`

2. **Test RemoteStop:**
   ```bash
   curl -X POST http://localhost:8000/api/stations/250822008C06/remote-stop \
     -H "Content-Type: application/json" \
     -d '{"transactionId":"LAST_TXID_FROM_DB"}' \
     -v
   ```

3. **Monitor serial for RemoteStop markers:**
   ```
   [OCPP_CALLBACK] 🔔 TxNotification fired: ...
   [OCPP] 📥 RemoteStop received
   [OCPP] 🛑 RemoteStop → ending transaction
   [OCPP] ✅ StopTransaction queued to server
   [GATE] 🔒 HARD GATE CLOSED
   ```

4. **Verify transaction closed in DB:**
   ```bash
   # Check same transaction again
   # Should now show: isActive = false, endTime = NOW()
   ```

---

## Debugging: If Callback Not Being Called

**Check #1: Server Logs**
```bash
docker logs csms-citrineos-core --tail 200 | grep -i "remotestart\|250822008C06"
```

Look for:
- Was RemoteStart actually sent?
- Any errors trying to create transaction?
- Any WebSocket/routing errors?

---

**Check #2: ESP32 WebSocket Activity**
```
Look in serial for: [MO] Recv: WS pong  (should appear every 40s or so)
```

If pongs stop or disconnect shown → network issue.

---

**Check #3: MicroOcpp Operation Registry**
```
Your logs show:
[MO] debug (OperationRegistry.cpp:40): registered operation RemoteStartTransaction ✅
```

This is correct. RemoteStartTransaction IS registered.

---

**Check #4 (if still stuck): Connector ID**

In `src/modules/ocpp_manager.cpp`, find the `setTxNotificationOutput` registration (around line 360).

The callback doesn't specify a connector ID explicitly - MicroOcpp calls it for ALL connectors. So connector 0 vs 1 shouldn't matter **unless** the server is sending commands to connector 0 and your charger only has connector 1.

---

## Immediate Action List

- [ ] **BUILD:** ✅ Complete (102 sec, uploaded)
- [ ] **TEST 1:** Monitor for diagnostic markers (15 sec)
- [ ] **TEST 2:** Send RemoteStart, capture output (30 sec)
- [ ] **TEST 3:** Verify callback fired or not (5 sec analysis)
- [ ] **TEST 4:** If working, test RemoteStop (30 sec)
- [ ] **TEST 5:** Verify DB transactions (database query)
- [ ] **SHARE:** Send logs + screenshots

---

## Success Criteria (Complete Test)

✅ `[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***` appears  
✅ State changes from `Preparing` → `Charging` within 2 seconds  
✅ `[GATE] ✅ HARD GATE OPEN` appears  
✅ Database shows transaction with `isActive=true`  
✅ RemoteStop command is received and handled  
✅ State transitions back to `Preparing` or `Available`  
✅ Database shows transaction with `isActive=false, endTime=populated`  

If **ALL** of these: ✅ **TEST PASSED** - Issue is fixed!

---

## Documents Created

- `DIAGNOSTIC_TEST_PROCEDURE.md` - Detailed test steps with all scenarios
- `REMOTESTART_STATE_DEBUG.md` - Deep debugging reference
- `TEST_AND_DEPLOY_CHECKLIST.md` - Full end-to-end checklist
- `SERVER_REBUILD_GUIDE.md` - CitrineOS rebuild steps

---

## Ready to Test?

**Next step:** Run the test and share:
1. **Full serial output** from when you send RemoteStart until 10s after
2. **curl response** showing server's accept/reject
3. **Server logs** (last 100 lines)
4. **Database query result** (transaction state after test)

---

**Awaiting test results!** 🚀
