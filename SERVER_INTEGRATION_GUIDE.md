# OCPP Server Integration Guide
## ESP32 DC Fast Charger - Custom Implementation

**Client ID:** RIVOT_100A_01  
**OCPP Version:** 1.6J  
**Protocol:** WebSocket (WS) / Secure WebSocket (WSS)  
**Firmware Version:** v2.0

---

## 📋 Overview

This document describes the OCPP 1.6 implementation of the Rivot Motors ESP32 DC Fast Charger, including standard OCPP messages and custom vendor-specific extensions via `DataTransfer`.

---

## 🔌 Connection Details

### WebSocket Endpoint
```
ws://your-server.com:8080/steve/websocket/CentralSystemService/{chargeBoxId}
```

### Connection Parameters
- **Protocol:** OCPP 1.6J
- **Transport:** WebSocket (RFC 6455)
- **Heartbeat Interval:** 60 seconds (configurable via `HeartbeatInterval`)
- **MeterValues Interval:** 10 seconds (configurable via `MeterValueSampleInterval`)

### Boot Sequence
```
1. WebSocket connection established
2. BootNotification sent
3. Wait for BootNotification response (status: Accepted/Pending/Rejected)
4. Start normal operation
```

---

## 📊 Standard OCPP 1.6 Messages

### 1. BootNotification
**Direction:** Client → Server  
**Frequency:** On startup, on reconnect

```json
[2, "uuid", "BootNotification", {
  "chargePointModel": "Rivot Charger",
  "chargePointVendor": "Rivot Motors"
}]
```

**Expected Response:**
```json
[3, "uuid", {
  "status": "Accepted",
  "currentTime": "2026-01-21T09:51:06.209Z",
  "interval": 14400
}]
```

---

### 2. StatusNotification
**Direction:** Client → Server  
**Frequency:** On state change

#### Connector 0 (Charge Point)
```json
[2, "uuid", "StatusNotification", {
  "connectorId": 0,
  "errorCode": "NoError",
  "status": "Available",
  "timestamp": "2026-01-21T09:51:06Z"
}]
```

#### Connector 1 (Charging Connector)
**Possible States:**
- `Available` - Ready for use, no vehicle connected
- `Preparing` - Vehicle connected, waiting for authorization
- `Charging` - Active charging session
- `Finishing` - Charging stopped, waiting for vehicle disconnect

```json
[2, "uuid", "StatusNotification", {
  "connectorId": 1,
  "errorCode": "NoError",
  "status": "Preparing",
  "timestamp": "2026-01-21T09:51:06Z"
}]
```

---

### 3. StartTransaction
**Direction:** Client → Server  
**Frequency:** On charging start (after RemoteStartTransaction or local authorization)

```json
[2, "uuid", "StartTransaction", {
  "connectorId": 1,
  "idTag": "TEST_TAG",
  "meterStart": 0,
  "timestamp": "2026-01-21T09:52:20Z"
}]
```

**Expected Response:**
```json
[3, "uuid", {
  "transactionId": 130,
  "idTagInfo": {
    "status": "Accepted",
    "expiryDate": "2026-09-25T00:00:00.000Z"
  }
}]
```

---

### 4. MeterValues
**Direction:** Client → Server  
**Frequency:** Every 10 seconds during charging

```json
[2, "uuid", "MeterValues", {
  "connectorId": 1,
  "transactionId": 130,
  "meterValue": [{
    "timestamp": "2026-01-21T09:52:30Z",
    "sampledValue": [
      {
        "value": "0",
        "context": "Sample.Periodic",
        "measurand": "Energy.Active.Import.Register",
        "unit": "Wh"
      },
      {
        "value": "142.00",
        "context": "Sample.Periodic",
        "measurand": "Power.Active.Import",
        "unit": "W"
      },
      {
        "value": "87.07",
        "context": "Sample.Periodic",
        "measurand": "SoC",
        "unit": "Percent"
      },
      {
        "value": "77.15",
        "context": "Sample.Periodic",
        "measurand": "Voltage",
        "unit": "V"
      },
      {
        "value": "1.85",
        "context": "Sample.Periodic",
        "measurand": "Current.Import",
        "unit": "A"
      }
    ]
  }]
}]
```

**Measurands Included:**
| Measurand | Unit | Description | Frequency |
|-----------|------|-------------|-----------|
| Energy.Active.Import.Register | Wh | Cumulative energy delivered | Every 10s |
| Power.Active.Import | W | Instantaneous power | Every 10s |
| SoC | Percent | State of Charge | Every 10s |
| Voltage | V | Terminal voltage | Every 10s |
| Current.Import | A | Charging current | Every 10s |

**Expected Response:**
```json
[3, "uuid", {}]
```

---

### 5. StopTransaction
**Direction:** Client → Server  
**Frequency:** On charging stop (after RemoteStopTransaction or local stop)

```json
[2, "uuid", "StopTransaction", {
  "meterStop": 3,
  "timestamp": "2026-01-21T09:53:50Z",
  "transactionId": 130,
  "reason": "Remote"
}]
```

