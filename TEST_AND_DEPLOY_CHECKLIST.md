# RemoteStart/RemoteStop End-to-End Test Plan
## Feb 9, 2026 - Status: Ready for Validation

---

## ✅ COMPLETED

### Client-Side (ESP32)
- [x] Added verbose RemoteStop handler (line 433): calls endTransactionSafe()
- [x] Added BMS safety check to RemoteStart handler
- [x] Added debug snapshots to all transaction handlers
- [x] Source code verified: [ocpp_manager.cpp](src/modules/ocpp_manager.cpp#L407-L444)

### Server-Side (CitrineOS)
- [x] Created correct RemoteStart/RemoteStop handlers (don't create transactions)
- [x] Handlers just accept commands and let client send StartTransaction/StopTransaction
- [x] RabbitMQ auto-resubscribe fix already applied

---

## ⏳ TODO - Deployment

### Server Rebuild (Linux machine running CitrineOS)

```bash
# Run these commands on your remote server (via SSH or docker):

cd /opt/csms/citrineos-core && \
docker-compose down && \
rm -rf node_modules dist build && \
docker-compose build --no-cache --build-arg NODE_ENV=production && \
docker-compose up -d

# Wait for startup
sleep 30

# Verify handlers deployed
docker logs csms-citrineos-core --tail 50 | grep -E "module|started|listening"

# Confirm no "duplicate transaction" errors
docker logs csms-citrineos-core --tail 200 | grep -i "duplicate\|error" || echo "✅ No duplicate errors"
```

Status: **Awaiting execution**

### Client Rebuild (Optional - only if firmware changed since last build)

From your PlatformIO workspace:
```bash
# If you've modified ocpp_manager.cpp, rebuild:
platformio run          # Compile
platformio run --target upload   # Flash to ESP32

# The fix IS already in the source code, so just verify it compiled
```

Status: Build completed (via Build terminal showing Exit Code: 0)

---

## ⏳ TODO - Test Execution

### Phase 1: Baseline (Pre-RemoteStart)
**Goal:** Verify system is healthy before issuing commands

```bash
# Check ESP32 serial output shows:
# [✓] WebSocket connected
# [✓] State: Preparing (after plug detected)
# [✓] All flags initialized
# [✓] BootNotification accepted
```

Status: **✅ Baseline verified** at 06:19 UTC Feb 9

---

### Phase 2: RemoteStart Test

**Step 1: Send RemoteStart from server**

Run on server machine:
```bash
# Option A: If you have curl access to Citrine API
curl -X POST http://localhost:8000/api/stations/250822008C06/remote-start \
  -H "Content-Type: application/json" \
  -d '{"idTag":"TEST_TAG","connectorId":1}' \
  -s | jq .

# Option B: Via docker on server
docker exec csms-citrineos-core node -e \
  "const { RemoteStartTransaction } = require('./dist'); \
   RemoteStartTransaction('250822008C06', 'TEST_TAG', 1);"

# Option C: Use the CitrineOS CLI/admin interface (if available)
```

**Step 2: Monitor client serial output**

Expected sequence:
```
[OCPP] 📥 RemoteStart received
[OCPP]   snapshot before RemoteStart: txActive=0 activeTx=-1
[OCPP] ✅ RemoteStart accepted (latching)
[GATE] Gate check: charger=healthy bms=safe battery=connected gun=physical
[OCPP] 📥 StartTransaction received (txId=NNN)
[OCPP] ▶️  Transaction STARTED - Charging ENABLED (txId=NNN)
[GATE] ✅ HARD GATE OPEN
[OCPP] 📊 MeterValues will be sent automatically
```

**Step 3: Verify server logs**

Run on server:
```bash
docker logs csms-citrineos-core --tail 20 | grep -A 3 "RemoteStart"

# Expected: Handler logs accepting RemoteStart, NOT creating transaction
# Expected: See StartTransaction.req from client
```

**Step 4: Check database**

Run on server:
```bash
docker exec csms-postgres psql -U citrine -d citrine -c \
  "SELECT id, \"transactionId\", \"isActive\", \"startTime\" FROM \"Transactions\" \
   WHERE \"stationId\"='250822008C06' ORDER BY id DESC LIMIT 1;"

# Expected: Transaction created when StartTransaction received (not when RemoteStart received!)
# Expected: isActive = true
# Expected: stationId = 250822008C06
```

Status: **Awaiting execution**

---

### Phase 3: RemoteStop Test

**Step 1: Send RemoteStop from server**

```bash
# After RemoteStart completed successfully, send RemoteStop
curl -X POST http://localhost:8000/api/stations/250822008C06/remote-stop \
  -H "Content-Type: application/json" \
  -d '{"transactionId":"LAST_TX_ID"}' \
  -s | jq .

# Or via docker/CLI method (same as RemoteStart above)
```

**Step 2: Monitor client serial output for NEW debug markers**

Expected sequence:
```
[OCPP] 📥 RemoteStop received
[OCPP]   snapshot before stop: txActive=1 activeTx=NNN txRunning=1
[OCPP] ⏹️  Charging disabled - hardware stop command sent

[OCPP] 🛑 RemoteStop → ending transaction (txId=NNN)    <-- NEW FIX MARKER
[OCPP] ✅ StopTransaction queued to server                <-- NEW FIX MARKER

[OCPP] 📥 StopTransaction received
[OCPP] ⟹️  Transaction STOPPED and UNLOCKED
[GATE] 🔒 HARD GATE CLOSED
```

**Critical markers showing fix is working:**
- `[OCPP] 🛑 RemoteStop → ending transaction` — RemoteStop handler entered correct path
- `[OCPP] ✅ StopTransaction queued to server` — endTransactionSafe() succeeded

Status: **Awaiting execution**

**Step 3: Verify server logs**

```bash
docker logs csms-citrineos-core --tail 30 | grep -A 3 "RemoteStop"

# Expected: Handler accepts RemoteStop
# Expected: Sees StopTransaction.req from client (this is the key!)
```

**Step 4: Verify database shows transaction closed**

```bash
docker exec csms-postgres psql -U citrine -d citrine -c \
  "SELECT id, \"transactionId\", \"isActive\", \"startTime\", \"endTime\" FROM \"Transactions\" \
   WHERE \"stationId\"='250822008C06' ORDER BY id DESC LIMIT 1;"

# Expected: isActive = false
# Expected: endTime = (not NULL, shows when StopTransaction was received)
# Expected: transactionId matches what was in client logs
```

Status: **Awaiting execution**

---

## Validation Checklist

After running Phases 1-3, verify:

- [ ] **Client RemoteStart Accepted:** Log shows `✅ RemoteStart accepted`
- [ ] **Client StartTransaction Received:** Log shows StartTransaction notification
- [ ] **Gate Opens:** Log shows `✅ HARD GATE OPEN`
- [ ] **Server DB Transaction Created:** Transaction row exists with isActive=true
- [ ] **Client RemoteStop Received:** Log shows `📥 RemoteStop received`
- [ ] **NEW FIX MARKER 1:** Log shows `🛑 RemoteStop → ending transaction` (proves handler called endTransactionSafe)
- [ ] **NEW FIX MARKER 2:** Log shows `✅ StopTransaction queued to server` (proves endTransactionSafe succeeded)
- [ ] **Client StopTransaction Received:** Log shows StopTransaction notification
- [ ] **Gate Closes:** Log shows `🔒 HARD GATE CLOSED`
- [ ] **Server DB Transaction Closed:** Transaction row shows isActive=false and endTime populated
- [ ] **No Duplicate Errors:** Server logs have NO "duplicate key" or "transaction already exists" errors

---

## Success Criteria

✅ **Test passes if:**
1. Client sends RemoteStart → RemoteStop cycle WITHOUT hanging
2. Both `🛑` and `✅` markers appear in client logs (new fix is executing)
3. Server DB shows transaction opened when StartTransaction received and closed when StopTransaction received
4. No "duplicate transaction" errors in server logs

---

## Logs to Capture

Please share these after running the test:

1. **Client Serial (±30s around test):** 
   - From "📥 RemoteStart received" to "🔒 HARD GATE CLOSED"
   - Paste as: client-serial.log

2. **Server CitrineOS logs (last 100 lines):**
   ```bash
   docker logs csms-citrineos-core --tail 100 > server-app.log
   ```
   - Paste as: server-app.log

3. **Server Database state (after test):**
   ```bash
   docker exec csms-postgres psql -U citrine -d citrine -c \
     "SELECT * FROM \"Transactions\" WHERE \"stationId\"='250822008C06' ORDER BY id DESC LIMIT 3;" > db-state.txt
   ```
   - Paste as: db-state.txt

---

## Rollback Plan (If Issues Occur)

If new bugs appear after rebuild:

**Client:**
```bash
platformio run --target upload  # Flash previous version or rebuild
```

**Server:**
```bash
docker-compose restore 
# Or manually restore from backup image
docker stop csms-citrineos-core
docker rm csms-citrineos-core
docker run -d --name csms-citrineos-core ... citrineos-core:previous-tag
```

---

**Next Action:** 
1. Run server rebuild commands on your remote Linux machine
2. Execute Phase 2 (RemoteStart) and capture logs
3. Execute Phase 3 (RemoteStop) and capture logs
4. Share the 3 log files above for verification

All client-side code is ready. Awaiting server rebuild and test execution.
