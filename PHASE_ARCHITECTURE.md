# Industrial-Grade 3-Phase Architecture
## ESP32 OCPP DC Fast Charger

---

## ✅ IMPLEMENTATION STATUS: COMPLETE

### 🔹 Phase 1: PREPARING (Cable Plugged, No Transaction)

**Status:** `Preparing`  
**Transaction:** `NOT ACTIVE`  
**Duration:** From gun plug until RemoteStart

#### Goal
Display initial vehicle information to UI before charging starts.

#### Implementation
- **Message Type:** `DataTransfer (VehicleInfo)`
- **Frequency:** **ONE-SHOT per plug-in event**
- **Trigger Condition:**
  ```cpp
  if (batteryConnected && 
      !isTransactionActive(1) && 
      !vehicleInfoSent && 
      socPercent > 0.0f && 
      BMS_Imax > 0.0f && 
      terminalVolt > 0.0f)
  ```
- **Reset Condition:** Gun unplugged ONLY

#### Payload Example
```json
{
  "vendorId": "RivotMotors",
  "messageId": "VehicleInfo",
  "data": "{\"soc\":86.88,\"maxCurrent\":90,\"voltage\":77.2,\"current\":0.0,\"temperature\":26.6}"
}
```

#### UI Display
- ✅ Vehicle connected
- ✅ Initial SOC (%)
- ✅ Max current (A)
- ✅ Voltage (V)
- ✅ Battery temperature (°C)
- ✅ Estimated range/time

#### Code Location
- **File:** `src/main.cpp` (lines 247-258)
- **Flag:** `vehicleInfoSent` (static, resets on unplug only)

---

### 🔹 Phase 2: CHARGING (Transaction Active)

**Status:** `Charging`  
**Transaction:** `ACTIVE (transactionId exists)`  
**Duration:** From StartTransaction until StopTransaction

#### Goal
Provide live telemetry for real-time charging visualization.

#### Implementation
- **Message Type:** `MeterValues` (OCPP 1.6 standard)
- **Frequency:** **10 seconds** (DC fast charging optimized)
- **Measurands:**
  - `SoC` (Percent) - Live state of charge
  - `Voltage` (V) - Terminal voltage
  - `Current.Import` (A) - Charging current
  - `Power.Active.Import` (W) - Instantaneous power
  - `Energy.Active.Import.Register` (Wh) - Cumulative energy

#### Configuration
```cpp
// MeterValueSampleInterval = 10s (DC fast charging)
config->setInt(10);

// MeterValuesSampledData
config->setString("Energy.Active.Import.Register,Power.Active.Import,Voltage,Current.Import,SoC");
```

#### Payload Example
```json
{
  "connectorId": 1,
  "transactionId": 12345,
  "meterValue": [{
    "timestamp": "2025-01-28T09:18:45Z",
    "sampledValue": [
      {"measurand": "SoC", "value": "86.83", "unit": "Percent"},
      {"measurand": "Voltage", "value": "77.2", "unit": "V"},
      {"measurand": "Current.Import", "value": "45.3", "unit": "A"},
      {"measurand": "Power.Active.Import", "value": "3497", "unit": "W"},
      {"measurand": "Energy.Active.Import.Register", "value": "1250", "unit": "Wh"}
    ]
  }]
}
```

#### UI Display
- ✅ Live SOC (updates every 10s)
- ✅ Power (W)
- ✅ Voltage (V)
- ✅ Current (A)
- ✅ Energy delivered (Wh)
- ✅ Charging graph (smooth updates)

#### Code Location
- **File:** `src/modules/ocpp_manager.cpp` (lines 62-120)
- **Interval:** Line 63 (`setInt(10)`)

#### Why 10s (not 30s)?
| Interval | User Experience | Network Load |
|----------|----------------|--------------|
| 5s | Ideal (real-time feel) | High |
| **10s** | **Acceptable (smooth)** | **Moderate** ✅ |
| 30s | Broken (45% → 50% jumps) | Low |

---

### 🔹 Phase 3: FINISHING (Transaction Stopped)

**Status:** `Finishing`  
**Transaction:** `STOPPED (StopTransaction sent)`  
**Duration:** From StopTransaction until gun unplugged

#### Goal
Send final session summary and stop all background traffic.

#### Implementation
- **Message Type:** `DataTransfer (SessionSummary)`
- **Frequency:** **ONE-SHOT** (on StopTransaction)
- **Trigger:** `TxNotification_StopTx` event

#### Payload Example
```json
{
  "vendorId": "RivotMotors",
  "messageId": "SessionSummary",
  "data": "{\"finalSoc\":92.5,\"energyDelivered\":3420,\"durationMinutes\":15.3}"
}
```

