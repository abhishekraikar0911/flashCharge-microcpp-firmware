# OCPP Fault & Alert Messaging - Current Status Analysis

## Currently Implemented Alerts ✅

### 1. BMS Safety Alerts
**Function**: `ocpp::sendBMSAlert(alertType, message)`  
**Location**: `src/modules/ocpp_manager.cpp`  
**Method**: DataTransfer with messageId="BMSAlert"

**Alert Types Sent**:
- `BMS_CHARGING_DISABLED` - When BMS MOSFET is OFF (byte4=0x01)
- `BMS_EMERGENCY_STOP` - When BMS switches OFF during charging
- `BMS_CHARGING_ENABLED` - When BMS recovers and allows charging

**Trigger Locations**:
- `src/main.cpp` - BMS safety monitor (100ms check)
- `src/modules/ocpp_state_machine.cpp` - RemoteStart rejection

**Data Sent**:
```json
{
  "vendorId": "RivotMotors",
  "messageId": "BMSAlert",
  "data": "{\"alertType\":\"BMS_EMERGENCY_STOP\",\"message\":\"...\",\"timestamp\":12345}"
}
```

### 2. Charger Module Health Status
**Function**: `setEvseReadyInput()` callback  
**Location**: `src/modules/ocpp_manager.cpp`  
**Method**: Automatic connector status update (Available/Unavailable)

**Detection**:
- Monitors CAN message timeouts (3 seconds)
- Checks: Terminal Power, Terminal Status, Heartbeat
- Sets connector to "Unavailable" when offline

**Logging Only** (not sent to server):
```
[OCPP] EVSE ready: NO
[OCPP] Charger OFFLINE - Availability will update automatically
```

### 3. Plug Connection Status
**Function**: `setConnectorPluggedInput()` callback  
**Location**: `src/modules/ocpp_manager.cpp`  
**Method**: Automatic StatusNotification

**Detection**:
- Monitors `gunPhysicallyConnected` and `batteryConnected`
- Logs state changes

**Logging Only**:
```
[OCPP] Plug state: DISCONNECTED (gun=0, battery=0)
```

---

## Missing Critical Alerts ❌

### 1. WiFi Connection Loss
**Current Status**: NOT SENT TO SERVER  
**Detection**: `g_wifiManager.isConnected()` exists  
**Impact**: Server doesn't know charger is offline

**What's Missing**:
- No alert when WiFi disconnects
- No alert when WiFi reconnects
- No network quality metrics

### 2. Charger Module Offline Alert
**Current Status**: PARTIAL - Only changes connector status  
**Detection**: `isChargerModuleHealthy()` exists  
**Impact**: Server sees "Unavailable" but no detailed reason

**What's Missing**:
- No DataTransfer alert explaining WHY offline
- No CAN bus error details
- No recovery notification with details

### 3. CAN Bus Errors
**Current Status**: NOT SENT TO SERVER  
**Detection**: CAN bus status logged in `chargerCommTask()`  
**Impact**: Critical hardware issues invisible to server

**What's Missing**:
- BUS-OFF state alerts
- High error counter warnings
- Recovery notifications

### 4. Temperature Warnings
**Current Status**: NOT SENT TO SERVER  
**Detection**: `chargerTemp` monitored  
**Impact**: Overheating risk not communicated

**What's Missing**:
- High temperature warnings (>60°C)
- Critical temperature alerts (>70°C)
- Cooling recovery notifications

### 5. Voltage/Current Out of Range
**Current Status**: NOT SENT TO SERVER  
**Detection**: Validation exists in code  
**Impact**: Electrical faults not reported

**What's Missing**:
- Overvoltage alerts (>85.5V)
- Undervoltage alerts (<56V)
- Overcurrent alerts (>300A)

### 6. Transaction Failures
**Current Status**: NOT SENT TO SERVER  
**Detection**: Transaction rejection logged  
**Impact**: User doesn't know why charging failed

**What's Missing**:
- RemoteStart rejection reasons
- Authorization failures
- Hardware not ready alerts

### 7. System Health Metrics
**Current Status**: NOT SENT TO SERVER  
**Detection**: Health monitor exists  
**Impact**: No proactive maintenance alerts

**What's Missing**:
- Memory usage warnings
- Task watchdog timeouts
- Reboot count increases
- Uptime milestones

---

## Recommended Implementation

### Priority 1: Critical Safety Alerts (MUST HAVE)

1. **Charger Module Offline Alert**
   ```cpp
   ocpp::sendSystemAlert("CHARGER_OFFLINE", "CAN communication lost");
   ```

2. **WiFi Disconnection Alert**
   ```cpp
   ocpp::sendSystemAlert("WIFI_DISCONNECTED", "Network connection lost");
   ```

3. **Temperature Critical Alert**
   ```cpp
   ocpp::sendSystemAlert("TEMPERATURE_CRITICAL", "Charger temp: 75°C");
   ```

