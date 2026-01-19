# API Reference Guide

Complete reference for all public APIs in the OCPP charging station firmware.

## Configuration APIs

### Hardware Configuration (`include/config/hardware.h`)

```cpp
// CAN Bus Settings
#define CAN_TX_PIN GPIO_NUM_21
#define CAN_RX_PIN GPIO_NUM_22
#define CAN_BAUDRATE 250000
#define CAN_RX_QUEUE_SIZE 64
#define CAN_TX_QUEUE_SIZE 16

// Safety Limits
#define MIN_VOLTAGE_V 56.0f
#define MAX_VOLTAGE_V 85.5f
#define MAX_CURRENT_A 300.0f
#define MAX_TEMPERATURE_C 70.0f
#define BATTERY_CAPACITY_AH 30.0f

// Watchdog
#define WATCHDOG_TIMEOUT_S 30

// Task Configuration
#define TASK_STACK_SIZE_CAN_RX 4096
#define TASK_PRIORITY_CAN_RX 5
```

### Timing Configuration (`include/config/timing.h`)

```cpp
// Communication Timeouts
#define BMS_TIMEOUT_MS 5000
#define CHARGER_RESPONSE_TIMEOUT_MS 3000
#define HEARTBEAT_INTERVAL_MS 100
#define SOC_REQUEST_INTERVAL_MS 2000

// OCPP Configuration
#define OCPP_METER_VALUE_INTERVAL_S 10
#define OCPP_HEARTBEAT_INTERVAL_S 60
#define OCPP_RECONNECT_INTERVAL_MS 5000
#define OCPP_MAX_RECONNECT_ATTEMPTS 10

// Feature Flags
#define ENABLE_OTA_UPDATES 1
#define ENABLE_REMOTE_LOGGING 1
#define ENABLE_DIAGNOSTICS 1
#define ENABLE_WATCHDOG 1
#define ENABLE_CRASH_RECOVERY 1
```

### Version Information (`include/config/version.h`)

```cpp
#define FIRMWARE_VERSION "1.0.0"
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 0
#define FIRMWARE_VERSION_PATCH 0
#define CHARGER_MODEL "RIVOT_100A"
#define MANUFACTURER "Rivot Motors"
```

---

## CAN Driver API

### Interface (`include/drivers/can_driver.h`)

```cpp
namespace CAN {
    // Initialization
    bool init();
    bool deinit();
    bool isActive();

    // Message Operations
    bool sendMessage(uint32_t id, const uint8_t* data, uint8_t length, 
                     bool is_extended = false);
    bool receiveMessage(twai_message_t* frame, uint32_t* timestamp_ms = nullptr);
    bool peekMessage(twai_message_t* frame);

    // Status & Management
    CanStatus getStatus();
    void flushRxBuffer();
    uint8_t getRxBufferUsage();
    void resetStatistics();
}

struct RxBufItem {
    twai_message_t frame;
    uint32_t timestamp_ms;
};

struct CanStatus {
    bool is_initialized;
    bool is_active;
    uint32_t total_rx_messages;
    uint32_t total_tx_messages;
    uint32_t error_count;
    uint32_t last_activity_ms;
};
```

### Usage Example

```cpp
// Initialize
if (!CAN::init()) {
    LOG_ERROR(LOG_TAG_CAN, "Failed to initialize");
    return false;
}

// Send message
uint8_t data[] = {0x01, 0x02, 0x03};
if (CAN::sendMessage(0x100, data, 3)) {
    LOG_DEBUG(LOG_TAG_CAN, "Message sent");
}

// Receive messages
twai_message_t frame;
uint32_t timestamp;
while (CAN::receiveMessage(&frame, &timestamp)) {
    LOG_INFO(LOG_TAG_CAN, "RX: ID=0x%03X, DLC=%d, TS=%u",
             frame.identifier, frame.data_length_code, timestamp);
}

// Get status
CanStatus status = CAN::getStatus();
LOG_INFO(LOG_TAG_CAN, "Active: %s, RX: %u, TX: %u, Errors: %u",
         status.is_active ? "yes" : "no",
         status.total_rx_messages,
         status.total_tx_messages,
         status.error_count);
```

---

## BMS Interface API

