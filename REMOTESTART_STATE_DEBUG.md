# RemoteStart State Transition Issue - Debug Guide

## Problem
- Server sent RemoteStart (presumably)
- Client received it at OCPP level (MicroOcpp)
- BUT client shows `State: Preparing` (not `State: Charging`)
- Missing log: `[OCPP] 📥 RemoteStart received`

## Expected Flow
```
RemoteStart sent by server
  ↓
MicroOcpp receives RemoteStart (TxNotification_RemoteStart)
  ↓
Your handler logs: [OCPP] 📥 RemoteStart received
  ↓
Handler accepts RemoteStart, sets remoteStartAccepted=true
  ↓
MicroOcpp issues StartTransaction.req to server
  ↓
Handler receives TxNotification_StartTx
  ↓
State transitions: Preparing → Charging
```

## Step 1: Verify Server Sent RemoteStart

**On remote server, check CitrineOS logs:**
```bash
docker logs csms-citrineos-core --tail 200 | grep -A 5 "RemoteStart\|Remote start"

# Expected:
# - Log showing RemoteStart command was sent to station 250822008C06
# - No error messages about transaction creation
# - Response status should be "Accepted" or "Pending"
```

**Check if server found the station:**
```bash
docker exec csms-postgres psql -U citrine -d citrine -c \
  "SELECT id, name, \"stationId\" FROM \"Stations\" WHERE \"stationId\"='250822008C06';"

# Should return at least 1 row
```

---

## Step 2: Check MicroOcpp Logs for RemoteStart Reception

**Look for MicroOcpp framework logs showing RemoteStart:**

In your current log, search for:
- `[MO] .... RemoteStart` — Any mention of RemoteStart in MicroOcpp debug
- `[MO] Recv: ... RemoteStart` — WebSocket frame received
- TxNotification callback being invoked

**Current log shows:**
```
[MO] debug (OperationRegistry.cpp:40): registered operation RemoteStartTransaction ✅
```

But we DON'T see:
```
[MO] Recv: [2, ..., "RemoteStartTransaction", ...   ← MISSING!
```

This suggests **the WebSocket message never arrived at the client**.

---

## Step 3: Check If WebSocket Is Receiving Messages

**Add logging to verify WebSocket activity:**

In `src/modules/ocpp_manager.cpp`, find the `ocpp::poll()` function and add:

```cpp
void ocpp::poll() {
    if (!initialized) return;
    
    // Add WebSocket frame counter
    static unsigned long lastWebSocketLog = 0;
    if (millis() - lastWebSocketLog > 10000) {  // Log every 10s
        // Check internal MicroOcpp counters
        Serial.printf("[OCPP_DEBUG] WebSocket frames received in last 10s\n");
        lastWebSocketLog = millis();
    }
    
    mocpp_loop();
}
```

**Better: Add a MicroOcpp request callback:**

```cpp
// In ocpp::init(), after mocpp_initialize():

// Add callback to debug ALL received OCPP operations
if (setReceiveHandler([](JsonObject payload) -> bool {
    const char* action = payload["action"] | "";
    Serial.printf("[MicroOcpp_RCV] Action: %s\n", action);
    return true;
})) {
    Serial.println("[OCPP] ✅ Request receive handler registered");
}
```

---

## Step 4: Verify TxNotification Callback Is Being Called

**Current code at TxNotification_RemoteStart handler:**

Check if the handler code is being reached. Add this at the START of `TxNotification_RemoteStart` block:

```cpp
else if (notification == TxNotification_RemoteStart) {
    Serial.println("\n[OCPP] 🎯 TxNotification_RemoteStart FIRED!");  // ← ADD THIS
    Serial.println("[OCPP] 📥 RemoteStart received");
    // ... rest of handler
}
```

**If you rebuild and run, and you DON'T see `[OCPP] 🎯 TxNotification_RemoteStart FIRED!`, then:**
- The callback is never being invoked
- The problem is in MicroOcpp message routing, not your handler

---

## Step 5: Check Server Communication

**Verify the server actually sent to THIS station ID:**

Your logs show:
```
[OCPP] 📍 StationId: 250822008C06
[OCPP] 🌐 Base URL: ws://103.174.148.201:8092
[OCPP] Full Connection URL: ws://103.174.148.201:8092/250822008C06
```

This is CORRECT. But verify CitrineOS is sending to this exact ID.

**Check CitrineOS database:**
```bash
docker exec csms-postgres psql -U citrine -d citrine -c \
  "SELECT * FROM \"StationConnectors\" WHERE \"stationId\"='250822008C06';"
```

Should show connector configuration for station.

---

## Step 6: Immediate Action Plan

### Phase A: Add Debug Logging (TODAY)

