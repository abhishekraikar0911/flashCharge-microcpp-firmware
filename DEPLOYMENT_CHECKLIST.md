# CitrineOS Migration - Final Pre-Deployment Checks

## ✅ Configuration Verified

### 1. WebSocket Subprotocol
**Status**: ✅ VERIFIED
- MicroOCPP uses standard `ocpp1.6` subprotocol (library default)
- No custom subprotocol set in code
- No SteVe-specific naming found
- **Action**: None required

### 2. Local Authorization Cache
**Status**: ✅ VERIFIED
- No `setLocalAuthListEnabled()` calls found in codebase
- Local auth cache is DISABLED by default
- All authorization flows through CitrineOS server
- **Action**: None required (clean CSMS authorization)

### 3. WebSocket URL
**Status**: ✅ VERIFIED
```cpp
ws://103.174.148.201:8092/250822008C06
```
- No `/steve/websocket/CentralSystemService/` path
- Clean CitrineOS format
- Single source of truth (uses SECRET_CSMS_HOST macro)

### 4. BootNotification Payload
**Status**: ✅ VERIFIED
```json
{
  "chargePointVendor": "Rivot Motors",
  "chargePointModel": "Rivot Charger",
  "chargePointSerialNumber": "250822008C06",
  "firmwareVersion": "2.4.0"
}
```
- All required fields present
- Non-empty values
- Firmware version from version.h

### 5. OCPP Configuration
**Status**: ✅ VERIFIED
- HeartbeatInterval: 60s (standard OCPP 1.6)
- MeterValueSampleInterval: 5s
- Security Profile: 0 (no TLS)
- MeterValues: Standard measurands only

---

## 🚀 Deployment Steps

### Step 1: Flash Firmware (2 minutes)
```bash
cd c:\Users\AKSHAY\Documents\PlatformIO\Projects\microocpp
pio run -e charger_esp32_production --target upload
```

### Step 2: Monitor Serial Output (3 minutes)
```bash
pio device monitor --baud 115200
```

**Expected Output:**
```
========================================
  ESP32 OCPP EVSE Controller - v2.4.0
  StationId: 250822008C06
  CSMS: 103.174.148.201:8092
========================================

[OCPP] 🔌 Initializing OCPP...
[OCPP] 📍 StationId: 250822008C06
[OCPP] 🌐 Server: ws://103.174.148.201:8092/250822008C06
[OCPP] ✅ WiFi connected
[OCPP] 🚀 Calling mocpp_initialize()...
[OCPP] ✅ OCPP initialization complete
[OCPP] Connection status changed: CONNECTED
```

### Step 3: Verify on CitrineOS Server (1 minute)
```bash
docker logs -f server-citrine-1 | grep 250822008C06
```

**Expected Output (in order):**
```
BootNotification received from 250822008C06
BootNotification accepted
StatusNotification: Available
Heartbeat received
```

---

## ✅ Success Criteria

### Immediate (within 30 seconds)
- [ ] WiFi connected
- [ ] WebSocket connected to CitrineOS
- [ ] BootNotification sent
- [ ] BootNotification accepted
- [ ] StatusNotification: Available

### Short-term (within 2 minutes)
- [ ] First Heartbeat received
- [ ] Connector status: Available
- [ ] No error messages in logs
- [ ] No connection retries

### Medium-term (within 10 minutes)
- [ ] Heartbeat every 60 seconds
- [ ] No disconnections
- [ ] Stable connection

---

## 🧪 First Transaction Test

### Test Scenario: RemoteStartTransaction
1. **Plug in vehicle** (wait for "Preparing" state)
2. **Send RemoteStartTransaction** from CitrineOS dashboard
3. **Verify charging starts**

**Expected Serial Output:**
```
[PLUG] 🔌 Gun plugged, vehicle detected
[OCPP] 📥 RemoteStart received
[OCPP] ✅ RemoteStart accepted
>>> CONTACTOR ON <<<
[OCPP] ▶️  Transaction STARTED - Charging ENABLED (txId=1)
[GATE] ✅ HARD GATE OPEN
[OCPP] 📊 MeterValues will be sent automatically every 5s
```

