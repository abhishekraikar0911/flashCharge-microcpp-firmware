# RemoteStart Diagnostic Test - Feb 9, 2026

## What Was Changed

Added CRITICAL diagnostic logging to identify why RemoteStart callback is not being invoked:

### Changes Made:
1. **Line ~360:** Added `[OCPP_CALLBACK] 🔔 TxNotification fired` marker to log EVERY callback invocation
2. **Line ~372:** Added `[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***` critical marker
3. **Line ~530:** Added periodic diagnostic log every 30s showing callback is active
4. **Line ~478:** Added "Waiting for RemoteStart command from CSMS..." message

## Test Procedure

### Step 1: Rebuild Firmware

From PlatformIO:
```bash
platformio run          # Compile with new diagnostic logs
platformio run --target upload  # Flash to ESP32
```

### Step 2: Capture Baseline (Before Sending RemoteStart)

Monitor serial output for ~15 seconds and look for:
- ✅ `[OCPP]   📡 Waiting for RemoteStart command from CSMS...`
- ✅ `[OCPP_DIAG] ✅ CallbackActive` (every 30s)
- This shows callback is registered and monitoring

Expected log snippet:
```
[OCPP]   ✓ Transaction callbacks registered
[OCPP]   🔍 Registered operations including: RemoteStartTransaction, RemoteStopTransaction
[OCPP]   📡 Waiting for RemoteStart command from CSMS...

[Status] Uptime: 10s | WiFi: ✅ | OCPP: Connected | State: Preparing
[Metrics] V=79.2V I=31.9A SOC=40.9% Range=66.2km
```

### Step 3: Send RemoteStart from Server

On your remote Linux server:
```bash
curl -X POST http://localhost:8000/api/stations/250822008C06/remote-start \
  -H "Content-Type: application/json" \
  -d '{"idTag":"TEST_TAG","connectorId":1}' \
  -v  # Verbose to see response
```

**Critical:** Save the curl response (will show if server accepted the command).

### Step 4: Monitor ESP32 Serial Immediately After Sending RemoteStart

Capture ALL output in the next 30 seconds. Look for:

**Scenario A: GOOD - Callback IS being called**
```
[OCPP_CALLBACK] 🔔 TxNotification fired: type=X connectorId=1 txId=valid
[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***
[OCPP] 📥 RemoteStart received
[OCPP]   snapshot: txId=XXX txRunning=0 permitsCharge=1
[OCPP] ✅ RemoteStart accepted (latching)
```
→ This means fix is working, move to next phase

**Scenario B: BAD - Callback IS NOT being called**
```
(No [OCPP_CALLBACK] or [OCPP] 🎯 markers appear)
(No state change, still shows State: Preparing)
```
→ This means RemoteStart message is NOT reaching MicroOcpp
→ **Root cause could be:**
  - Server didn't actually send it (check server logs)
  - WebSocket dropped it
  - Firewall blocking it
  - Wrong connector ID

**Scenario C: BAD - Callback called but RemoteStart rejected**
```
[OCPP_CALLBACK] 🔔 TxNotification fired: ...
[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***
[OCPP] ❌ REJECTING: [reason]
```
→ One of the safety checks failed:
  - Charger module offline
  - BMS charging disabled
  - Transaction already active

---

## What Each Diagnostic Log Means

| Log | Meaning | Status |
|-----|---------|--------|
| `[OCPP]   📡 Waiting for RemoteStart` | Callback registered successfully | ✅ Good |
| `[OCPP_DIAG] ✅ CallbackActive` | Callback still listening (appears every 30s) | ✅ Good |
| `[OCPP_CALLBACK] 🔔 TxNotification fired` | **ANY** transaction event occurred | ✅ Good (proves callback works) |
| `[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***` | Server sent RemoteStart to THIS station | ✅ CRITICAL SUCCESS |
| `[OCPP] ✅ RemoteStart accepted (latching)` | Remote start accepted, will issue StartTransaction | ✅ Next step |
| *(nothing appears after RemoteStart sent)* | **RemoteStart message was lost or not sent** | ❌ DEBUG SERVER |

---

## Debug: If Callback Is NOT Being Called

Check:

1. **Server logs** - Did CitrineOS actually send RemoteStart?
```bash
docker logs csms-citrineos-core --tail 100 | grep -i "remotestart\|station.*250822008C06"
```

2. **WebSocket connection** - Is it still active?
```
Look for: [MO] Recv: WS pong  # Should appear frequently
```

3. **Check for WebSocket errors in logs:**
```
Look for: [MO] error ... connection ... closed
```

4. **Check connector ID** - Server must send to connector 1:
```bash
# On server, check EVDriver handler
grep -n "connectorId\|RemoteStartTransaction" /opt/csms/.../handlers.js | head -10
```

---

## Test Success Criteria

Test is **SUCCESSFUL** if:
1. ✅ `[OCPP] 🎯 *** RemoteStart NOTIFICATION RECEIVED ***` appears in logs
2. ✅ `[OCPP] ✅ RemoteStart accepted (latching)` appears  
3. ✅ State transitions from `Preparing` → `Charging` (may take 1-2 seconds)
4. ✅ `[GATE] ✅ HARD GATE OPEN` appears in logs
5. ✅ `[Status]` line shows `State: Charging`

If ALL of these appear: **RemoteStart is working, proceed to RemoteStop test**.

---

## After Test: Share These Logs

1. **curl response** — What the server returned
2. **ESP32 serial output** — Full 30-second capture after RemoteStart sent
3. **Server logs** — Last 100 lines from CitrineOS
4. **DB state** — Check if transaction was created

---

## Emergency Fallback

If callback still not being called after this diagnostic:

**Option 1: Add WebSocket frame logging** (will help us see if message arrives)
```cpp
// In ocpp_manager.cpp poll()
Serial.println("[OCPP] *** Checking for pending WebSocket messages...");
```

**Option 2: Check MicroOcpp version**
```cpp
// In ocpp_manager.cpp init()
Serial.printf("[OCPP] MicroOcpp version: %s\n", MICROOCPP_VERSION);  // If defined
```

**Option 3: Test with simpler message** — Send a DataTransfer or TriggerMessage instead of RemoteStart to see if ANY message can reach the callback

---

**Ready?** 
1. Run `platformio run --target upload`
2. Send RemoteStart from server
3. Capture and share the logs