### Interface (`include/drivers/bms_interface.h`)

```cpp
namespace BMS {
    // Initialization & Status
    bool init();
    BatteryState getState();
    bool isAlive();
    uint32_t getTimeSinceLastMessage();
    bool isSafeState();
    uint8_t getErrorCode();

    // Commands
    void handleCanMessage(uint32_t can_id, const uint8_t* data, uint8_t length);
    void requestSOC();
    void requestVoltages();
}

struct BatteryVoltages {
    float pack_voltage;
    float max_cell_voltage;
    float min_cell_voltage;
    uint8_t cell_count;
};

struct BatterySOC {
    float soc_percent;
    float soh_percent;
    uint32_t remaining_capacity_ah;
    uint32_t total_capacity_ah;
};

struct BatteryState {
    BatteryVoltages voltages;
    BatteryTemperature temperature;
    BatterySOC soc;
    float discharge_current_a;
    float charge_current_a;
    uint8_t status_flags;
    uint32_t timestamp_ms;
    bool is_valid;
};
```

### Usage Example

```cpp
// Get current battery state
BatteryState battery = BMS::getState();

if (battery.is_valid) {
    LOG_INFO(LOG_TAG_BMS, "SOC: %.1f%% | Volt: %.1fV | Temp: %.1f°C",
             battery.soc.soc_percent,
             battery.voltages.pack_voltage,
             battery.temperature.cell_max_temp);
    
    // Check safety limits
    if (!BMS::isSafeState()) {
        LOG_WARN(LOG_TAG_BMS, "Safety violation detected");
    }
}

// Request specific data
BMS::requestSOC();
BMS::requestVoltages();

// Check connection
if (!BMS::isAlive()) {
    LOG_ERROR(LOG_TAG_BMS, "Lost communication (timeout: %u ms)",
              BMS::getTimeSinceLastMessage());
}
```

---

## Charger Interface API

### Interface (`include/drivers/charger_interface.h`)

```cpp
namespace Charger {
    // Initialization & Status
    bool init();
    ChargerStatus getStatus();
    TerminalStatus getTerminalStatus();
    bool isAlive();
    uint32_t getTimeSinceLastMessage();
    uint8_t getErrorCode();
    const char* getErrorString(uint8_t error_code);

    // Control Commands
    bool setChargingEnabled(bool enable);
    bool setChargingCurrent(float current_a);

    // Message Handling
    void handleCanMessage(uint32_t can_id, const uint8_t* data, uint8_t length);
}

typedef enum {
    CHARGER_STATE_IDLE = 0,
    CHARGER_STATE_CHARGING = 1,
    CHARGER_STATE_CHARGING_ENABLED = 2,
    CHARGER_STATE_FAULT = 3,
    CHARGER_STATE_OFFLINE = 4
} ChargerState;

struct ChargerStatus {
    ChargerState state;
    float output_voltage;
    float output_current;
    float temperature;
    uint16_t fault_code;
    bool relay_enabled;
    bool grid_connected;
    uint32_t timestamp_ms;
    bool is_valid;
};
```

### Usage Example

```cpp
// Enable charging with 30A limit
if (Charger::setChargingCurrent(30.0f)) {
    LOG_INFO(LOG_TAG_CHG, "Charging current set to 30A");
}

if (Charger::setChargingEnabled(true)) {
    LOG_INFO(LOG_TAG_CHG, "Charging enabled");
}

// Monitor charger status
ChargerStatus status = Charger::getStatus();
LOG_INFO(LOG_TAG_CHG, "Output: %.1fV @ %.1fA | Temp: %.1f°C | State: %d",
         status.output_voltage,
         status.output_current,
         status.temperature,
         status.state);

// Check for faults
if (status.fault_code != 0) {
    const char* error = Charger::getErrorString(status.fault_code);
    LOG_ERROR(LOG_TAG_CHG, "Fault: %s (0x%04X)", error, status.fault_code);
}
```

---

## Logger API

### Interface (`include/core/logger.h`)

