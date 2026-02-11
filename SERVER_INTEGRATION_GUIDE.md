# OCPP Alert System - Complete Server Integration Guide

## Overview

The charger sends **4 types of messages** to the OCPP server via DataTransfer:

1. **VehicleInfo** - Vehicle data before charging starts
2. **ChargerStatus** - Readiness check before user presses start
3. **SystemAlert** - Critical system faults (WiFi, charger, temperature, voltage, current)
4. **BMSAlert** - BMS safety alerts
5. **SessionSummary** - Charging session summary after stop

---

## Message Types & Formats

### 1. ChargerStatus (NEW - Pre-Charge Check)

**When Sent**: Every 5 seconds when vehicle is plugged in but NOT charging  
**Purpose**: Show user if charger is ready or has blocking issues  
**Timing**: BEFORE user presses "Start Charging" button

**OCPP Message**:
```json
{
  "messageType": 2,
  "messageId": "unique-id",
  "action": "DataTransfer",
  "payload": {
    "vendorId": "RivotMotors",
    "messageId": "ChargerStatus",
    "data": "{\"ready\":false,\"reason\":\"Charger module offline - check CAN bus connection\",\"timestamp\":12345}"
  }
}
```

**Parsed Data**:
```json
{
  "ready": false,
  "reason": "Charger module offline - check CAN bus connection",
  "timestamp": 12345
}
```

**Possible Reasons**:
- ✅ `"Charger ready - you can start charging"` (ready=true)
- ❌ `"Charger module offline - check CAN bus connection"` (ready=false)
- ❌ `"WiFi disconnected - check network connection"` (ready=false)
- ❌ `"BMS charging disabled - vehicle not ready"` (ready=false)
- ❌ `"Temperature too high: 75.2°C"` (ready=false)
- ❌ `"Voltage out of range: 90.5V"` (ready=false)

---

### 2. SystemAlert (Real-Time Faults)

**When Sent**: Immediately when fault occurs or recovers  
**Purpose**: Notify server of critical system issues  
**Timing**: Anytime during operation

**OCPP Message**:
```json
{
  "messageType": 2,
  "messageId": "unique-id",
  "action": "DataTransfer",
  "payload": {
    "vendorId": "RivotMotors",
    "messageId": "SystemAlert",
    "data": "{\"alertType\":\"CHARGER_OFFLINE\",\"message\":\"CAN communication timeout\",\"severity\":\"Critical\",\"timestamp\":12345}"
  }
}
```

**Parsed Data**:
```json
{
  "alertType": "CHARGER_OFFLINE",
  "message": "CAN communication timeout",
  "severity": "Critical",
  "timestamp": 12345
}
```

**Alert Types**:

| Alert Type | Severity | Meaning |
|------------|----------|---------|
| `CHARGER_OFFLINE` | Critical | Charger module not responding |
| `CHARGER_ONLINE` | Info | Charger module recovered |
| `WIFI_DISCONNECTED` | Critical | Network connection lost |
| `WIFI_RECONNECTED` | Info | Network connection restored |
| `TEMPERATURE_WARNING` | Warning | Temperature > 60°C |
| `TEMPERATURE_CRITICAL` | Critical | Temperature > 70°C |
| `TEMPERATURE_NORMAL` | Info | Temperature recovered |
| `VOLTAGE_FAULT` | Critical | Voltage out of range (50-90V) |
| `VOLTAGE_NORMAL` | Info | Voltage recovered |
| `OVERCURRENT` | Critical | Current > 320A |
| `CURRENT_NORMAL` | Info | Current recovered |

---

### 3. BMSAlert (BMS Safety)

**When Sent**: When BMS charging permission changes  
**Purpose**: Notify server of BMS safety status  
**Timing**: Anytime during operation

**OCPP Message**:
```json
{
  "messageType": 2,
  "messageId": "unique-id",
  "action": "DataTransfer",
  "payload": {
    "vendorId": "RivotMotors",
    "messageId": "BMSAlert",
    "data": "{\"alertType\":\"BMS_EMERGENCY_STOP\",\"message\":\"BMS disabled charging during transaction\",\"timestamp\":12345}"
  }
}
```

**Alert Types**:
- `BMS_CHARGING_DISABLED` - BMS not ready for charging
- `BMS_EMERGENCY_STOP` - BMS switched OFF during charging
- `BMS_CHARGING_ENABLED` - BMS ready for charging

---

### 4. VehicleInfo (Pre-Charge Data)

**When Sent**: Every 5 seconds when vehicle plugged in but NOT charging  
**Purpose**: Show vehicle data to user for charging cost calculation