**Expected Server Logs:**
```
StartTransaction received (txId=1)
MeterValues: Energy=0Wh, Voltage=72.5V, Current=45.2A, SoC=65%
MeterValues: Energy=250Wh, Voltage=72.3V, Current=44.8A, SoC=66%
```

### Test Scenario: RemoteStopTransaction
1. **Send RemoteStopTransaction** from CitrineOS
2. **Verify charging stops**

**Expected Serial Output:**
```
[OCPP] 📥 RemoteStop received
[OCPP] ⏹️  Charging disabled
[OCPP] ⏹️  Transaction STOPPED and UNLOCKED
[GATE] 🔒 HARD GATE CLOSED
```

**Expected Server Logs:**
```
StopTransaction received (txId=1, energy=1250Wh)
StatusNotification: Available
```

---

## 🚨 Troubleshooting

### Issue: WebSocket Connection Failed
**Symptoms:**
```
[OCPP] Connection status changed: DISCONNECTED
```

**Checks:**
1. Verify CitrineOS server is running: `docker ps | grep citrine`
2. Check firewall allows port 8092
3. Verify StationId registered in CitrineOS
4. Check network connectivity: `ping 103.174.148.201`

**Fix:**
- Restart CitrineOS: `docker restart server-citrine-1`
- Check server logs: `docker logs server-citrine-1`

---

### Issue: BootNotification Rejected
**Symptoms:**
```
BootNotification rejected
```

**Checks:**
1. Verify StationId matches CitrineOS registration
2. Check vendor/model fields are non-empty
3. Verify firmware version format

**Fix:**
- Re-register charger in CitrineOS with exact StationId: `250822008C06`

---

### Issue: No MeterValues During Charging
**Symptoms:**
```
[Metrics] V=0.0V I=0.0A
```

**Checks:**
1. Verify CAN bus connection (BMS + Charger module)
2. Check terminal voltage/current CAN messages (ID 0x00433F01)
3. Verify charger module is online

**Fix:**
- Check CAN wiring and termination resistors
- Verify charger module power supply

---

## 📊 Migration Status

### Pre-Deployment
- [x] WebSocket URL updated
- [x] Subprotocol verified (standard ocpp1.6)
- [x] Local auth cache disabled
- [x] BootNotification fields verified
- [x] Configuration validated
- [x] Documentation created

### Deployment
- [ ] Firmware flashed
- [ ] Serial output verified
- [ ] CitrineOS connection confirmed
- [ ] BootNotification accepted
- [ ] First Heartbeat received

### Post-Deployment
- [ ] First transaction completed
- [ ] MeterValues flowing correctly
- [ ] WiFi reconnection tested
- [ ] Power cycle recovery tested
- [ ] 24-hour stability confirmed

---

## 🔄 Rollback Plan

If any critical issue occurs:

1. **Revert secrets.h:**
```cpp
#define SECRET_CSMS_HOST "ocpp.rivotmotors.com"
#define SECRET_CSMS_PORT 8080
#define SECRET_CSMS_URL "ws://" SECRET_CSMS_HOST ":8080/steve/websocket/CentralSystemService/" SECRET_CHARGER_ID
```

2. **Reflash:**
```bash
pio run -e charger_esp32_production --target upload
```

3. **Verify SteVe connection:**
```bash
# Check SteVe server logs
```

**Rollback Time**: < 5 minutes
**Data Loss**: None (transaction persistence maintained)

---

## 🎯 Next Steps After Success

1. ✅ Monitor first 3-5 charging sessions
2. ✅ Verify transaction persistence after power cycle
3. ✅ Test WiFi reconnection during charging
4. ✅ Confirm no duplicate transactions
5. ✅ Run 24-hour stability test
6. 🔜 Migrate remaining chargers (one at a time)
7. 🔜 Plan OCPP 2.0.1 upgrade

---

## 📝 Notes

- **Migration Type**: OCPP 1.6J → OCPP 1.6J (server change only)
- **Downtime**: < 2 minutes (firmware flash time)
- **Risk Level**: Low (URL change only, easy rollback)
- **Testing Required**: 3-5 full charging sessions
- **Production Ready**: Yes (after successful testing)

---

**Status**: ✅ Ready for Deployment | **Date**: January 2025 | **Charger**: 250822008C06
