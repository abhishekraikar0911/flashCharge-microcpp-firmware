# SECURITY FIXES - QUICK REFERENCE

## 🚀 Quick Start

### 1. Safe String Operations
```cpp
// ❌ OLD (UNSAFE)
char buffer[32];
strcpy(buffer, userInput);  // Buffer overflow risk!
sprintf(buffer, "Value: %d", value);  // No bounds check!

// ✅ NEW (SAFE)
#include "utils/safe_string.h"
char buffer[32];
SafeString::copy(buffer, userInput, sizeof(buffer));
SafeString::format(buffer, sizeof(buffer), "Value: %d", value);
```

### 2. CAN Message Validation
```cpp
// ❌ OLD (NO VALIDATION)
void handleCANMessage(const twai_message_t &msg) {
    float voltage = parseVoltage(msg.data);
    // Use voltage directly - DANGEROUS!
}

// ✅ NEW (VALIDATED)
#include "utils/can_validator.h"
void handleCANMessage(const twai_message_t &msg) {
    if (!CANValidator::validateMessage(msg)) return;
    if (!CANValidator::validateRawData(msg.data, msg.data_length_code)) return;
    
    float voltage = parseVoltage(msg.data);
    if (!CANValidator::validateVoltage(voltage)) {
        Serial.println("Invalid voltage!");
        return;
    }
    // Safe to use voltage
}
```

### 3. Secure Credentials
```cpp
// ❌ OLD (HARDCODED)
#define WIFI_SSID "MyNetwork"
#define WIFI_PASS "MyPassword"
WiFi.begin(WIFI_SSID, WIFI_PASS);

// ✅ NEW (ENCRYPTED)
#include "utils/secure_credentials.h"
char ssid[33], pass[64];
if (SecureCredentials::g_secureCredentials.getWiFiCredentials(
    ssid, pass, sizeof(ssid), sizeof(pass))) {
    WiFi.begin(ssid, pass);
}
```

### 4. Mutex with Timeout
```cpp
// ❌ OLD (DEADLOCK RISK)
xSemaphoreTake(dataMutex, portMAX_DELAY);
// Critical section
xSemaphoreGive(dataMutex);

// ✅ NEW (SAFE)
if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    // Critical section
    xSemaphoreGive(dataMutex);
} else {
    Serial.println("[ERROR] Mutex timeout");
}
```

## 📋 Validation Functions

### Voltage Validation
```cpp
if (CANValidator::validateVoltage(voltage)) {
    // Valid: 0.0V to 100.0V
}
```

### Current Validation
```cpp
if (CANValidator::validateCurrent(current)) {
    // Valid: -10.0A to 350.0A
}
```

### SOC Validation
```cpp
if (CANValidator::validateSOC(soc)) {
    // Valid: 0.0% to 100.0%
}
```

### Temperature Validation
```cpp
if (CANValidator::validateTemperature(temp)) {
    // Valid: -40°C to 120°C
}
```

## 🔧 Common Patterns

### Pattern 1: Safe Buffer Copy
```cpp
void copyString(const char* src) {
    char dest[64];
    SafeString::copy(dest, src, sizeof(dest));
    // dest is guaranteed null-terminated
}
```

### Pattern 2: Safe Format String
```cpp
void formatMessage(int value) {
    char buffer[128];
    int written = SafeString::format(buffer, sizeof(buffer), 
                                     "Status: %d", value);
    Serial.println(buffer);
}
```

### Pattern 3: Validate Before Use
```cpp
void processCANData(const twai_message_t &msg) {
    // Step 1: Validate structure
    if (!CANValidator::validateMessage(msg)) return;
    
    // Step 2: Validate raw data
    if (!CANValidator::validateRawData(msg.data, msg.data_length_code)) return;
    
    // Step 3: Parse data
    float voltage = parseVoltage(msg.data);
    
    // Step 4: Validate parsed value
    if (!CANValidator::validateVoltage(voltage)) return;
    
    // Step 5: Safe to use
    processVoltage(voltage);
}
```

