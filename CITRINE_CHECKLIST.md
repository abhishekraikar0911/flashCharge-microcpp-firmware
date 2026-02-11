# CitrineOS Migration - Production Checklist

## ✅ Changes Completed

### 1. WebSocket URL (MANDATORY)
```cpp
// OLD (SteVe)
ws://ocpp.rivotmotors.com:8080/steve/websocket/CentralSystemService/250822008C06

// NEW (CitrineOS)
ws://103.174.148.201:8092/250822008C06
```

### 2. Boot Logging Enhanced
- StationId logged on boot: `250822008C06`
- CSMS server logged: `103.174.148.201:8092`
- Full WebSocket URL logged for debugging

## 🔍 Pre-Deployment Verification

### Check 1: WebSocket Subprotocol
✅ MicroOCPP uses standard `ocpp1.6` (no SteVe-specific naming)
✅ No custom subprotocol forced

### Check 2: StationId Case Sensitivity
✅ StationId: `250822008C06` (exact match required)
✅ Logged on every boot for debugging

### Check 3: BootNotification Fields
✅ `chargePointVendor`: "Rivot Motors"
✅ `chargePointModel`: "Rivot Charger"
✅ `firmwareVersion`: Set in version.h
✅ `reason`: "PowerUp" (MicroOCPP default)

### Check 4: Local Authorization
✅ Local auth cache NOT enabled (clean migration)
✅ All authorization via CitrineOS server

### Check 5: Configuration
✅ HeartbeatInterval: 60s (standard OCPP 1.6)
✅ MeterValueSampleInterval: 5s
✅ Security Profile: 0 (no TLS)

## 🧪 Testing Protocol

### Phase 1: Connection Test (5 minutes)
```bash
# Flash firmware
pio run -e charger_esp32_production --target upload

# Monitor serial
pio device monitor --baud 115200
```

**Expected Serial Output:**
```
========================================
  ESP32 OCPP EVSE Controller - v2.x.x
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

**Expected Server Logs:**
```bash
docker logs -f server-citrine-1 | grep 250822008C06
```
```
BootNotification received from 250822008C06
BootNotification accepted
StatusNotification: Available
Heartbeat received
```

**✅ PASS**: Connection established
**❌ FAIL**: Check firewall, StationId registration

---

### Phase 2: Transaction Test (30 minutes)

#### Test 2.1: RemoteStartTransaction
1. Plug in vehicle (wait for "Preparing" state)
2. Send RemoteStartTransaction from CitrineOS
3. Verify charging starts

**Expected Serial Output:**
```
[OCPP] 📥 RemoteStart received
[OCPP] ✅ RemoteStart accepted
>>> CONTACTOR ON <<<
[OCPP] ▶️  Transaction STARTED - Charging ENABLED (txId=1)
[GATE] ✅ HARD GATE OPEN
```

**Expected Server Logs:**
```
StartTransaction received (txId=1)
MeterValues received (Energy, Voltage, Current, SoC)
```

**✅ PASS**: Charging active, MeterValues flowing
**❌ FAIL**: Check BMS connection, charger module health

#### Test 2.2: MeterValues During Charging
Wait 30 seconds, verify MeterValues sent every 5s

**Expected Serial Output:**
```
[Metrics] V=72.5V I=45.2A SOC=65.3% Energy=1250Wh
```

**Expected Server Logs:**
```
MeterValues: Energy=1250Wh, Voltage=72.5V, Current=45.2A, SoC=65.3%
```

**✅ PASS**: Real-time data flowing
**❌ FAIL**: Check terminal voltage/current CAN messages

#### Test 2.3: RemoteStopTransaction
1. Send RemoteStopTransaction from CitrineOS
2. Verify charging stops

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

**✅ PASS**: Transaction completed cleanly
**❌ FAIL**: Check transaction persistence

---

### Phase 3: Recovery Test (15 minutes)

#### Test 3.1: WiFi Reconnection
1. Disconnect WiFi router
2. Wait 30 seconds
3. Reconnect WiFi

**Expected Serial Output:**
```
[WIFI] ❌ Network connection lost!
[WIFI] ✅ Network connection restored!
[OCPP] Connection status changed: CONNECTED
```

**✅ PASS**: Auto-reconnect works
**❌ FAIL**: Check WiFi credentials

#### Test 3.2: Power Cycle During Charging
1. Start charging session
2. Power off ESP32
3. Power on ESP32
4. Verify transaction resumes

**Expected Serial Output:**
```
[PERSIST] Restored transaction: 1
[OCPP] ▶️  Transaction STARTED - Charging ENABLED (txId=1)
```

**✅ PASS**: Transaction persistence works
**❌ FAIL**: Check NVS flash initialization

---

## 🚫 Known Issues to Avoid

### Issue 1: Duplicate Transactions
**Symptom**: Multiple StartTransaction for same session
**Cause**: Transaction not properly locked
**Status**: ✅ Fixed with `transactionLocked` flag

### Issue 2: Negative Energy Values
**Symptom**: MeterValues shows negative Wh
**Cause**: Energy accumulation without validation
**Status**: ✅ Fixed with non-negative validation

### Issue 3: Charger Offline Not Detected
**Symptom**: Transaction continues when charger module offline
**Cause**: No CAN timeout monitoring
**Status**: ✅ Fixed with `isChargerModuleHealthy()`

---

## 📊 Success Criteria

- [ ] BootNotification accepted within 10 seconds
- [ ] StatusNotification shows "Available"
- [ ] Heartbeat every 60 seconds
- [ ] RemoteStartTransaction works
- [ ] MeterValues sent every 5 seconds during charging
- [ ] RemoteStopTransaction works
- [ ] No duplicate transactions
- [ ] WiFi auto-reconnect works
- [ ] Transaction persistence after power cycle

---

## 🔄 Rollback Plan

If any test fails:

1. **Revert secrets.h:**
```cpp
#define SECRET_CSMS_HOST "ocpp.rivotmotors.com"
#define SECRET_CSMS_PORT 8080
#define SECRET_CSMS_URL "ws://ocpp.rivotmotors.com:8080/steve/websocket/CentralSystemService/" SECRET_CHARGER_ID
```

2. **Reflash firmware:**
```bash
pio run -e charger_esp32_production --target upload
```

3. **No firmware rollback needed** - only URL changed

---

## 🎯 Next Steps After Success

1. ✅ Migrate 1 charger (pilot)
2. ✅ Run 3-5 full charging sessions
3. ✅ Monitor for 24 hours
4. ✅ Migrate remaining chargers
5. 🔜 Plan OCPP 2.0.1 upgrade

---

**Status**: ✅ Ready for Production | **Date**: January 2025
