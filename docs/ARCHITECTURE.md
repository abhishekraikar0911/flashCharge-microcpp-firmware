# Architecture Overview

## System Design

This OCPP charging station firmware follows a **modular, layered architecture** with clear separation of concerns. The system is designed for reliability, maintainability, and extensibility in production environments.

## Architectural Layers

```
┌─────────────────────────────────────────────┐
│         Application Layer (main.cpp)         │
│  - Task initialization and FreeRTOS setup   │
│  - Main event loop and scheduler             │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│      Module Layer (Functional Features)      │
│  ┌──────────────────────────────────────┐  │
│  │ OCPP Manager      UI Console         │  │
│  │ OTA Manager       Event System       │  │
│  │ System Health     Logger             │  │
│  └──────────────────────────────────────┘  │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│      Driver Layer (Hardware Interfaces)      │
│  ┌──────────────────────────────────────┐  │
│  │ CAN Driver        BMS Interface      │  │
│  │ Charger Interface                    │  │
│  └──────────────────────────────────────┘  │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│      Hardware Layer (Physical Devices)       │
│  ┌──────────────────────────────────────┐  │
│  │ ESP32 SoC     CAN Bus    WiFi/BLE    │  │
│  │ UART Console  GPIO       FreeRTOS    │  │
│  └──────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
```

## Component Responsibilities

### Core Services
- **Logger**: Centralized logging with tags and levels
- **Event System**: Decoupled inter-module communication
- **System Health**: Monitoring and diagnostics

### Drivers (Hardware Abstraction)
- **CAN Driver**: TWAI bus communication, ring buffer management
- **BMS Interface**: Battery state acquisition and monitoring
- **Charger Interface**: Power supply control and status

### Modules (Application Logic)
- **OCPP Manager**: Protocol implementation, transaction handling
- **UI Console**: Serial interface, user commands
- **OTA Manager**: Firmware updates and verification

### Configuration
- **hardware.h**: Pin definitions, constraints, limits
- **timing.h**: Communication intervals, timeouts, flags
- **version.h**: Build metadata and firmware version

## Key Design Patterns

### 1. Namespace Encapsulation
```cpp
namespace CAN {
    bool init();
    bool sendMessage(...);
    CanStatus getStatus();
}
```
Benefits:
- Prevents naming conflicts
- Logical grouping of related functions
- Clear API boundaries

### 2. Interface Segregation
Each component has a **clean public interface** in headers with:
- Initialization function
- Core operational functions
- Status/state queries
- Error reporting

Implementations are private in `.cpp` files with:
- Static module variables
- Internal helper functions
- Task-local state

### 3. Event-Driven Architecture
```cpp
EventSystem::post({
    EVENT_BMS_VOLTAGE_WARNING,
    timestamp_ms,
    source_id,
    ...
});

EventSystem::subscribe(EVENT_BMS_VOLTAGE_WARNING, [](const SystemEvent& e) {
    // Handle event
});
```

**Benefits**:
- Loose coupling between modules
- Easy to add new event handlers
- Scalable to multiple consumers
- Testable in isolation

### 4. Centralized State Management
Global state is:
- Defined in ONE source file (e.g., `bms_state.cpp`)
- Declared extern in headers
- Never duplicated or redefined
- Accessed through getter functions

### 5. Resource Acquisition Is Initialization (RAII)
Hardware resources are acquired at initialization:
```cpp
CAN::init()      // Allocates ring buffer, starts TWAI driver
BMS::init()      // Starts message handlers
Charger::init()  // Enables control interface
```

And released during cleanup (via FreeRTOS task deletion).

## Data Flow

### CAN Message Reception
```
TWAI Hardware ISR
        ↓
    Ring Buffer
        ↓
    CAN::receiveMessage()
        ↓
    BMS::handleCanMessage() + Charger::handleCanMessage()
        ↓
    State Updates + Events Posted
        ↓
    OCPP::sendMeterValues()
```

### OCPP Communication
```
Central System
        ↓
    WiFi + TLS
        ↓
    OCPP::handleMessage()
        ↓
    Transaction Control / Configuration
        ↓
    Charger::setChargingCurrent()
```

### Serial Console Commands
```
User Input (UART)
        ↓
    UI::update()
        ↓
    Command Parsing
        ↓
    Module Function Calls
        ↓
    Status Display / Acknowledgment
```

## Concurrency Model

The system uses **FreeRTOS multitasking**:

```cpp
// Main task creation (main.cpp)
xTaskCreate(canRxTask, "CAN_RX", TASK_STACK_SIZE_CAN_RX, nullptr, 
            TASK_PRIORITY_CAN_RX, nullptr);
xTaskCreate(chargerTask, "CHARGER", TASK_STACK_SIZE_CHARGER_COMM, nullptr,
            TASK_PRIORITY_CHARGER_COMM, nullptr);
xTaskCreate(ocppTask, "OCPP", TASK_STACK_SIZE_OCPP, nullptr,
            TASK_PRIORITY_OCPP, nullptr);
// ... more tasks
vTaskStartScheduler();
```

### Task Responsibilities

| Task | Priority | Purpose | Blocking |
|------|----------|---------|----------|
| Watchdog | 6 | Health monitoring | No (ISR) |
| CAN RX | 5 | Message reception | Partial (queue wait) |
| Charger Comm | 4 | Power control | Partial (timeout) |
| OCPP | 3 | Central system | Yes (network I/O) |
| UI Console | 2 | User interface | Yes (serial I/O) |