Edit [src/modules/ocpp_manager.cpp](src/modules/ocpp_manager.cpp#L407) and add visibility:

**At TxNotification_RemoteStart (line ~350):**
```cpp
else if (notification == TxNotification_RemoteStart) {
    Serial.println("\n[OCPP] 🎯 *** TxNotification_RemoteStart CALLBACK FIRED ***");  // CRITICAL DEBUG
    Serial.printf("[OCPP] 📥 RemoteStart received at timestamp: %ld ms\n", millis());
    
    // Debug: print transaction object to see what MicroOcpp parsed
    Serial.printf("[OCPP] Transaction ID from server: %d\n", transaction.getTransactionId());
    Serial.printf("[OCPP] IdTag: %s\n", transaction.getIdTag() ? transaction.getIdTag() : "NULL");
    
    // Your existing logic
    Serial.println("[OCPP] 📥 RemoteStart received");
    // ... rest of code
}
```

**At ocpp::poll() entry:**
```cpp
void ocpp::poll() {
    if (!initialized) return;
    
    static unsigned long lastPollLog = 0;
    if (millis() - lastPollLog > 15000) {  // Every 15s
        Serial.printf("[OCPP_POLL] State: %d | TX_Active: %d | ActiveTxId: %d\n", 
                      g_ocppStateMachine.getCurrentState(), 
                      transactionActive, activeTransactionId);
        lastPollLog = millis();
    }
    
    mocpp_loop();
}
```

### Phase B: Rebuild and Test (TODAY)

```bash
platformio run          # Compile with new debug logs
platformio run --target upload  # Flash to ESP32
# Monitor serial output
```

### Phase C: Send RemoteStart Again

From server:
```bash
curl -X POST http://localhost:8000/api/stations/250822008C06/remote-start \
  -H "Content-Type: application/json" \
  -d '{"idTag":"TEST_TAG","connectorId":1}'
```

**Capture ESP32 output and look for:**
- ✅ `[OCPP] 🎯 *** TxNotification_RemoteStart CALLBACK FIRED ***` — Callback was invoked
- ✅ `[OCPP] Transaction ID from server: XXX` — Server passed a transaction ID
- ✅ `[OCPP] 📥 RemoteStart received` — Your handler got it
- ✅ `[OCPP] ✅ RemoteStart accepted` — Your handler accepted it
- ✅ Later: `[OCPP] 📥 StartTransaction received` — MicroOcpp issued StartTransaction

If **ANY** of these is missing, it tells us exactly where the problem is.

---

## Step 7: If Callback Is NOT Being Called

**That means MicroOcpp is not routing RemoteStart to your handler.**

Possible causes:
1. **Connector ID mismatch** — Server sending to connector 0, your handler listening to connector 1
2. **TxNotification not registered** — Check your registration code
3. **WebSocket message lost** — Network/TLS issue (unlikely, since VehicleInfo works)
4. **Message ID correlation** — MicroOcpp can't match response (those warnings about "Received response doesn't match")

**To fix: Check your TxNotification registration:**

In `ocpp::init()`, find where you register the transaction notification callback:
```cpp
if (!setTransactionEventHandler(...)) {
    Serial.println("[OCPP] ❌ Failed to register transaction handler!");
}
```

Verify:
- ✅ Handler is registered
- ✅ Listening to connector 1 (or the correct connector ID server is using)
- ✅ Listening to ALL transaction notifications (RemoteStart, StartTx, RemoteStop, StopTx)

---

## Step 8: Check Connector ID

**Your logs show:**
```
[MO] debug (Connector.cpp:123): found 0 transactions for connector 0
[MO] debug (Connector.cpp:117): found tx-1-36.jsn - Internal range from 36 to 38 (inclusive)
[MO] debug (Connector.cpp:123): found 4 transactions for connector 1
```

Good! MicroOcpp knows about **Connector 1** (where your transactions are).

**Verify your handler is listening to Connector 1:**

In your code, check:
```cpp
// Should be Connector 1, NOT 0
ocpp::setTransactionEventHandler(1, [](Transaction* txn, TxNotification notification) {
    // This should fire for RemoteStart on connector 1
});
```

If you're listening to connector 0, that's the problem!

---

## Summary

**Most Likely Issue:**
- RemoteStart message IS arriving at MicroOcpp via WebSocket
- BUT your TxNotification callback is NOT being fired
- Reason: Connector ID mismatch OR callback not registered

**Next Steps:**
1. Add the critical debug log: `[OCPP] 🎯 *** TxNotification_RemoteStart CALLBACK FIRED ***`
2. Rebuild and reflash
3. Send RemoteStart again
4. Share the new logs — focus on whether you see that debug line
5. If NO debug line → problem is in handler registration/connector ID
6. If YES debug line → problem is in handler logic or state machine transition

---

## Quick Verification Checklist

Run these checks on ESP32 to confirm current state:

```
✅ WebSocket connected? (Check log: "[MO] info (Connection.cpp:77): Connected")
✅ BootNotification accepted? (Check log: "[OCPP] Connection status changed: CONNECTED")
✅ Plug detected? (Check log: "[OCPP_SM] 🔌 Plug state changed: CONNECTED")
✅ State is Preparing? (Check latest [Status] line)
✅ Charger Module ONLINE? (Check log: "[Charger] Module=ONLINE")

❌ Missing: TxNotification_RemoteStart log
❌ Missing: State transition Preparing → Charging
❌ Missing: StartTransaction notification
```

ALL of these point to: **RemoteStart command not reaching the handler**.

---

**Ready to add the debug logs?** Let me know the exact line numbers if you need help inserting them.