4. **Voltage Out of Range Alert**
   ```cpp
   ocpp::sendSystemAlert("VOLTAGE_FAULT", "Voltage: 90V (max: 85.5V)");
   ```

### Priority 2: Operational Alerts (SHOULD HAVE)

5. **CAN Bus Error Alert**
   ```cpp
   ocpp::sendSystemAlert("CAN_BUS_OFF", "CAN bus entered BUS-OFF state");
   ```

6. **Transaction Rejection Alert**
   ```cpp
   ocpp::sendSystemAlert("REMOTE_START_REJECTED", "Charger not ready");
   ```

7. **Recovery Notifications**
   ```cpp
   ocpp::sendSystemAlert("CHARGER_ONLINE", "CAN communication restored");
   ocpp::sendSystemAlert("WIFI_RECONNECTED", "Network connection restored");
   ```

### Priority 3: Diagnostic Alerts (NICE TO HAVE)

8. **System Health Warnings**
   ```cpp
   ocpp::sendSystemAlert("MEMORY_LOW", "Free heap: 50KB");
   ocpp::sendSystemAlert("WATCHDOG_TIMEOUT", "Task CAN_RX timeout");
   ```

---

## Proposed Implementation Plan

### Step 1: Create Generic Alert Function
**File**: `src/modules/ocpp_manager.cpp`

```cpp
void ocpp::sendSystemAlert(const char* alertType, const char* message, const char* severity = "Warning")
{
    if (!isOperative()) return;
    
    Serial.printf("[OCPP] 🚨 Alert: [%s] %s - %s\n", severity, alertType, message);
    
    sendRequest("DataTransfer",
        [alertType, message, severity]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            MicroOcpp::JsonDoc dataDoc(256);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["alertType"] = alertType;
            dataObj["message"] = message;
            dataObj["severity"] = severity;  // "Info", "Warning", "Critical"
            dataObj["timestamp"] = millis();
            
            String dataStr;
            serializeJson(dataObj, dataStr);
            
            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(512));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"] = "RivotMotors";
            payload["messageId"] = "SystemAlert";
            payload["data"] = dataStr;
            return doc;
        },
        [](JsonObject response) {
            Serial.printf("[OCPP] ✅ SystemAlert acknowledged\n");
        }
    );
}
```

### Step 2: Add Alert Triggers

**WiFi Monitoring** (`src/main.cpp`):
```cpp
static bool lastWifiConnected = true;
bool wifiConnected = g_wifiManager.isConnected();

if (wifiConnected != lastWifiConnected) {
    if (!wifiConnected) {
        ocpp::sendSystemAlert("WIFI_DISCONNECTED", "Network connection lost", "Critical");
    } else {
        ocpp::sendSystemAlert("WIFI_RECONNECTED", "Network connection restored", "Info");
    }
    lastWifiConnected = wifiConnected;
}
```

**Charger Health Monitoring** (`src/main.cpp`):
```cpp
if (chargerHealthy != lastChargerHealthy) {
    if (!chargerHealthy) {
        ocpp::sendSystemAlert("CHARGER_OFFLINE", "CAN communication timeout", "Critical");
    } else {
        ocpp::sendSystemAlert("CHARGER_ONLINE", "CAN communication restored", "Info");
    }
}
```

**Temperature Monitoring** (`src/main.cpp`):
```cpp
static bool tempWarningActive = false;

if (chargerTemp > 70.0f && !tempWarningActive) {
    ocpp::sendSystemAlert("TEMPERATURE_CRITICAL", 
        String("Charger temp: " + String(chargerTemp, 1) + "°C").c_str(), 
        "Critical");
    tempWarningActive = true;
} else if (chargerTemp < 65.0f && tempWarningActive) {
    ocpp::sendSystemAlert("TEMPERATURE_NORMAL", "Temperature recovered", "Info");
    tempWarningActive = false;
}
```

### Step 3: Add Alert Configuration
**File**: `include/config/hardware.h`

```cpp
// ========== ALERT THRESHOLDS ==========
#define ALERT_TEMP_WARNING_C 60.0f
#define ALERT_TEMP_CRITICAL_C 70.0f
#define ALERT_VOLTAGE_MIN_V 50.0f
#define ALERT_VOLTAGE_MAX_V 90.0f
#define ALERT_CURRENT_MAX_A 320.0f
```

---

## Summary

### Currently Sending to Server:
✅ BMS safety alerts (3 types)  
✅ Connector availability status (automatic)  
✅ Plug connection status (automatic)

### NOT Sending to Server:
❌ WiFi connection status  
❌ Charger module offline details  
❌ CAN bus errors  
❌ Temperature warnings  
❌ Voltage/current faults  
❌ Transaction rejection reasons  
❌ System health metrics

### Recommendation:
Implement `sendSystemAlert()` function and add monitoring for all critical faults. This will provide complete visibility to the OCPP server for proactive maintenance and user support.

---

**Next Step**: Review this analysis and approve implementation of missing alerts.