**Stop Reasons:**
- `Remote` - Stopped via RemoteStopTransaction
- `Local` - Stopped by user
- `EVDisconnected` - Vehicle unplugged
- `EmergencyStop` - Emergency stop activated
- `Other` - Other reasons

**Expected Response:**
```json
[3, "uuid", {}]
```

---

### 6. Heartbeat
**Direction:** Client → Server  
**Frequency:** Every 60 seconds (configurable)

```json
[2, "uuid", "Heartbeat", {}]
```

**Expected Response:**
```json
[3, "uuid", {
  "currentTime": "2026-01-21T09:52:00Z"
}]
```

---

## 🎯 Custom DataTransfer Messages (Vendor-Specific)

### Overview
The charger uses OCPP 1.6 `DataTransfer` for vendor-specific data that doesn't fit standard OCPP messages.

**Vendor ID:** `RivotMotors`

---

### 1. VehicleInfo (Pre-Transaction Context)

**Purpose:** Send initial vehicle information when vehicle is connected but before charging starts.

**When Sent:**
- ✅ ONCE when vehicle is first detected (gun plugged + BMS connected)
- ✅ ONLY in `Preparing` state (before StartTransaction)
- ❌ NEVER during charging
- ❌ NEVER after transaction starts

**Message:**
```json
[2, "uuid", "DataTransfer", {
  "vendorId": "RivotMotors",
  "messageId": "VehicleInfo",
  "data": "{\"soc\":87.00,\"maxCurrent\":90.0,\"voltage\":76.38,\"current\":0.0,\"temperature\":26.5}"
}]
```

**Data Payload (JSON string):**
```json
{
  "soc": 87.00,           // Initial State of Charge (%)
  "maxCurrent": 90.0,     // BMS maximum current limit (A)
  "voltage": 76.38,       // Battery voltage (V)
  "current": 0.0,         // Current (A) - usually 0 before charging
  "temperature": 26.5     // Battery temperature (°C)
}
```

**Expected Response:**
```json
[3, "uuid", {
  "status": "Accepted"
}]
```

**Server Action:**
- Store initial vehicle context
- Display in UI: "Vehicle connected: 87% SOC, 90A max"
- Prepare charging session UI
- Do NOT use this for live telemetry (use MeterValues instead)

**Frequency:** ONE-SHOT per plug-in event

---

### 2. SessionSummary (Post-Transaction Summary)

**Purpose:** Send final session summary after charging stops.

**When Sent:**
- ✅ ONCE when RemoteStopTransaction is received
- ✅ BEFORE StopTransaction message
- ✅ ONLY at end of transaction

**Message:**
```json
[2, "uuid", "DataTransfer", {
  "vendorId": "RivotMotors",
  "messageId": "SessionSummary",
  "data": "{\"finalSoc\":87.18,\"energyDelivered\":3.39,\"durationMinutes\":1.50}"
}]
```

**Data Payload (JSON string):**
```json
{
  "finalSoc": 87.18,           // Final State of Charge (%)
  "energyDelivered": 3.39,     // Total energy delivered (Wh)
  "durationMinutes": 1.50      // Session duration (minutes)
}
```

**Expected Response:**
```json
[3, "uuid", {
  "status": "Accepted"
}]
```

**Server Action:**
- Store session summary
- Display in UI: "Session complete: 87.18% final SOC, 3.39 Wh delivered, 1.5 min"
- Use for billing/reporting
- Close transaction UI

**Frequency:** ONE-SHOT per transaction

---

## 📈 Message Flow - Complete Charging Session

### Phase 1: Vehicle Connection (Preparing)
```
1. StatusNotification (Preparing)
2. DataTransfer (VehicleInfo) ← CUSTOM
   └─ UI shows: "Vehicle connected: 87% SOC, 90A max"
```

### Phase 2: Charging (Active Transaction)
```
3. RemoteStartTransaction (from server)
4. StartTransaction (to server)
5. StatusNotification (Charging)
6. MeterValues (every 10s) ← LIVE TELEMETRY
   ├─ 09:52:30 - SOC: 87.07%, Power: 142W
   ├─ 09:52:40 - SOC: 87.07%, Power: 142W
   ├─ 09:52:50 - SOC: 87.07%, Power: 141W
   ├─ 09:53:00 - SOC: 87.12%, Power: 143W
   ├─ 09:53:10 - SOC: 87.12%, Power: 143W
   ├─ 09:53:20 - SOC: 87.12%, Power: 142W
   ├─ 09:53:30 - SOC: 87.18%, Power: 144W
   └─ 09:53:40 - SOC: 87.18%, Power: 140W
```

### Phase 3: Charging Stop (Finishing)
```
7. RemoteStopTransaction (from server)
8. DataTransfer (SessionSummary) ← CUSTOM
   └─ UI shows: "Session complete: 87.18% final, 3.39 Wh, 1.5 min"
9. StatusNotification (Finishing)
10. StopTransaction (to server)
```

---

## 🎨 UI/UX Recommendations

