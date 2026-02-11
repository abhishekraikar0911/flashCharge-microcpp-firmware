# CitrineOS Migration Guide

## ✅ Changes Made

### 1. WebSocket URL Updated
**Old (SteVe):**
```
ws://ocpp.rivotmotors.com:8080/steve/websocket/CentralSystemService/250822008C06
```

**New (CitrineOS):**
```
ws://103.174.148.201:8092/250822008C06
```

### 2. Configuration Changes
- **Host**: `103.174.148.201`
- **Port**: `8092`
- **Path**: `/<stationId>` (no extra paths)
- **Security Profile**: 0 (no TLS)
- **Heartbeat Interval**: 60 seconds

## 📋 What Was Changed

### File: `include/secrets.h`
```cpp
#define SECRET_CSMS_HOST "103.174.148.201"
#define SECRET_CSMS_PORT 8092
#define SECRET_CSMS_URL "ws://103.174.148.201:8092/" SECRET_CHARGER_ID
```

## ✅ What Stays the Same

- BootNotification payload (already compliant)
- MeterValues format (OCPP 1.6 standard measurands)
- Transaction logic (StartTransaction/StopTransaction)
- Authorization handling
- All hardware drivers and CAN bus logic

## 🧪 Testing Steps

1. **Build and flash:**
```bash
pio run -e charger_esp32_production --target upload
pio device monitor --baud 115200
```

2. **Expected boot sequence:**
```
[OCPP] 🔌 Initializing OCPP...
[OCPP] ✅ WiFi connected
[OCPP] 🚀 Calling mocpp_initialize()...
[OCPP] ✅ mocpp_initialize() completed
[OCPP] ✅ OCPP initialization complete
[OCPP] ⏳ Waiting for WebSocket connection and BootNotification...
```

3. **Check server logs:**
```bash
docker logs -f server-citrine-1
```

Expected output:
```
BootNotification received
BootNotification accepted
StatusNotification Available
Heartbeat
```

## 🔄 Rollback Plan

If connection fails, revert `secrets.h`:
```cpp
#define SECRET_CSMS_HOST "ocpp.rivotmotors.com"
#define SECRET_CSMS_PORT 8080
#define SECRET_CSMS_URL "ws://ocpp.rivotmotors.com:8080/steve/websocket/CentralSystemService/" SECRET_CHARGER_ID
```

## 📊 Verification Checklist

- [ ] WebSocket connects to CitrineOS
- [ ] BootNotification accepted
- [ ] StatusNotification shows "Available"
- [ ] Heartbeat every 60 seconds
- [ ] RemoteStartTransaction works
- [ ] MeterValues sent during charging
- [ ] RemoteStopTransaction works
- [ ] No duplicate transactions

## 🎯 Next Steps

After 3-5 successful charging sessions:
1. Confirm transaction persistence
2. Test WiFi reconnection
3. Test charger module offline/online
4. Plan OCPP 2.0.1 upgrade

---

**Status**: ✅ Ready for CitrineOS | **Date**: January 2025