#### Sequence (Correct Order)
```
1. RemoteStopTransaction (from server)
2. StopTransaction (to server)
3. StatusNotification: Finishing
4. DataTransfer: SessionSummary ✅
5. [Gun unplugged]
6. StatusNotification: Available
```

#### What STOPS in Phase 3
- ❌ MeterValues (no more live data)
- ❌ VehicleInfo (already sent in Phase 1)
- ❌ Power/Current sampling

#### Code Location
- **File:** `src/modules/ocpp_manager.cpp` (lines 130-136)
- **Trigger:** `TxNotification_StopTx` notification

---

## 📊 Phase Ownership Table

| Data Type | Phase 1 (Preparing) | Phase 2 (Charging) | Phase 3 (Finishing) |
|-----------|---------------------|--------------------|--------------------|
| **SOC** | VehicleInfo (once) | MeterValues (10s) | SessionSummary (once) |
| **Voltage** | VehicleInfo (once) | MeterValues (10s) | — |
| **Current** | VehicleInfo (once) | MeterValues (10s) | — |
| **Power** | — | MeterValues (10s) | — |
| **Energy** | — | MeterValues (10s) | SessionSummary (once) |
| **Temperature** | VehicleInfo (once) | — | — |

**Rule:** Only ONE SOC source active at a time.

---

## 🔒 Critical Guards

### 1. VehicleInfo One-Shot Guard
```cpp
static bool vehicleInfoSent = false;  // Per plug-in event

// Send ONCE
if (!vehicleInfoSent && !isTransactionActive(1)) {
    sendVehicleInfo(...);
    vehicleInfoSent = true;
}

// Reset ONLY on unplug
if (!gunPlugged) {
    vehicleInfoSent = false;
}
```

### 2. Transaction State Guard
```cpp
// NEVER send VehicleInfo during transaction
if (isTransactionActive(1)) {
    // VehicleInfo blocked ❌
}
```

### 3. MeterValues Automatic Control
- MicroOcpp library automatically sends MeterValues during transaction
- No manual control needed
- Stops automatically when transaction ends

---

## 🎯 Production-Grade Checklist

- ✅ VehicleInfo sent ONCE per plug-in (not periodic)
- ✅ VehicleInfo NEVER sent during transaction
- ✅ MeterValues interval = 10s (DC fast charging optimized)
- ✅ MeterValues include all essential measurands
- ✅ SessionSummary sent on StopTransaction
- ✅ Clean shutdown sequence (no duplicate messages)
- ✅ Flag resets only on gun unplug
- ✅ No timer-based repeated sends

---

## 🧠 One-Line Summary

**"VehicleInfo is a one-shot pre-transaction message; once a transaction starts, SOC is exclusively reported via MeterValues."**

This is exactly how an OEM would justify it.

---

## 📝 Testing Verification

### Expected Log Sequence (Clean)
```
[OCPP] 🔌 Gun plugged, vehicle detected
[OCPP] 📤 Sending VehicleInfo: SOC=86.9% MaxI=90.0A V=77.2V I=0.0A T=26.6°C
[OCPP] ✅ VehicleInfo sent (one-shot)
[OCPP] ▶️  Charging enabled (RemoteStart)
[MO] MeterValues: SoC=86.83% V=77.2V I=45.3A P=3497W E=0Wh
[MO] MeterValues: SoC=87.12% V=77.4V I=46.1A P=3568W E=595Wh
[MO] MeterValues: SoC=87.45% V=77.5V I=45.8A P=3549W E=1184Wh
[OCPP] ⏹️  Charging disabled (RemoteStop)
[OCPP] 📊 Sending SessionSummary: FinalSOC=92.5% Energy=3420Wh Duration=15.3min
[OCPP] 🔌 Gun unplugged, sending Available status
```

### What You Should NOT See
```
❌ VehicleInfo → VehicleInfo → VehicleInfo (repeated)
❌ VehicleInfo after StartTransaction
❌ VehicleInfo during Charging
❌ VehicleInfo after StopTransaction
❌ 30-second gaps in MeterValues (now 10s)
```

---

## 🚀 Performance Metrics

| Metric | Value | Status |
|--------|-------|--------|
| MeterValues Interval | 10s | ✅ Optimal for DC |
| VehicleInfo Frequency | Once per plug | ✅ Minimal traffic |
| UI Update Latency | <10s | ✅ Smooth UX |
| Network Overhead | Low | ✅ Efficient |
| OCPP Compliance | 1.6 Full | ✅ Standard |

---

**Last Updated:** January 2025  
**Status:** ✅ Production Ready  
**Architecture:** Industrial-Grade DC Fast Charging
