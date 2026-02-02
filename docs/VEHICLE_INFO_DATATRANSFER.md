# Vehicle Info DataTransfer

## Problem
Need to show vehicle details (SOC, max current) to users **before** they start charging, but MeterValues only work during active transactions.

## Solution
Use OCPP 1.6 **DataTransfer** to send vehicle info when gun is connected and vehicle is detected.

## Flow

```
Gun Connected
   ↓
ESP32 reads BMS (SOC, BMS_Imax)
   ↓
Battery detected (batteryConnected = true)
   ↓
StatusNotification (Preparing)
   ↓
DataTransfer (VehicleInfo) ← NEW
   ↓
Server UI shows: SOC, MaxCurrent
   ↓
User presses START
   ↓
StartTransaction
   ↓
MeterValues begin (every 30s)
```

## DataTransfer Message

**Request:**
```json
{
  "vendorId": "RivotMotors",
  "messageId": "VehicleInfo",
  "data": {
    "soc": 84.77,
    "maxCurrent": 100.0
  }
}
```

**Response:**
```json
{
  "status": "Accepted"
}
```

## Implementation

### 1. API Function
```cpp
// include/ocpp/ocpp_client.h
namespace ocpp {
    void sendVehicleInfo(float soc, float maxCurrent);
}
```

### 2. OCPP Manager
```cpp
// src/modules/ocpp_manager.cpp
void ocpp::sendVehicleInfo(float soc, float maxCurrent)
{
    sendRequest("DataTransfer",
        [soc, maxCurrent]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(256));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"] = "RivotMotors";
            payload["messageId"] = "VehicleInfo";
            JsonObject data = payload.createNestedObject("data");
            data["soc"] = soc;
            data["maxCurrent"] = maxCurrent;
            return doc;
        },
        [](JsonObject response) {
            Serial.printf("[OCPP] ✅ VehicleInfo response: %s\n", response["status"] | "Unknown");
        }
    );
}
```

### 3. Main Loop Trigger
```cpp
// src/main.cpp
static bool vehicleInfoSent = false;

// Send once when battery connected and not in transaction
if (batteryConnected && !vehicleInfoSent && !isTransactionActive(1))
{
    if (socPercent > 0.0f && BMS_Imax > 0.0f)
    {
        ocpp::sendVehicleInfo(socPercent, BMS_Imax);
        vehicleInfoSent = true;
    }
}

// Reset flag when gun unplugged
if (!currentPlugState) {
    vehicleInfoSent = false;
}
```

## Server-Side Handling

Your OCPP server should handle the DataTransfer message:

```java
// Example SteVe server handler
@Override
public DataTransferResponse dataTransfer(DataTransferRequest request) {
    if ("RivotMotors".equals(request.getVendorId()) && 
        "VehicleInfo".equals(request.getMessageId())) {
        
        JsonObject data = parseJson(request.getData());
        float soc = data.get("soc").getAsFloat();
        float maxCurrent = data.get("maxCurrent").getAsFloat();
        
        // Store in database
        vehicleInfoRepository.save(chargeBoxId, soc, maxCurrent);
        
        // Update UI in real-time
        websocketService.sendToUI(chargeBoxId, "vehicleInfo", data);
        
        return new DataTransferResponse(DataTransferStatus.ACCEPTED);
    }
    return new DataTransferResponse(DataTransferStatus.REJECTED);
}
```

## Benefits

✅ **Industry Standard**: Uses OCPP 1.6 DataTransfer (vendor-specific data)
✅ **Pre-Transaction**: Sends before StartTransaction
✅ **Real-Time**: Immediate feedback when vehicle connects
✅ **Flexible**: Can add more fields (range, model, etc.) later
✅ **Non-Blocking**: Doesn't interfere with transaction flow

## Future Enhancements

Can extend to send:
- Vehicle model (Classic/Pro/Max)
- Battery capacity (Ah)
- Estimated range (km)
- Battery temperature
- Charging history

Example:
```json
{
  "vendorId": "RivotMotors",
  "messageId": "VehicleInfo",
  "data": {
    "soc": 84.77,
    "maxCurrent": 100.0,
    "model": "Pro",
    "capacity": 68.0,
    "range": 245.5,
    "temperature": 27.6
  }
}
```