**OCPP Message**:
```json
{
  "messageType": 2,
  "messageId": "unique-id",
  "action": "DataTransfer",
  "payload": {
    "vendorId": "RivotMotors",
    "messageId": "VehicleInfo",
    "data": "{\"soc\":98.2,\"maxCurrent\":32.0,\"model\":\"Pro\",\"range\":159.1}"
  }
}
```

---

### 5. SessionSummary (Post-Charge Data)

**When Sent**: Once after StopTransaction  
**Purpose**: Provide charging session summary

**OCPP Message**:
```json
{
  "messageType": 2,
  "messageId": "unique-id",
  "action": "DataTransfer",
  "payload": {
    "vendorId": "RivotMotors",
    "messageId": "SessionSummary",
    "data": "{\"finalSoc\":100.0,\"energyDelivered\":1250.5,\"durationMinutes\":15.5}"
  }
}
```

---

## Server Implementation (Node.js Example)

### 1. Handle DataTransfer Messages

```javascript
// OCPP WebSocket message handler
function handleOCPPMessage(chargePointId, message) {
  if (message.action === 'DataTransfer') {
    const { vendorId, messageId, data } = message.payload;
    
    if (vendorId === 'RivotMotors') {
      const parsedData = JSON.parse(data);
      
      switch (messageId) {
        case 'ChargerStatus':
          handleChargerStatus(chargePointId, parsedData);
          break;
        case 'SystemAlert':
          handleSystemAlert(chargePointId, parsedData);
          break;
        case 'BMSAlert':
          handleBMSAlert(chargePointId, parsedData);
          break;
        case 'VehicleInfo':
          handleVehicleInfo(chargePointId, parsedData);
          break;
        case 'SessionSummary':
          handleSessionSummary(chargePointId, parsedData);
          break;
      }
      
      // Send acknowledgment
      return {
        messageType: 3,
        messageId: message.messageId,
        payload: { status: 'Accepted' }
      };
    }
  }
}
```

### 2. Handle ChargerStatus (Pre-Charge Check)

```javascript
function handleChargerStatus(chargePointId, data) {
  const { ready, reason, timestamp } = data;
  
  // Store in database
  db.chargerStatus.upsert({
    chargePointId,
    ready,
    reason,
    timestamp: new Date(timestamp),
    updatedAt: new Date()
  });
  
  // Send to user's mobile app via push notification or WebSocket
  if (!ready) {
    sendToUser(chargePointId, {
      type: 'CHARGER_NOT_READY',
      title: 'Cannot Start Charging',
      message: reason,
      severity: 'warning'
    });
  } else {
    sendToUser(chargePointId, {
      type: 'CHARGER_READY',
      title: 'Ready to Charge',
      message: reason,
      severity: 'success'
    });
  }
  
  console.log(`[${chargePointId}] ChargerStatus: ${ready ? 'READY' : 'NOT READY'} - ${reason}`);
}
```

### 3. Handle SystemAlert (Real-Time Faults)

```javascript
function handleSystemAlert(chargePointId, data) {
  const { alertType, message, severity, timestamp } = data;
  
  // Store in database
  db.systemAlerts.insert({
    chargePointId,
    alertType,
    message,
    severity,
    timestamp: new Date(timestamp),
    createdAt: new Date()
  });
  
  // Send notification based on severity
  if (severity === 'Critical') {
    // Send SMS/email to admin
    sendAdminAlert({
      chargePointId,
      alertType,
      message,
      severity
    });
    
    // Send push notification to user
    sendToUser(chargePointId, {
      type: 'CRITICAL_ALERT',
      title: 'Charger Issue',
      message: message,
      severity: 'error'
    });
  }
  
  console.log(`[${chargePointId}] SystemAlert [${severity}]: ${alertType} - ${message}`);
}
```

### 4. Handle BMSAlert

```javascript
function handleBMSAlert(chargePointId, data) {
  const { alertType, message, timestamp } = data;
  
  // Store in database
  db.bmsAlerts.insert({
    chargePointId,
    alertType,
    message,
    timestamp: new Date(timestamp),
    createdAt: new Date()
  });
  
  // Emergency stop notification
  if (alertType === 'BMS_EMERGENCY_STOP') {
    sendToUser(chargePointId, {
      type: 'EMERGENCY_STOP',
      title: 'Charging Stopped',
      message: 'Vehicle BMS stopped charging for safety',
      severity: 'error'
    });
  }
  
  console.log(`[${chargePointId}] BMSAlert: ${alertType} - ${message}`);
}
```

---

## UI Display Examples

### Mobile App - Pre-Charge Screen

