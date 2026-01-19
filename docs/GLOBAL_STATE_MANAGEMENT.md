# Global State Management Architecture

## Overview
This document describes how global state is managed in the microOCPP ESP32 firmware to avoid linker errors and ensure thread-safe access.

---

## Global Variable Definition Pattern

### Rule 1: Declare in Header, Define in Implementation
```cpp
// ✅ CORRECT - in include/header.h
extern SemaphoreHandle_t dataMutex;
extern float energyWh;
extern bool batteryConnected;

// ✅ CORRECT - in src/twai_init.cpp (SINGLE definition location)
SemaphoreHandle_t dataMutex = nullptr;
float energyWh = 0.0f;
bool batteryConnected = false;
```

```cpp
// ❌ WRONG - DO NOT DO THIS in header.h
SemaphoreHandle_t dataMutex = nullptr;  // This would be defined in EVERY .cpp that includes header.h
```

### Rule 2: All Global Variable Definitions in One Place
**Single Source of Truth**: `src/twai_init.cpp`

This file contains definitions for:
- Synchronization primitives (mutexes, semaphores)
- Vehicle/charging state variables
- Power measurements (voltage, current, temperature, power)
- BMS/Charger negotiation values
- Metric data and buffers
- Session state flags

**Why this location?**
- `twai_init.cpp` is always compiled (contains CAN initialization)
- It's the core system module
- Logically related to hardware state management

---

## Current Global Variables (All Defined in twai_init.cpp)

### Synchronization (2 variables)
```cpp
SemaphoreHandle_t dataMutex = nullptr;      // Protects shared state
SemaphoreHandle_t serialMutex = nullptr;    // Protects Serial I/O
```

### Energy & Time (2 variables)
```cpp
float energyWh = 0.0f;                      // Cumulative energy delivered
unsigned long lastHeartbeat = 0;            // Heartbeat timestamp
```

### System Flags (3 variables)
```cpp
bool batteryConnected = false;              // Battery presence
bool chargingEnabled = false;               // Charging permission
bool chargingswitch = false;                // Relay control
```

### Vehicle Detection (2 variables)
```cpp
String vehicleModel = "Unknown";            // Detected vehicle
bool vehicleConfirmed = false;              // Confirmation state
bool gunPhysicallyConnected = false;        // Connector presence
```

### BMS & Charger Values (8 variables)
```cpp
float BMS_Vmax = 0.0f, BMS_Imax = 0.0f;                    // BMS limits
float Charger_Vmax = 0.0f, Charger_Imax = 0.0f;            // Charger limits
float chargerVolt = 0.0f, chargerCurr = 0.0f;              // Charger output
float chargerTemp = 0.0f, terminalchargerPower = 0.0f;     // Charger health
float terminalVolt = 0.0f, terminalCurr = 0.0f;            // Terminal feedback
```

### Additional State (7+ variables)
```cpp
float socPercent = 0.0f;                    // Battery State of Charge
uint16_t metric79_raw = 0;                  // Raw metric values
float metric79_scaled = 0;
uint32_t metric83_raw = 0;
float metric83_scaled = 0;

unsigned long lastBMS = 0;                  // Last BMS comm
uint8_t heating = 0;                        // Temperature control

const char *chargerStatus = "UNKNOWN";      // Status strings
const char *terminalchargerStatus = "...";
const char *terminalStatus = "...";
```

### Message Buffers (8 variables)
```cpp
uint8_t lastData[8], lastBMSData[8], lastStatusData[8], lastHData[8];
uint8_t lastVmaxData[8], lastImaxData[8], lastBattData[8];
uint8_t lastVoltData[8], lastCurrData[8], lastTempData[8];
uint8_t lastTermData1[8], lastTermData2[8];
```

### Other State (2 variables)
```cpp
uint32_t cachedRawV;                        // Cached voltage
uint32_t cachedRawI;                        // Cached current
bool sessionActive = false;                 // Transaction state
bool ocppInitialized = false;               // OCPP readiness
int userChoice = 0;                         // Menu selection
unsigned long lastPrint = 0;                // UI update timing
uint8_t stopCmd = 0;                        // Stop command flag
```

---

## Thread Safety Implementation

### Protected Resources
Access to shared state is guarded with mutex locks:

```cpp
// Example: Updating vehicle confirmation
if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    vehicleConfirmed = true;
    xSemaphoreGive(dataMutex);
}

// Example: Reading energy value
if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    float current_energy = energyWh;  // Safe read
    xSemaphoreGive(dataMutex);
}
```

### Safe Serial Output
```cpp
// Helper functions in header.h prevent Serial mutex contention
inline void safePrint(const char *str) {
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Serial.print(str);
        xSemaphoreGive(serialMutex);
    }
}

inline void safePrintf(const char *format, ...) {
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        va_list args;
        va_start(args, format);
        Serial.printf(format, args);
        va_end(args);
        xSemaphoreGive(serialMutex);
    }
}
```

---

## Access Patterns

### Task-Wise Access
- **CAN RX Task** (`can_rx_task`): Reads messages, updates vehicle/charger state
- **Charger Comm Task** (`chargerCommTask`): Sends feedback, updates power metrics
- **Serial/UI Task** (`main.cpp loop`): Reads all globals for display/menu
- **OCPP Task** (`startOCPP`): Reads/writes transaction state, telemetry

All access is serialized via `dataMutex` for consistency.

### Critical Sections
```cpp
// CAN RX update
xSemaphoreTake(dataMutex, portMAX_DELAY);
{
    vehicleConfirmed = (message_id == EXPECTED);
    chargerTemp = decode_temperature(data);
    lastBMS = millis();
}
xSemaphoreGive(dataMutex);

// OCPP telemetry
xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100));
{
    float voltage = terminalVolt;  // Safe snapshot
    float current = terminalCurr;
}
xSemaphoreGive(dataMutex);
```

---

## Adding New Global Variables

### When to Add
- System-wide state needed by 2+ source files
- Persistent across function calls
- Must be protected by mutex for multi-threaded access

### How to Add
1. **Declare in `include/header.h`**:
   ```cpp
   extern float myNewVariable;
   ```

2. **Define in `src/twai_init.cpp`**:
   ```cpp
   float myNewVariable = 0.0f;  // Add near related variables
   ```

3. **Protect access** with `dataMutex` or create new mutex if needed

4. **Document** the purpose and access patterns

### Anti-Pattern: What NOT to Do
❌ Define global variables in multiple `.cpp` files  
❌ Initialize static members in header files  
❌ Skip mutex protection for multi-threaded access  
❌ Use global variables for local scope data  
❌ Create circular dependencies between globals  

---

## Future Improvements

### If Codebase Grows
Consider organizing globals into modules:
```
src/system/
  - global_state.cpp (core variables)
  - synchronization.cpp (mutex initialization)
  - power_state.cpp (voltage/current/temp)
  - transaction_state.cpp (OCPP-specific)
```

With separate extern declarations in:
```
include/system/
  - global_state.h
  - synchronization.h
  - power_state.h
  - transaction_state.h
```

### Runtime Validation
Add startup checks:
```cpp
void validateGlobalState() {
    assert(dataMutex != nullptr);
    assert(serialMutex != nullptr);
    // Check other critical globals
}
```

---

## Summary

✅ **Current Pattern Works Because:**
1. Single definition location (twai_init.cpp)
2. Clear extern declarations (header.h)
3. Consistent mutex protection
4. No header-based initialization

This avoids:
- ❌ One Definition Rule (ODR) violations
- ❌ Multiple symbol definitions linker errors
- ❌ Race conditions in multi-threaded context
- ❌ Initialization order issues
