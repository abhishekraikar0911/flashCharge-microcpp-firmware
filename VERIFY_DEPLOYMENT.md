# CitrineOS Migration - Verification Script

## 🔍 Step-by-Step Verification

### Step 1: Clean Flash (MANDATORY)
```bash
cd c:\Users\AKSHAY\Documents\PlatformIO\Projects\microocpp

# Erase old firmware
pio run -e charger_esp32_production -t erase

# Clean build cache
pio run -e charger_esp32_production -t clean

# Build and upload fresh
pio run -e charger_esp32_production -t upload

# Monitor serial
pio device monitor --baud 115200
```

---

### Step 2: Verify Boot Output

**Expected Serial Output:**
```
========================================
  ESP32 OCPP EVSE Controller - v2.4.0
  Production-Ready Edition
  Build: Jan 15 2025 14:30:00
  StationId: 250822008C06
  CSMS: 103.174.148.201:8092
========================================

🔍 DEBUG: Configuration Verification
  CSMS_HOST = 103.174.148.201
  CSMS_PORT = 8092
  CSMS_URL  = ws://103.174.148.201:8092/250822008C06
  CHARGER_ID = 250822008C06
========================================

[OCPP] 🔌 Initializing OCPP...
[OCPP] 📍 StationId: 250822008C06
[OCPP] 🌐 Server: ws://103.174.148.201:8092/250822008C06
```

---

### Step 3: Check for SteVe Path (CRITICAL)

**❌ FAIL - If you see:**
```
CSMS_URL = ws://ocpp.rivotmotors.com:8080/steve/websocket/CentralSystemService/250822008C06
```
→ Old firmware still running, repeat Step 1

**✅ PASS - If you see:**
```
CSMS_URL = ws://103.174.148.201:8092/250822008C06
```
→ Correct firmware loaded

---

### Step 4: Verify CitrineOS Connection

**Terminal 1 - Serial Monitor:**
```
[OCPP] ✅ WiFi connected
[OCPP] 🚀 Calling mocpp_initialize()...
[OCPP] ✅ mocpp_initialize() completed
[OCPP] Connection status changed: CONNECTED
```

**Terminal 2 - CitrineOS Logs:**
```bash
docker logs -f server-citrine-1 | grep 250822008C06
```

**❌ FAIL - If you see:**
```
GET /steve/websocket/CentralSystemService/250822008C06
```
→ Wrong firmware, repeat Step 1 with longer erase time

**✅ PASS - If you see:**
```
WebSocket connection established for stationId 250822008C06
BootNotification received from 250822008C06
BootNotification accepted
StatusNotification: Available
Heartbeat received
```

---

### Step 5: Verify BootNotification Payload

**Expected in CitrineOS logs:**
```json
{
  "chargePointVendor": "Rivot Motors",
  "chargePointModel": "Rivot Charger",
  "chargePointSerialNumber": "250822008C06",
  "firmwareVersion": "2.4.0"
}
```

---

## 🚨 Troubleshooting

### Issue: Still seeing SteVe path after clean flash

**Cause**: Build cache not cleared or wrong environment

**Fix**:
```bash
# Delete entire build directory
rmdir /s /q .pio\build

# Rebuild from scratch
pio run -e charger_esp32_production -t upload
```

---

### Issue: Upload fails

**Symptoms:**
```
Failed to connect to ESP32
```

**Fix**:
1. Press and hold BOOT button on ESP32
2. Press RESET button
3. Release RESET, then release BOOT
4. Retry upload

---

### Issue: Serial monitor shows garbage

**Cause**: Wrong baud rate

**Fix**:
```bash
pio device monitor --baud 115200 --raw
```

---

## ✅ Success Checklist

- [ ] Clean flash completed (erase + clean + upload)
- [ ] Boot banner shows CitrineOS URL
- [ ] DEBUG section shows correct CSMS_URL
- [ ] OCPP init shows correct Server URL
- [ ] CitrineOS logs show BootNotification (not GET request)
- [ ] StatusNotification: Available
- [ ] Heartbeat received every 60s

---

## 📊 Expected Timeline

- **0-5s**: Boot + WiFi connect
- **5-10s**: OCPP initialize
- **10-15s**: WebSocket connect
- **15-20s**: BootNotification sent
- **20-25s**: BootNotification accepted
- **60s**: First Heartbeat

---

## 🎯 Next Steps After Verification

1. ✅ Leave running for 5 minutes (verify stable connection)
2. ✅ Test RemoteStartTransaction
3. ✅ Verify MeterValues during charging
4. ✅ Test RemoteStopTransaction
5. ✅ Power cycle test (transaction persistence)

---

**Status**: Ready for Verification | **Date**: January 2025