```
┌─────────────────────────────────┐
│  🔌 Charger Status              │
├─────────────────────────────────┤
│                                 │
│  Vehicle: Rivot Pro             │
│  SOC: 98.2%                     │
│  Range: 159 km                  │
│                                 │
│  ❌ Charger Not Ready           │
│  ⚠️  Charger module offline -   │
│     check CAN bus connection    │
│                                 │
│  [ Cannot Start Charging ]      │
│                                 │
└─────────────────────────────────┘
```

### Mobile App - Ready to Charge

```
┌─────────────────────────────────┐
│  🔌 Charger Status              │
├─────────────────────────────────┤
│                                 │
│  Vehicle: Rivot Pro             │
│  SOC: 98.2%                     │
│  Range: 159 km                  │
│                                 │
│  ✅ Charger Ready               │
│  You can start charging         │
│                                 │
│  [  Start Charging  ]           │
│                                 │
└─────────────────────────────────┘
```

### Admin Dashboard - Alert Panel

```
┌─────────────────────────────────────────────────────┐
│  🚨 System Alerts                                   │
├─────────────────────────────────────────────────────┤
│  🔴 CRITICAL - Charger CP001                        │
│     CHARGER_OFFLINE - CAN communication timeout     │
│     2 minutes ago                                   │
├─────────────────────────────────────────────────────┤
│  🟡 WARNING - Charger CP002                         │
│     TEMPERATURE_WARNING - Temperature: 65.2°C       │
│     5 minutes ago                                   │
├─────────────────────────────────────────────────────┤
│  🟢 INFO - Charger CP001                            │
│     CHARGER_ONLINE - CAN communication restored     │
│     10 minutes ago                                  │
└─────────────────────────────────────────────────────┘
```

---

## Database Schema

### charger_status Table
```sql
CREATE TABLE charger_status (
  id SERIAL PRIMARY KEY,
  charge_point_id VARCHAR(50) NOT NULL,
  ready BOOLEAN NOT NULL,
  reason TEXT NOT NULL,
  timestamp BIGINT NOT NULL,
  updated_at TIMESTAMP DEFAULT NOW(),
  UNIQUE(charge_point_id)
);
```

### system_alerts Table
```sql
CREATE TABLE system_alerts (
  id SERIAL PRIMARY KEY,
  charge_point_id VARCHAR(50) NOT NULL,
  alert_type VARCHAR(50) NOT NULL,
  message TEXT NOT NULL,
  severity VARCHAR(20) NOT NULL,
  timestamp BIGINT NOT NULL,
  created_at TIMESTAMP DEFAULT NOW(),
  INDEX idx_charger_time (charge_point_id, created_at)
);
```

### bms_alerts Table
```sql
CREATE TABLE bms_alerts (
  id SERIAL PRIMARY KEY,
  charge_point_id VARCHAR(50) NOT NULL,
  alert_type VARCHAR(50) NOT NULL,
  message TEXT NOT NULL,
  timestamp BIGINT NOT NULL,
  created_at TIMESTAMP DEFAULT NOW(),
  INDEX idx_charger_time (charge_point_id, created_at)
);
```

---

## Testing

### Test ChargerStatus Message

1. Plug in vehicle
2. Wait 5 seconds
3. Server should receive ChargerStatus with ready=true or false
4. UI should show status to user

### Test SystemAlert Messages

1. Disconnect CAN bus → Receive `CHARGER_OFFLINE` alert
2. Reconnect CAN bus → Receive `CHARGER_ONLINE` alert
3. Heat charger > 60°C → Receive `TEMPERATURE_WARNING` alert
4. Disconnect WiFi → Receive `WIFI_DISCONNECTED` alert

---

## Summary

**Message Flow**:
```
1. User plugs in vehicle
   ↓
2. Charger sends VehicleInfo (SOC, model, range)
   ↓
3. Charger sends ChargerStatus every 5s
   - Shows "Ready" or blocking issue
   ↓
4. User sees status in app
   - If ready: "Start Charging" button enabled
   - If not ready: Shows reason (e.g., "Charger offline")
   ↓
5. User presses "Start Charging"
   ↓
6. Server sends RemoteStart
   ↓
7. Charging begins (or rejected if not ready)
```

**Key Benefits**:
- ✅ User knows WHY they can't charge BEFORE pressing start
- ✅ Admin gets real-time alerts for all faults
- ✅ Complete fault history in database
- ✅ Proactive maintenance based on alerts
- ✅ Better user experience with clear error messages

---

**Implementation Complete**: All alerts now sent to server with detailed messages for user display.