### Pattern 4: Protected Critical Section
```cpp
void updateSharedState(float newValue) {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        sharedVariable = newValue;
        xSemaphoreGive(dataMutex);
    } else {
        Serial.println("[ERROR] Failed to acquire mutex");
        // Handle error - don't update state
    }
}
```

## ⚠️ Common Mistakes to Avoid

### Mistake 1: Forgetting Null Termination
```cpp
// ❌ WRONG
char buffer[32];
strncpy(buffer, src, 32);  // May not be null-terminated!

// ✅ CORRECT
SafeString::copy(buffer, src, sizeof(buffer));
```

### Mistake 2: No Bounds Checking
```cpp
// ❌ WRONG
sprintf(buffer, "Long string: %s", userInput);  // Overflow!

// ✅ CORRECT
SafeString::format(buffer, sizeof(buffer), "Long string: %s", userInput);
```

### Mistake 3: Trusting CAN Data
```cpp
// ❌ WRONG
float voltage = msg.data[0] / 10.0f;
setChargerVoltage(voltage);  // Could be garbage!

// ✅ CORRECT
float voltage = msg.data[0] / 10.0f;
if (CANValidator::validateVoltage(voltage)) {
    setChargerVoltage(voltage);
}
```

### Mistake 4: Infinite Mutex Wait
```cpp
// ❌ WRONG
xSemaphoreTake(mutex, portMAX_DELAY);  // Can deadlock!

// ✅ CORRECT
if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    // ...
}
```

## 🎯 Checklist for New Code

When adding new code, ensure:

- [ ] All string operations use `SafeString::` functions
- [ ] All CAN messages validated with `CANValidator::`
- [ ] All mutex operations have timeout (50-100ms)
- [ ] All array accesses bounds-checked
- [ ] All function return values checked
- [ ] No credentials hardcoded or logged
- [ ] All user inputs validated
- [ ] Error paths properly handled

## 📚 API Reference

### SafeString Namespace
- `copy(dest, src, destSize)` - Safe string copy
- `format(buffer, size, format, ...)` - Safe sprintf
- `validate(str, maxLen)` - Check null termination
- `append(dest, src, destSize)` - Safe concatenation

### CANValidator Namespace
- `validateMessage(msg)` - Check message structure
- `validateVoltage(v)` - Range: 0-100V
- `validateCurrent(i)` - Range: -10 to 350A
- `validateSOC(soc)` - Range: 0-100%
- `validateTemperature(t)` - Range: -40 to 120°C
- `validateRawData(data, dlc)` - Detect noise
- `validateCANID(id, ext)` - Check ID validity

### SecureCredentials Namespace
- `init()` - Initialize secure storage
- `storeWiFiCredentials(ssid, pass)` - Store WiFi
- `getWiFiCredentials(ssid, pass, ...)` - Retrieve WiFi
- `storeOCPPCredentials(host, port, id)` - Store OCPP
- `getOCPPCredentials(host, port, id, ...)` - Retrieve OCPP
- `clearAll()` - Factory reset
- `hasCredentials()` - Check if configured

## 🔍 Debugging Tips

### Enable Validation Logging
```cpp
// In your code
#define DEBUG_VALIDATION 1

#if DEBUG_VALIDATION
Serial.printf("[VALIDATE] Voltage: %.1fV %s\n", 
              voltage, 
              CANValidator::validateVoltage(voltage) ? "OK" : "FAIL");
#endif
```

### Check Mutex Contention
```cpp
unsigned long start = millis();
if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    unsigned long wait = millis() - start;
    if (wait > 10) {
        Serial.printf("[MUTEX] Long wait: %lu ms\n", wait);
    }
    // ...
    xSemaphoreGive(mutex);
}
```

### Monitor Buffer Usage
```cpp
char buffer[128];
int written = SafeString::format(buffer, sizeof(buffer), "Data: %s", data);
if (written >= sizeof(buffer) - 1) {
    Serial.println("[WARN] Buffer nearly full!");
}
```

---

**Quick Help**: See `SECURITY_FIXES_v2.6.0.md` for complete documentation.
