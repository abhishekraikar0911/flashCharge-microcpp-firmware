# CitrineOS Server Rebuild Guide - Feb 9, 2026

## Current Issue
The running server has **OLD handlers** that try to create transactions on RemoteStart. The correct implementation just accepts the command and lets the client send StartTransaction.

## Fix Location
Your corrected handlers should be in the CitrineOS source code at:
```
/opt/csms/citrineos-core/src/evdriver/handlers.js  (or similar path)
```

## Rebuild Steps

### Step 1: Verify Source Code Has Fix
Run on the remote server (via SSH or docker exec):

```bash
# Check the running container first
docker ps | grep citrine

# Then verify the source code has the fix (handlers should NOT create transactions)
docker exec csms-citrineos-core grep -A 15 "RemoteStartTransaction" /opt/citrineos/src/modules/evdriver/index.js 2>/dev/null || \
docker exec csms-node grep -A 15 "RemoteStartTransaction" /app/src/modules/evdriver/index.js 2>/dev/null || \
echo "Handler location may differ - check your specific container"
```

### Step 2: Check Current Handlers in Running Container
```bash
# See what's currently executing (old code showing RemoteStart creating DB transaction)
docker exec csms-citrineos-core cat /opt/citrineos/src/modules/evdriver/index.js | grep -A 20 "RemoteStartTransaction"

# Check for error messages in logs about "duplicate transaction"
docker logs csms-citrineos-core --tail 100 | grep -i "duplicate\|transaction"
```

### Step 3: Full Rebuild Process

**Option A: If you have docker-compose (recommended)**
```bash
# cd into the citrineos directory
cd /opt/csms/citrineos-core

# Stop everything
docker-compose down -v

# Clear old build artifacts (be careful!)
rm -rf node_modules dist build

# Rebuild with no-cache
docker-compose build --no-cache --build-arg NODE_ENV=production

# Start fresh
docker-compose up -d

# Wait 30s for startup
sleep 30

# Verify handlers are deployed
docker logs csms-citrineos-core --tail 30 | grep -E "EVDriver|RemoteStart|RemoteStop"
```

**Option B: If using raw docker (manual build)**
```bash
# Navigate to source
cd /opt/csms/citrineos-core

# View current Dockerfile
cat Dockerfile

# Rebuild image
docker build --no-cache -f Dockerfile -t citrineos-core:latest \
  --build-arg NODE_ENV=production \
  --build-arg NPM_REGISTRY=https://registry.npmjs.org/ \
  -o type=docker .

# Stop old container
docker stop csms-citrineos-core
docker rm csms-citrineos-core

# Start new container with corrected code
docker run -d --name csms-citrineos-core \
  --network csms-network \
  -p 8080:8080 \
  -e NODE_ENV=production \
  -v citrineos-data:/data \
  citrineos-core:latest

# Follow logs
docker logs -f csms-citrineos-core
```

### Step 4: Verify Fix is Deployed
```bash
# Check that the handlers don't have the old "create transaction" error
docker logs csms-citrineos-core --tail 200 | grep -i "error\|duplicate"

# Expected: NO "duplicate key" or "transaction already exists" errors
# Expected: You should see successful RemoteStart/RemoteStop logs only

# Test: Send RemoteStart from client and check logs
docker logs csms-citrineos-core --tail 50 | grep -A 5 "RemoteStart"
```

### Step 5: Database Check (Post-Test)
After running RemoteStart→RemoteStop test:

```bash
# Check transaction status
docker exec csms-postgres psql -U citrine -d citrine -c \
  "SELECT id, \"transactionId\", \"isActive\", \"startTime\", \"endTime\", \"stationId\" FROM \"Transactions\" \
   WHERE \"stationId\"='250822008C06' \
   ORDER BY id DESC LIMIT 5;"

# Expected: Last transaction should have:
# - isActive = false
# - endTime = (timestamp of when RemoteStop was sent)
# - transactionId = (matches what client sent)
```

---

## Troubleshooting

### Issue: "node_modules not found" error during build
**Solution:** Ensure npm install runs during build
```bash
# In Dockerfile, make sure you have:
RUN npm install --production
```

### Issue: Build fails with "ENOENT" errors
**Solution:** Clear Docker build cache and rebuild
```bash
docker system prune -a --volumes
cd /opt/csms/citrineos-core
docker build --no-cache -f Dockerfile -t citrineos-core:latest .
```

### Issue: Old container still running old handlers
**Solution:** Force stop and remove old image
```bash
docker stop csms-citrineos-core
docker rm csms-citrineos-core
docker rmi citrineos-core:old  # if tagged as old
docker run -d --name csms-citrineos-core ... citrineos-core:latest
```

### Issue: Can't find container name
**Solution:** List all containers
```bash
docker ps -a
docker ps -a | grep -i citrine
```

---

## Expected Behavior After Fix

1. **Server receives RemoteStart**
   - Handler logs: accepts RemoteStart
   - Does NOT try to create DB transaction
   - Sends RemoteStart.conf to client

2. **Client receives RemoteStart.conf**
   - Sets remoteStartAccepted=true
   - Issues StartTransaction.req to server
   - Server creates DB transaction

3. **Server receives RemoteStop**
   - Handler logs: accepts RemoteStop
   - Does NOT modify DB transaction (yet)
   - Sends RemoteStop.conf to client

4. **Client receives RemoteStop.conf**
   - Calls endTransactionSafe()
   - Issues StopTransaction.req to server
   - Sets transactionActive=false

5. **Server receives StopTransaction.req**
   - Sets transaction.isActive=false
   - Sets transaction.endTime=NOW()
   - Transaction is closed

---

## Quick Command Summary

```bash
# SSH to server, then run:
cd /opt/csms/citrineos-core && \
docker-compose down && \
rm -rf node_modules dist && \
docker-compose build --no-cache && \
docker-compose up -d && \
sleep 30 && \
docker logs csms-citrineos-core --tail 50
```

---

After rebuild, please share:
1. Output from Step 4 (verify fix is deployed)
2. Serial logs from ESP32 when you send RemoteStart/RemoteStop
3. Server logs showing the handlers accepting commands