### Phase 1: Preparing State
**Display:**
```
┌─────────────────────────────────┐
│ Vehicle Connected               │
│                                 │
│ Initial SOC:     87.0%          │
│ Max Current:     90.0 A         │
│ Voltage:         76.4 V         │
│ Temperature:     26.5°C         │
│                                 │
│ [Start Charging]                │
└─────────────────────────────────┘
```
**Data Source:** VehicleInfo DataTransfer

---

### Phase 2: Charging State
**Display:**
```
┌─────────────────────────────────┐
│ Charging Active                 │
│                                 │
│ SOC:            87.18% ▲        │
│ Power:          142 W           │
│ Current:        1.85 A          │
│ Voltage:        77.4 V          │
│ Energy:         3.4 Wh          │
│                                 │
│ Duration:       1:30            │
│                                 │
│ [███████░░░] 87%                │
│                                 │
│ [Stop Charging]                 │
└─────────────────────────────────┘
```
**Data Source:** MeterValues (updates every 10s)

---

### Phase 3: Session Complete
**Display:**
```
┌─────────────────────────────────┐
│ Session Complete ✓              │
│                                 │
│ Final SOC:      87.18%          │
│ Energy:         3.39 Wh         │
│ Duration:       1:30            │
│                                 │
│ [View Details] [Close]          │
└─────────────────────────────────┘
```
**Data Source:** SessionSummary DataTransfer

---

## 🔧 Server Configuration Requirements

### 1. Accept Custom DataTransfer Messages
```java
// Pseudo-code
if (message.type == "DataTransfer" && 
    message.vendorId == "RivotMotors") {
    
    if (message.messageId == "VehicleInfo") {
        handleVehicleInfo(message.data);
    }
    else if (message.messageId == "SessionSummary") {
        handleSessionSummary(message.data);
    }
    
    return { status: "Accepted" };
}
```

### 2. Parse JSON Data Field
```java
// DataTransfer.data is a JSON STRING, not object
String dataStr = message.data;
JsonObject dataObj = parseJson(dataStr);

// VehicleInfo
float soc = dataObj.get("soc");
float maxCurrent = dataObj.get("maxCurrent");
float voltage = dataObj.get("voltage");
float current = dataObj.get("current");
float temperature = dataObj.get("temperature");

// SessionSummary
float finalSoc = dataObj.get("finalSoc");
float energyDelivered = dataObj.get("energyDelivered");
float durationMinutes = dataObj.get("durationMinutes");
```

### 3. Configure MeterValues Interval
**Default:** 10 seconds (optimized for DC fast charging)

To change, send `ChangeConfiguration`:
```json
[2, "uuid", "ChangeConfiguration", {
  "key": "MeterValueSampleInterval",
  "value": "10"
}]
```

**Recommended values:**
- 5s - Real-time feel (high network load)
- 10s - Smooth updates (recommended) ✅
- 30s - Slow updates (feels laggy)

---

## 📊 Database Schema Recommendations

### Table: vehicle_info
```sql
CREATE TABLE vehicle_info (
    id SERIAL PRIMARY KEY,
    charge_box_id VARCHAR(50),
    connector_id INT,
    timestamp TIMESTAMP,
    initial_soc FLOAT,
    max_current FLOAT,
    voltage FLOAT,
    current FLOAT,
    temperature FLOAT
);
```

### Table: session_summary
```sql
CREATE TABLE session_summary (
    id SERIAL PRIMARY KEY,
    transaction_id INT,
    charge_box_id VARCHAR(50),
    timestamp TIMESTAMP,
    final_soc FLOAT,
    energy_delivered FLOAT,
    duration_minutes FLOAT
);
```

---

## 🔍 Troubleshooting

### Issue: VehicleInfo not received
**Cause:** Gun was already plugged when charger booted  
**Solution:** VehicleInfo only sends on plug-in transition (unplugged → plugged)

### Issue: MeterValues too slow
**Cause:** MeterValueSampleInterval set to 30s  
**Solution:** Change to 10s via ChangeConfiguration

### Issue: SessionSummary not received
**Cause:** Old firmware (before v2.0)  
**Solution:** Update firmware to v2.0+

### Issue: DataTransfer rejected
**Cause:** Server doesn't recognize vendorId "RivotMotors"  
**Solution:** Add vendor whitelist in server config

---

## 📞 Support

**Vendor:** Rivot Motors  
**Email:** support@rivotmotors.com  
**Documentation:** See `PHASE_ARCHITECTURE.md` for detailed implementation

---

## 📝 Version History

| Version | Date | Changes |
|---------|------|---------|
| v2.0 | Jan 2026 | Added SessionSummary, 10s MeterValues, VehicleInfo one-shot |
| v1.0 | Dec 2025 | Initial OCPP 1.6 implementation |

---

## ✅ Compliance

- ✅ OCPP 1.6J compliant
- ✅ WebSocket RFC 6455
- ✅ JSON-RPC 2.0
- ✅ ISO 15118 ready (future)
- ✅ IEC 61851 compliant

---

**Last Updated:** January 2026  
**Document Version:** 1.0