```cpp
namespace Logger {
    // Initialization
    void init(LogLevel initial_level = LOG_LEVEL_INFO);
    
    // Configuration
    void setLevel(LogLevel level);
    void setEnabled(bool enable);
    
    // Logging
    void log(const char* tag, LogLevel level, const char* format, ...);
    
    // Status
    uint32_t getMessageCount();
    void flush();
}

typedef enum {
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_WARN = 3,
    LOG_LEVEL_ERROR = 4,
    LOG_LEVEL_CRITICAL = 5,
    LOG_LEVEL_SILENT = 6
} LogLevel;

// Log Tags
#define LOG_TAG_CAN "CAN"
#define LOG_TAG_BMS "BMS"
#define LOG_TAG_CHG "CHG"
#define LOG_TAG_OCPP "OCPP"
#define LOG_TAG_SYS "SYS"
#define LOG_TAG_NET "NET"
#define LOG_TAG_UI "UI"
#define LOG_TAG_OTA "OTA"

// Convenience Macros
#define LOG_TRACE(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(tag, fmt, ...) Logger::log(tag, LOG_LEVEL_CRITICAL, fmt, ##__VA_ARGS__)
```

### Usage Example

```cpp
// Initialize
Logger::init(LOG_LEVEL_DEBUG);

// Log at different levels
LOG_TRACE(LOG_TAG_SYS, "Function entry");
LOG_DEBUG(LOG_TAG_SYS, "Variable value: %d", var);
LOG_INFO(LOG_TAG_SYS, "System initialized");
LOG_WARN(LOG_TAG_SYS, "Temperature high: %.1f°C", temp);
LOG_ERROR(LOG_TAG_SYS, "Failed to initialize: %s", error_str);
LOG_CRITICAL(LOG_TAG_SYS, "Critical system failure!");

// Adjust runtime
Logger::setLevel(LOG_LEVEL_INFO);  // Less verbose
Logger::setEnabled(false);          // Disable logging
```

---

## Event System API

### Interface (`include/core/event_system.h`)

```cpp
namespace EventSystem {
    // Initialization
    void init();
    
    // Subscriptions
    uint32_t subscribe(SystemEventType event_type, EventHandler handler);
    void unsubscribe(uint32_t subscription_id);
    
    // Publishing
    void post(const SystemEvent& event);
    
    // Management
    void flush();
    uint32_t getQueuedEventCount();
}

typedef enum {
    // CAN Events
    EVENT_CAN_INITIALIZED,
    EVENT_CAN_ERROR,
    EVENT_CAN_MESSAGE_RECEIVED,
    
    // BMS Events
    EVENT_BMS_VOLTAGE_WARNING,
    EVENT_BMS_TEMPERATURE_WARNING,
    EVENT_BMS_SOC_UPDATE,
    EVENT_BMS_COMMUNICATION_LOST,
    
    // Charger Events
    EVENT_CHARGER_ENABLED,
    EVENT_CHARGER_DISABLED,
    EVENT_CHARGER_FAULT,
    EVENT_CHARGER_COMMUNICATION_LOST,
    
    // OCPP Events
    EVENT_OCPP_CONNECTED,
    EVENT_OCPP_DISCONNECTED,
    EVENT_OCPP_TRANSACTION_STARTED,
    EVENT_OCPP_TRANSACTION_STOPPED,
    
    // System Events
    EVENT_SYSTEM_RESET,
    EVENT_SYSTEM_SHUTDOWN,
    EVENT_SYSTEM_ERROR,
    EVENT_WATCHDOG_TRIGGERED,
    
    EVENT_MAX
} SystemEventType;

struct SystemEvent {
    SystemEventType type;
    uint32_t timestamp_ms;
    uint32_t source_id;
    int32_t data1;
    int32_t data2;
    void* user_data;
};

typedef std::function<void(const SystemEvent&)> EventHandler;
```

### Usage Example

```cpp
// Subscribe to events
uint32_t sub_id = EventSystem::subscribe(EVENT_BMS_VOLTAGE_WARNING,
    [](const SystemEvent& e) {
        LOG_WARN(LOG_TAG_BMS, "Voltage warning: %d", e.data1);
        // Take corrective action
    });

// Later, post an event
SystemEvent event = {
    .type = EVENT_BMS_VOLTAGE_WARNING,
    .timestamp_ms = millis(),
    .source_id = 1,
    .data1 = 92000,  // Voltage in mV
    .data2 = 0,
    .user_data = nullptr
};
EventSystem::post(event);

// Unsubscribe when done
EventSystem::unsubscribe(sub_id);
```

