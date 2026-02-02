# Quick Reference - OCPP Server Integration
## Rivot Motors ESP32 DC Fast Charger

---

## 🎯 TL;DR - What You Need to Know

### Custom Messages (DataTransfer)
Your server needs to handle **2 custom messages** via OCPP 1.6 `DataTransfer`:

1. **VehicleInfo** - Initial vehicle data (ONCE per plug-in)
2. **SessionSummary** - Final session data (ONCE per transaction)

Both use `vendorId: "RivotMotors"`

---

## 📨 Message 1: VehicleInfo

### When Received
- Vehicle plugged in (Preparing state)
- BEFORE StartTransaction
- ONCE per plug-in event

### Example
```json
{
  "vendorId": "RivotMotors",
  "messageId": "VehicleInfo",
  "data": "{\"soc\":87.0,\"maxCurrent\":90.0,\"voltage\":76.4,\"current\":0.0,\"temperature\":26.5}"
}
```

### Parsed Data
```javascript
{
  soc: 87.0,          // Initial SOC (%)
  maxCurrent: 90.0,   // BMS max current (A)
  voltage: 76.4,      // Battery voltage (V)
  current: 0.0,       // Current (A)
  temperature: 26.5   // Battery temp (°C)
}
```

### What to Do
1. Parse `data` field as JSON string
2. Store in database
3. Display in UI: "Vehicle connected: 87% SOC, 90A max"
4. Respond with `{ status: "Accepted" }`

---

## 📨 Message 2: SessionSummary

### When Received
- After RemoteStopTransaction
- BEFORE StopTransaction
- ONCE per transaction

### Example
```json
{
  "vendorId": "RivotMotors",
  "messageId": "SessionSummary",
  "data": "{\"finalSoc\":87.18,\"energyDelivered\":3.39,\"durationMinutes\":1.50}"
}
```

### Parsed Data
```javascript
{
  finalSoc: 87.18,           // Final SOC (%)
  energyDelivered: 3.39,     // Total energy (Wh)
  durationMinutes: 1.50      // Duration (min)
}
```

### What to Do
1. Parse `data` field as JSON string
2. Store in database (link to transactionId)
3. Display in UI: "Session complete: 87.18% final, 3.39 Wh"
4. Respond with `{ status: "Accepted" }`

---

## 📊 Standard OCPP Messages

### MeterValues (Every 10 seconds)
```javascript
{
  connectorId: 1,
  transactionId: 130,
  meterValue: [{
    timestamp: "2026-01-21T09:52:30Z",
    sampledValue: [
      { measurand: "Energy.Active.Import.Register", value: "0", unit: "Wh" },
      { measurand: "Power.Active.Import", value: "142", unit: "W" },
      { measurand: "SoC", value: "87.07", unit: "Percent" },
      { measurand: "Voltage", value: "77.15", unit: "V" },
      { measurand: "Current.Import", value: "1.85", unit: "A" }
    ]
  }]
}
```

**Use for:** Live charging telemetry (updates every 10s)

---

## 🔄 Complete Flow

```
1. StatusNotification (Preparing)
2. DataTransfer (VehicleInfo) ← Parse this
3. RemoteStartTransaction
4. StartTransaction
5. StatusNotification (Charging)
6. MeterValues (every 10s) ← Use for live UI
7. RemoteStopTransaction
8. DataTransfer (SessionSummary) ← Parse this
9. StatusNotification (Finishing)
10. StopTransaction
```

---

## 💻 Code Example (Pseudo)

```javascript
// Handle DataTransfer
function handleDataTransfer(message) {
  if (message.vendorId !== "RivotMotors") {
    return { status: "UnknownVendorId" };
  }
  
  // Parse data field (it's a JSON STRING)
  const data = JSON.parse(message.data);
  
  if (message.messageId === "VehicleInfo") {
    // Store initial vehicle info
    database.insert('vehicle_info', {
      chargeBoxId: message.chargeBoxId,
      timestamp: new Date(),
      initialSoc: data.soc,
      maxCurrent: data.maxCurrent,
      voltage: data.voltage,
      temperature: data.temperature
    });
    
    // Update UI
    ui.showVehicleConnected(data.soc, data.maxCurrent);
    
  } else if (message.messageId === "SessionSummary") {
    // Store session summary
    database.insert('session_summary', {
      transactionId: getCurrentTransactionId(),
      timestamp: new Date(),
      finalSoc: data.finalSoc,
      energyDelivered: data.energyDelivered,
      durationMinutes: data.durationMinutes
    });
    
    // Update UI
    ui.showSessionComplete(data.finalSoc, data.energyDelivered);
  }
  
  return { status: "Accepted" };
}
```

---

## ⚠️ Important Notes

1. **`data` field is a STRING, not object** - Must parse as JSON
2. **VehicleInfo is ONE-SHOT** - Don't expect it every time
3. **MeterValues interval is 10s** - Not 30s (DC fast charging optimized)
4. **SessionSummary comes BEFORE StopTransaction** - Handle async
5. **Always respond with `{ status: "Accepted" }`** - Or charger will retry

---

## 🎨 UI Recommendations

### Preparing State (VehicleInfo)
```
┌─────────────────────────┐
│ Vehicle Connected       │
│ SOC: 87.0%             │
│ Max: 90.0 A            │
│ [Start Charging]       │
└─────────────────────────┘
```

### Charging State (MeterValues)
```
┌─────────────────────────┐
│ Charging Active         │
│ SOC: 87.18% ▲          │
│ Power: 142 W           │
│ [███████░░░] 87%       │
│ [Stop Charging]        │
└─────────────────────────┘
```

### Complete State (SessionSummary)
```
┌─────────────────────────┐
│ Session Complete ✓      │
│ Final: 87.18%          │
│ Energy: 3.39 Wh        │
│ Time: 1:30             │
└─────────────────────────┘
```

---

## 🔍 Testing

### Test VehicleInfo
1. Unplug gun
2. Plug gun back in
3. Wait for StatusNotification (Preparing)
4. Expect DataTransfer (VehicleInfo)

### Test SessionSummary
1. Start charging (RemoteStartTransaction)
2. Wait 1 minute
3. Stop charging (RemoteStopTransaction)
4. Expect DataTransfer (SessionSummary)
5. Then expect StopTransaction

---

## 📞 Questions?

**Email:** support@rivotmotors.com  
**Full Docs:** `SERVER_INTEGRATION_GUIDE.md`

---

**Version:** 1.0 | **Date:** January 2026