### Synchronization
- **CAN Ring Buffer**: Interrupt-safe, no locks needed
- **State Variables**: Atomic reads/writes or protected by task exclusion
- **Serial Access**: Guarded by semaphore (`serialMutex`)

## Error Handling Strategy

### Detection
```cpp
// Monitor function returns for errors
if (!CAN::sendMessage(id, data, len)) {
    LOG_ERROR(LOG_TAG_CAN, "Send failed");
    EventSystem::post({EVENT_CAN_ERROR, ...});
}

// Check component status
if (!BMS::isAlive()) {
    // BMS communication timeout
}
```

### Recovery
1. **Graceful Degradation**: Continue operation if non-critical
2. **Retry Logic**: For transient failures
3. **Fallback Modes**: Safe defaults when components fail
4. **Watchdog Reset**: System restart on critical failure

### Reporting
- Log errors with context (module tag, error code)
- Post events for critical failures
- Update system health status
- Report to central system via OCPP

## Performance Considerations

### Memory
- **Stack**: Per-task allocations, typically 2-8KB
- **Heap**: Minimal dynamic allocation, mostly static
- **Global State**: Pre-allocated, fixed-size buffers

### CPU
- **CAN ISR**: < 1ms per message interrupt
- **Task Switching**: ~1% overhead at 1000Hz interrupt rate
- **OCPP**: Asynchronous (non-blocking network I/O)

### Timing
- CAN messages: 1-10ms response time
- Charger control: 100-500ms reaction time
- OCPP updates: 10-60 second intervals
- Watchdog: 30-second timeout

## Configuration Management

Hardware and timing parameters are centralized:
```cpp
// include/config/hardware.h
#define CAN_TX_PIN GPIO_NUM_21
#define CAN_BAUDRATE 250000
#define MAX_VOLTAGE_V 85.5f

// include/config/timing.h
#define BMS_TIMEOUT_MS 5000
#define OCPP_HEARTBEAT_INTERVAL_S 60
#define ENABLE_OTA_UPDATES 1
```

**Benefits**:
- Single source of truth for configuration
- No magic numbers scattered in code
- Easy to adjust for different hardware
- Compile-time optimization

## Testing Strategy

### Unit Testing
- Test individual modules in isolation
- Mock external dependencies
- Verify state machines and transitions

### Integration Testing
- Test CAN message handling
- Test OCPP transaction flow
- Test charger control sequences
- Test error recovery

### System Testing
- Boot sequence validation
- Long-running stability tests
- Network failure scenarios
- Power failure recovery

### Performance Testing
- Throughput benchmarks
- Memory usage profiling
- Latency measurements
- Thermal stability

## Extensibility

To add new functionality:

1. **Define Interface** (`include/modules/new_feature.h`)
   ```cpp
   namespace NewFeature {
       bool init();
       void update();
       FeatureStatus getStatus();
   }
   ```

2. **Implement** (`src/modules/new_feature.cpp`)
   ```cpp
   // Private state and helpers
   static FeatureStatus status;
   static void updateInternal() { ... }
   
   namespace NewFeature {
       bool init() { ... }
       void update() { updateInternal(); }
   }
   ```

3. **Integrate** (in `main.cpp`)
   ```cpp
   NewFeature::init();
   xTaskCreate(newFeatureTask, "NEW_FEATURE", ...);
   ```

4. **Communicate** (via events or function calls)
   ```cpp
   EventSystem::subscribe(EVENT_TRIGGER, [](const SystemEvent& e) {
       NewFeature::handle(e);
   });
   ```

## Security Architecture

### Network Security
- **WiFi**: WPA2 encryption
- **OCPP**: WSS (WebSocket Secure) over HTTPS
- **Authentication**: Central system validates charge point ID

### Firmware Security
- **Update Verification**: SHA256 checksum validation
- **Secure Boot**: ESP32 hardware security (if enabled)
- **Flash Encryption**: Optional via ESP32 configuration

### Runtime Security
- **Watchdog**: Hardware watchdog prevents hangs
- **Stack Guard**: Detection of stack overflow
- **Bounds Checking**: Safe buffer operations

## Monitoring and Observability

### Logging
```cpp
LOG_INFO(LOG_TAG_CAN, "Message received: ID=0x%03X", can_id);
LOG_ERROR(LOG_TAG_BMS, "Communication timeout after %u ms", timeout);
```

### Health Checks
```cpp
SystemHealth health = SystemHealth::check();
if (health.overall_status == HEALTH_CRITICAL) {
    // Trigger recovery
}
```

### Diagnostics
```
> diagnostics
System Health: OK
  Uptime: 24h 15m 32s
  Heap: 45% used (92KB / 204KB)
  Core Temp: 52°C
  Total Errors: 0
  Total Warnings: 2

CAN Bus: Active
  Messages RX: 1,247,382
  Messages TX: 456,891
  Errors: 0

BMS: Online
  Voltage: 72.4V
  SOC: 65%
  Temp: 35°C
  
OCPP: Connected
  Session: 3847
  Energy: 123.45 kWh
```

---

**For questions or clarifications**, refer to the code comments and Doxygen documentation in header files.