---

## OCPP Manager API

### Interface (`include/modules/ocpp_manager.h`)

```cpp
namespace OCPP {
    // Initialization & Connection
    bool init(const char* central_system_url, const char* charge_point_id);
    bool connect();
    bool disconnect();
    bool isConnected();
    
    // Transaction Management
    uint32_t startTransaction(const char* connector_id, const char* user_id = nullptr);
    bool stopTransaction(uint32_t transaction_id);
    
    // Meter Values
    bool sendMeterValues(uint32_t transaction_id, uint32_t energy_wh, uint32_t power_w);
    
    // Configuration
    bool handleConfigurationChange(const char* key, const char* value);
    
    // Status & Errors
    OcppStatus getStatus();
    const char* getLastError();
    
    // Processing
    void update();
}

typedef enum {
    OCPP_IDLE = 0,
    OCPP_CHARGING = 1,
    OCPP_SUSPENDED = 2,
    OCPP_FINISHED = 3,
    OCPP_ERROR = 4
} OcppTransactionState;

struct OcppTransaction {
    uint32_t transaction_id;
    OcppTransactionState state;
    uint32_t start_time;
    uint32_t end_time;
    uint32_t energy_wh;
    char connector_id[16];
    char user_id[32];
    bool is_active;
};

struct OcppStatus {
    OcppConnectionState connection_state;
    bool authenticated;
    OcppTransaction current_transaction;
    uint32_t heartbeat_interval_s;
    uint32_t meter_value_interval_s;
    const char* central_system_url;
    uint32_t last_heartbeat_ms;
    uint32_t reconnect_attempts;
};
```

### Usage Example

```cpp
// Initialize
if (!OCPP::init("ws://central-system.example.com:8080", "RIVOT_100A_01")) {
    LOG_ERROR(LOG_TAG_OCPP, "Initialization failed");
    return;
}

// Connect to central system
if (OCPP::connect()) {
    LOG_INFO(LOG_TAG_OCPP, "Connecting to central system...");
}

// Start transaction
uint32_t transaction_id = OCPP::startTransaction("1", "USER123");
if (transaction_id > 0) {
    LOG_INFO(LOG_TAG_OCPP, "Transaction started: %u", transaction_id);
}

// Send meter values periodically
if (OCPP::sendMeterValues(transaction_id, 12345, 7500)) {
    LOG_DEBUG(LOG_TAG_OCPP, "Meter values sent");
}

// Stop transaction
if (OCPP::stopTransaction(transaction_id)) {
    LOG_INFO(LOG_TAG_OCPP, "Transaction stopped");
}

// In main loop
OCPP::update();
```

---

## UI Console API

### Interface (`include/modules/ui_console.h`)

```cpp
namespace UI {
    // Initialization & Control
    bool init(uint32_t baud_rate = 115200);
    void setEnabled(bool enable);
    bool isEnabled();
    
    // Display Functions
    void printStatus();
    void printDiagnostics();
    void printHelp();
    void printVersion();
    void println(const char* format, ...);
    void clear();
    
    // Input Processing
    void update();
    UiCommand getLastCommand();
}

typedef enum {
    UI_CMD_UNKNOWN = 0,
    UI_CMD_STATUS = 1,
    UI_CMD_START_CHARGE = 2,
    UI_CMD_STOP_CHARGE = 3,
    UI_CMD_RESET = 4,
    UI_CMD_DIAGNOSTICS = 5,
    UI_CMD_HELP = 6,
    UI_CMD_VERSION = 7,
    UI_CMD_CONFIG = 8,
    UI_CMD_LOGS = 9
} UiCommand;
```

### Usage Example

```cpp
// Initialize
if (!UI::init(115200)) {
    return false;
}

// In main loop
UI::update();

// Print information
UI::println("Charging started at %u W", power);
UI::printStatus();
UI::printDiagnostics();

// Process commands
UiCommand cmd = UI::getLastCommand();
switch (cmd) {
    case UI_CMD_STATUS:
        UI::printStatus();
        break;
    case UI_CMD_RESET:
        ESP.restart();
        break;
    case UI_CMD_HELP:
        UI::printHelp();
        break;
    default:
        break;
}
```

