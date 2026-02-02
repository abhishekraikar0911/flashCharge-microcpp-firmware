# Pay-Before-Charge Implementation

## Changes Made (2025-01-23)

### ✅ P0 Critical Fixes

#### 1. **Energy Accumulation Fixed**
- **Issue**: Energy always showed 0 in MeterValues
- **Root Cause**: Energy meter callback returned `(int)energyWh` which truncated small values
- **Fix**: Energy now accumulates correctly using power × time calculation
- **Location**: `src/main.cpp` - Energy accumulation loop
- **Result**: MeterValues will show increasing energy (0.39Wh @ 10s, 0.78Wh @ 20s, etc.)

#### 2. **meterStop Value Fixed**
- **Issue**: StopTransaction sent `meterStop: 0` instead of actual energy
- **Root Cause**: Energy meter callback validation
- **Fix**: Energy meter now returns valid accumulated value
- **Location**: `src/modules/ocpp_manager.cpp` - setEnergyMeterInput
- **Result**: StopTransaction will send correct meterStop value matching SessionSummary

#### 3. **Current.Offered Added**
- **Issue**: Backend couldn't detect vehicle model during charging
- **Fix**: Added `Current.Offered` measurand to MeterValues
- **Location**: `src/modules/ocpp_manager.cpp` - addMeterValueInput
- **Values**: 
  - Classic: 2A
  - Pro: 30A
  - Max: 60A
  - Max+: 100A
- **Result**: Backend can identify vehicle model from BMS_Imax

#### 4. **PreChargeData Implemented**
- **Issue**: No vehicle data sent before user pays
- **Fix**: Send DataTransfer with PreChargeData when vehicle connects
- **Location**: `src/main.cpp` - Vehicle connection detection
- **Trigger**: When `batteryConnected && gunPhysicallyConnected && !isTransactionActive`
- **Data Sent**:
  ```json
  {
    "soc": 87.99,
    "maxCurrent": 2,
    "voltage": 76.46,
    "current": 0.0,
    "temperature": 25.0
  }
  ```
- **Result**: Dashboard shows battery info before payment

### 📊 Expected OCPP Flow

```
1. Vehicle Connects
   └─> DataTransfer: PreChargeData (SOC, maxCurrent, voltage)
   └─> StatusNotification: Preparing

2. User Pays on Dashboard
   └─> Backend sends RemoteStartTransaction

3. Transaction Starts
   └─> StartTransaction (meterStart: 0)
   └─> StatusNotification: Charging
   └─> Reset energyWh = 0.0

4. During Charging (every 10s)
   └─> MeterValues:
       - Energy.Active.Import.Register: 0.39 Wh (cumulative)
       - Power.Active.Import: 139 W
       - SoC: 87.99 %
       - Voltage: 76.46 V
       - Current.Import: 1.82 A
       - Current.Offered: 2 A  ← NEW!

5. Transaction Stops
   └─> StopTransaction (meterStop: 872 Wh)  ← FIXED!
   └─> DataTransfer: SessionSummary
   └─> StatusNotification: Finishing → Available
```

### 🔧 Technical Details

#### Energy Calculation
```cpp
// In main.cpp loop()
if (chargingEnabled && ocppAllowsCharge) {
    unsigned long now = millis();
    float dt_hours = (now - lastEnergyTime) / 3600000.0f;
    float energyDelta = terminalVolt * terminalCurr * dt_hours;
    
    if (energyDelta > 0.0f && energyDelta < 1000.0f) {
        energyWh += energyDelta;  // Cumulative
    }
    lastEnergyTime = now;
}
```

#### PreChargeData Trigger
```cpp
// In main.cpp loop()
static bool preChargeDataSent = false;

if (batteryConnected && gunPhysicallyConnected && 
    !isTransactionActive(1) && !preChargeDataSent &&
    socPercent > 0.0f && BMS_Imax > 0.0f && terminalVolt > 0.0f)
{
    ocpp::sendVehicleInfo(socPercent, BMS_Imax, terminalVolt, terminalCurr, chargerTemp);
    preChargeDataSent = true;
}

// Reset when vehicle disconnects
if (!batteryConnected || !gunPhysicallyConnected) {
    preChargeDataSent = false;
}
```

### 📝 Testing Checklist

- [ ] Verify PreChargeData sent when vehicle connects (before payment)
- [ ] Verify Energy increases in MeterValues (not stuck at 0)
- [ ] Verify meterStop matches SessionSummary energyDelivered
- [ ] Verify Current.Offered shows correct BMS_Imax value
- [ ] Verify energy resets to 0 at StartTransaction
- [ ] Verify SessionSummary sent at StopTransaction
- [ ] Test with all vehicle models (Classic, Pro, Max, Max+)

### 🎯 Expected Results

**Before Fix:**
```json
MeterValues: { "Energy": "0" }  ← Always 0!
StopTransaction: { "meterStop": 0 }  ← Wrong!
```

**After Fix:**
```json
PreChargeData: { "soc": 87.99, "maxCurrent": 2, "voltage": 76.46 }
MeterValues (10s): { "Energy": "0.39", "Current.Offered": "2" }
MeterValues (20s): { "Energy": "0.78", "Current.Offered": "2" }
StopTransaction: { "meterStop": 872 }  ← Correct!
SessionSummary: { "energyDelivered": 0.872694016 }  ← Matches!
```

### 🚀 Deployment

1. Build firmware: `pio run -e charger_esp32_production`
2. Flash to device: `pio run -e charger_esp32_production --target upload`
3. Monitor logs: `pio device monitor --baud 115200`
4. Test complete flow:
   - Plug in vehicle
   - Check PreChargeData in server logs
   - Start transaction from dashboard
   - Monitor MeterValues every 10s
   - Stop transaction
   - Verify meterStop value

### 📚 Related Files

- `src/modules/ocpp_manager.cpp` - OCPP callbacks, PreChargeData, Current.Offered
- `src/main.cpp` - Energy accumulation, PreChargeData trigger
- `src/core/globals.cpp` - Global variable initialization
- `include/ocpp/ocpp_client.h` - Function declarations

### 🔗 Backend Integration

Backend should handle:
1. **PreChargeData** - Display vehicle info before payment
2. **Current.Offered** - Detect vehicle model (2A=Classic, 30A=Pro, 60A=Max, 100A=Max+)
3. **Energy.Active.Import.Register** - Real-time energy consumption
4. **meterStop** - Final energy for billing (matches SessionSummary)

---

**Status**: ✅ Ready for Testing
**Version**: v2.5.0
**Date**: 2025-01-23