---

## System Health API

### Interface (`include/core/system_health.h`)

```cpp
namespace SystemHealth {
    // Initialization & Monitoring
    void init();
    SystemHealth check();
    HealthStatus getStatus();
    void setEnabled(bool enable);
    
    // Event Logging
    void logEvent(HealthStatus status, const char* message);
    void resetCounters();
}

typedef enum {
    HEALTH_OK = 0,
    HEALTH_WARNING = 1,
    HEALTH_CRITICAL = 2,
    HEALTH_FAULT = 3
} HealthStatus;

struct SystemHealth {
    HealthStatus overall_status;
    uint32_t uptime_seconds;
    uint8_t heap_usage_percent;
    uint32_t free_heap_bytes;
    uint8_t core_temp_c;
    bool watchdog_active;
    uint32_t total_errors;
    uint32_t total_warnings;
    uint32_t last_check_ms;
};
```

### Usage Example

```cpp
// Initialize health monitoring
SystemHealth::init();

// Periodic health checks
if (SystemHealth::check().overall_status != HEALTH_OK) {
    LOG_WARN(LOG_TAG_SYS, "System health degraded");
}

// Log events
SystemHealth::logEvent(HEALTH_WARNING, "High memory usage detected");

// Log errors
SystemHealth::logEvent(HEALTH_ERROR, "BMS communication timeout");
```

---

## OTA Manager API

### Interface (`include/modules/ota_manager.h`)

```cpp
namespace OTA {
    // Initialization
    bool init();
    
    // Update Management
    bool checkForUpdates(const char* update_server);
    bool startUpdate(const char* firmware_url);
    bool cancel();
    bool verifyFirmware(const char* checksum);
    
    // Status & Information
    OtaInfo getStatus();
    const char* getAvailableVersion();
    const char* getCurrentVersion();
    
    // Processing
    void update();
}

typedef enum {
    OTA_IDLE = 0,
    OTA_CHECKING = 1,
    OTA_DOWNLOADING = 2,
    OTA_INSTALLING = 3,
    OTA_COMPLETE = 4,
    OTA_ERROR = 5
} OtaStatus;

struct OtaInfo {
    OtaStatus status;
    uint32_t progress_percent;
    uint32_t downloaded_bytes;
    uint32_t total_bytes;
    const char* error_message;
    uint32_t start_time_ms;
};
```

### Usage Example

```cpp
// Initialize
OTA::init();

// Check for updates
if (OTA::checkForUpdates("https://updates.example.com/check")) {
    LOG_INFO(LOG_TAG_OTA, "Checking for updates...");
}

// Monitor status
OtaInfo status = OTA::getStatus();
if (status.status == OTA_DOWNLOADING) {
    LOG_INFO(LOG_TAG_OTA, "Progress: %u%% (%u/%u bytes)",
             status.progress_percent,
             status.downloaded_bytes,
             status.total_bytes);
}

// Process updates in main loop
OTA::update();
```

---

## Common Patterns

### Error Handling
```cpp
bool operation() {
    if (!CAN::isActive()) {
        LOG_ERROR(LOG_TAG_CAN, "CAN not initialized");
        return false;
    }
    
    if (!CAN::sendMessage(id, data, len)) {
        LOG_ERROR(LOG_TAG_CAN, "Failed to send message");
        return false;
    }
    
    return true;
}
```

### Periodic Tasks
```cpp
void periodicTask(void* param) {
    uint32_t last_run = millis();
    
    while (1) {
        uint32_t now = millis();
        
        if (now - last_run >= INTERVAL_MS) {
            // Do work
            last_run = now;
        }
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
```

### State Machine
```cpp
switch (current_state) {
    case STATE_IDLE:
        if (condition) {
            // Transition to next state
            current_state = STATE_ACTIVE;
        }
        break;
    
    case STATE_ACTIVE:
        // Handle active state
        if (error_condition) {
            current_state = STATE_ERROR;
        }
        break;
    
    case STATE_ERROR:
        // Handle error recovery
        break;
}
```

---

**For more details**, see the corresponding header files with full Doxygen documentation.
