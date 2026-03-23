#include "../../include/drivers/can_mcp2515_driver.h"
#include "../../include/header.h"
#include "../../include/config/hardware.h"
#include "../../include/health_monitor.h"
#include "../../include/debug_logger.h"
#include "../../include/utils/can_status_logger.h"
#include "../../include/modules/system_state.h"
#include <SPI.h>

// MCP2515 instance
static MCP2515 *mcp2515 = nullptr;

// FreeRTOS Queue for received messages (replaces manual ring buffer)
static QueueHandle_t rxQueue = nullptr;

// Driver status
static CanMcp2515Status driverStatus = {false, false, 0, 0, 0, 0};
static SemaphoreHandle_t mcp2515RecoveryMutex = nullptr;

// ISR and Task handle
static TaskHandle_t can2TaskHandle = nullptr;
static volatile bool messageAvailable = false;

// ISR triggered by MCP2515 INT pin
static void IRAM_ATTR mcp2515_isr()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (can2TaskHandle != nullptr)
    {
        vTaskNotifyGiveFromISR(can2TaskHandle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken)
        {
            portYIELD_FROM_ISR();
        }
    }
}

namespace CAN_MCP2515
{
    bool init()
    {
        Serial.println("[CAN2] Initializing MCP2515...");

        // Create recovery mutex
        if (mcp2515RecoveryMutex == nullptr)
        {
            mcp2515RecoveryMutex = xSemaphoreCreateMutex();
        }

        // Create RX queue (thread-safe, replaces volatile ring buffer)
        if (rxQueue == nullptr)
        {
            rxQueue = xQueueCreate(CAN_RX_QUEUE_SIZE, sizeof(CanMessage));
            if (rxQueue == nullptr)
            {
                Serial.println("[CAN2] ❌ Failed to create RX queue!");
                return false;
            }
        }

        if (mcp2515RecoveryMutex && xSemaphoreTake(mcp2515RecoveryMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            // Initialize SPI explicitly with hardware pins to avoid conflicts
            Serial.printf("[CAN2] Starting SPI bus (SCK:%d, MISO:%d, MOSI:%d, CS:%d)...\n", 
                CAN2_SCK_PIN, CAN2_MISO_PIN, CAN2_MOSI_PIN, CAN2_CS_PIN);
            
            SPI.begin(CAN2_SCK_PIN, CAN2_MISO_PIN, CAN2_MOSI_PIN, CAN2_CS_PIN); 
            delay(100); 

            // DIAGNOSTIC: Manual SPI read to verify chip presence
            Serial.println("[CAN2] 🧪 Manual SPI test (Read CANSTAT 0x0E at 1MHz)...");
            SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
            digitalWrite(CAN2_CS_PIN, LOW);
            SPI.transfer(0x03); // READ command
            SPI.transfer(0x0E); // CANSTAT address
            uint8_t stat = SPI.transfer(0x00);
            digitalWrite(CAN2_CS_PIN, HIGH);
            SPI.endTransaction();
            Serial.printf("[CAN2] 🧪 Manual SPI result: 0x%02X (Expected ~0x80 or 0x00)\n", stat);
            
            // Create MCP2515 instance with 2MHz SPI clock to reduce EMI susceptibility (was 10MHz)
            if (mcp2515 == nullptr)
            {
                mcp2515 = new MCP2515(CAN2_CS_PIN, 2000000);
            }
            // CRITICAL: Hardware reset first
            Serial.println("[CAN2] Resetting MCP2515...");
            MCP2515::ERROR result = mcp2515->reset();
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Reset failed: %d\n", result);
                
                // Try again with standard SPI speed
                delay(100);
                Serial.println("[CAN2] 🔄 Retry reset...");
                result = mcp2515->reset();
            }

            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Reset failed again: %d\n", result);
                
                // DIAGNOSTIC EXTRAS:
                Serial.println("[CAN2] 🔍 SPI Health Check:");
                if (stat == 0xFF) {
                    Serial.println("[CAN2] ❌ SPI Error: Received 0xFF (MISO pin stuck HIGH - check wiring/power)");
                } else if (stat == 0x00) {
                    Serial.println("[CAN2] ❌ SPI Error: Received 0x00 (MISO pin stuck LOW - check wiring/CS/SCK)");
                } else {
                    Serial.printf("[CAN2] 📊 SPI responded with 0x%02X, but library reset failed.\n", stat);
                }

                Serial.println("[CAN2] ⚠️  Possible causes:");
                Serial.println("[CAN2]    - MCP2515 not connected (check wiring)");
                Serial.println("[CAN2]    - Module not powered (VCC/GND)");
                Serial.println("[CAN2]    - SPI bus conflict\n");
                
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }
            delay(50); // Chip stabilization 

            // Set bitrate (CRITICAL: 8MHz crystal)
            // Note: Library constants for 8MHz/250k have been patched in mcp2515.h for 81.25% sample point
            result = mcp2515->setBitrate(CAN_250KBPS, MCP_8MHZ);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Bitrate config failed: %d (check 8MHz crystal)\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // Configure hardware filters strictly for allowed BMS messages only
            // ID 1: 0x1806E5F4 (BMS Vmax/Imax)
            // ID 2: 0x18904001 (SOC Response)
            
            // RXB0: Filters 0, 1 + Mask 0
            result = mcp2515->setFilter(MCP2515::RXF0, true, 0x1806E5F4UL);
            result = mcp2515->setFilter(MCP2515::RXF1, true, 0x18904001UL);
            result = mcp2515->setFilterMask(MCP2515::MASK0, true, 0x1FFFFFFFUL); // Strict match

            // RXB1: Filters 2, 3, 4, 5 + Mask 1
            result = mcp2515->setFilter(MCP2515::RXF2, true, 0x1806E5F4UL);
            result = mcp2515->setFilter(MCP2515::RXF3, true, 0x18904001UL);
            result = mcp2515->setFilter(MCP2515::RXF4, true, 0x1806E5F4UL);
            result = mcp2515->setFilter(MCP2515::RXF5, true, 0x18904001UL);
            result = mcp2515->setFilterMask(MCP2515::MASK1, true, 0x1FFFFFFFUL); // Strict match

            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Filter/Mask config failed: %d\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            Serial.println("[CAN2] ✅ Strict HW Filters: ONLY 0x1806E5F4 and 0x18904001");

            // Set normal mode
            result = mcp2515->setNormalMode();
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Normal mode failed: %d (check wiring)\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // Setup interrupt (ENABLED: Lower latency RX)
            Serial.printf("[CAN2] 🗳️  Interrupt Mode Active (GPIO %d)\n", CAN2_INT_PIN);
            pinMode(CAN2_INT_PIN, INPUT); // GPIO 34 is input-only
            attachInterrupt(digitalPinToInterrupt(CAN2_INT_PIN), mcp2515_isr, FALLING);

            driverStatus.is_initialized = true;
            driverStatus.is_active = true;
            Serial.println("[CAN2] ✅ MCP2515 initialized successfully");

            xSemaphoreGive(mcp2515RecoveryMutex);
            return true;
        }

        return false;
    }

    bool deinit()
    {
        if (mcp2515)
        {
            if (CAN2_INT_PIN >= 0) {
                detachInterrupt(digitalPinToInterrupt(CAN2_INT_PIN));
            }
            delete mcp2515;
            mcp2515 = nullptr;
        }
        driverStatus.is_initialized = false;
        driverStatus.is_active = false;
        return true;
    }

    bool isActive()
    {
        return driverStatus.is_active;
    }

    bool sendMessage(uint32_t id, const uint8_t *data, uint8_t length, bool is_extended)
    {
        if (!mcp2515 || !driverStatus.is_active)
            return false;

        struct can_frame frame;
        frame.can_id = id;
        if (is_extended)
        {
            frame.can_id |= CAN_EFF_FLAG;
        }
        frame.can_dlc = length;
        memcpy(frame.data, data, length);

        MCP2515::ERROR result = MCP2515::ERROR_FAIL;
        if (mcp2515RecoveryMutex && xSemaphoreTake(mcp2515RecoveryMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            result = mcp2515->sendMessage(&frame);
            xSemaphoreGive(mcp2515RecoveryMutex);
        }

        if (result == MCP2515::ERROR_OK)
        {
            driverStatus.total_tx_messages++;
            driverStatus.last_activity_ms = millis();
            return true;
        }

        driverStatus.error_count++;
        return false;
    }

    bool receiveMessage(CanMessage *msg)
    {
        if (rxQueue == nullptr) return false;
        return xQueueReceive(rxQueue, msg, 0) == pdTRUE;
    }

    bool popFrame(RxBufItem &out)
    {
        CanMessage msg;
        if (!receiveMessage(&msg))
            return false;

        out.id = msg.id;
        out.dlc = msg.dlc;
        memcpy(out.data, msg.data, 8);
        out.ext = msg.extended;
        out.rtr = false;
        return true;
    }

    CanMcp2515Status getStatus()
    {
        return driverStatus;
    }

    void flushRxBuffer()
    {
        if (rxQueue != nullptr) xQueueReset(rxQueue);
    }

    uint8_t getRxBufferUsage()
    {
        if (rxQueue == nullptr) return 0;
        UBaseType_t count = uxQueueMessagesWaiting(rxQueue);
        return (count * 100) / CAN_RX_QUEUE_SIZE;
    }

    void resetStatistics()
    {
        driverStatus.total_rx_messages = 0;
        driverStatus.total_tx_messages = 0;
        driverStatus.error_count = 0;
    }

    bool isHealthy()
    {
        const uint32_t TIMEOUT_MS = 3000;
        return (millis() - driverStatus.last_activity_ms) < TIMEOUT_MS;
    }

    static void decodeErrorFlags(uint8_t flags, char* buf, size_t maxLen)
    {
        if (flags == 0) {
            buf[0] = '\0';
            return;
        }
        
        size_t pos = snprintf(buf, maxLen, " [");
        bool first = true;
        
        auto appendFlag = [&](const char* name) {
            if (!first && pos < maxLen) {
                pos += snprintf(buf + pos, maxLen - pos, "|");
            }
            if (pos < maxLen) {
                pos += snprintf(buf + pos, maxLen - pos, "%s", name);
            }
            first = false;
        };

        if (flags & MCP2515::EFLG_RX1OVR) appendFlag("RX1OVR");
        if (flags & MCP2515::EFLG_RX0OVR) appendFlag("RX0OVR");
        if (flags & MCP2515::EFLG_TXBO)   appendFlag("Bus-Off");
        if (flags & MCP2515::EFLG_TXEP)   appendFlag("TX-Passive");
        if (flags & MCP2515::EFLG_RXEP)   appendFlag("RX-Passive");
        if (flags & MCP2515::EFLG_TXWAR)  appendFlag("TX-Warn");
        if (flags & MCP2515::EFLG_RXWAR)  appendFlag("RX-Warn");
        if (flags & 0x01)                 appendFlag("Error-Warn");
        
        if (pos < maxLen) snprintf(buf + pos, maxLen - pos, "]");
    }

    bool readDiagnostics()
    {
        if (!mcp2515 || !driverStatus.is_active)
        {
            Serial.println("[CAN2] ❌ Driver not initialized");
            return false;
        }

        uint8_t eflg = 0, tec = 0, rec = 0, canintf = 0, status = 0;
        bool spiLocked = false;

        if (mcp2515RecoveryMutex && xSemaphoreTake(mcp2515RecoveryMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            // Read error flags and counters
            eflg = mcp2515->getErrorFlags();
            tec = mcp2515->errorCountTX();
            rec = mcp2515->errorCountRX();
            canintf = mcp2515->getInterrupts();
            status = mcp2515->getStatus();
            xSemaphoreGive(mcp2515RecoveryMutex);
            spiLocked = true;
        }

        if (!spiLocked) {
            Serial.println("[CAN2] ❌ Could not acquire SPI mutex for diagnostics");
            return false;
        }
        
        // Determine state string
        const char* stateStr = "INITIALIZING";
        if (eflg & (MCP2515::EFLG_TXEP | MCP2515::EFLG_RXEP)) stateStr = "ERROR_PASSIVE";
        else if (eflg & (MCP2515::EFLG_TXWAR | MCP2515::EFLG_RXWAR)) stateStr = "ERROR_WARNING";
        else if (driverStatus.is_active) stateStr = "RUNNING";
        
        // Print formatted status table
        CANStatusLogger::printMCP2515Status(
            stateStr, tec, rec, 
            driverStatus.total_rx_messages, 
            driverStatus.total_tx_messages
        );

        char eflgStr[100];
        decodeErrorFlags(eflg, eflgStr, sizeof(eflgStr));
        SafeSerial::printf("[CAN2] EFLG (0x%02X):%s\n", eflg, eflgStr);
        SafeSerial::printf("[CAN2] CANINTF: 0x%02X\n", canintf);
        
        // Check if SPI communication is working
        bool spiOk = (eflg != 0xFF && canintf != 0xFF);
        SafeSerial::printf("[CAN2] SPI Communication: %s\n", spiOk ? "✅ OK" : "❌ FAILED");
        SafeSerial::printf("[CAN2] MCP Status: 0x%02X\n", status);
        SafeSerial::println("================================");
        
        return spiOk;
    }

} // namespace CAN_MCP2515

// CAN2 RX Task (BMS messages)
void can2_rx_task(void *arg)
{
    can2TaskHandle = xTaskGetCurrentTaskHandle();
    Serial.println("[CAN2] RX task started");

    struct can_frame frame;
    
    // Boot monitor: track REC/TEC every 5s for first 60s
    unsigned long bootStart = millis();
    unsigned long lastBootLog = 0;
    uint8_t prevRec = 0, prevTec = 0;
    bool bootMonitorDone = false;

    while (true)
    {
        // PERIODIC DIAGNOSTIC: Log status every 30s if idle
        static unsigned long lastAliveLog = 0;
        if (millis() - lastAliveLog > 30000) {
            SafeSerial::println("[CAN2] 🔍 Driver Alive - Waiting for BMS messages...");
            lastAliveLog = millis();
        }

        // Take mutex before accessing MCP2515
        if (mcp2515RecoveryMutex && xSemaphoreTake(mcp2515RecoveryMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (driverStatus.is_initialized && driverStatus.is_active)
            {
                // ── Boot Monitor: Log REC/TEC every 5s for first 60s ──
                if (!bootMonitorDone)
                {
                    unsigned long elapsed = millis() - bootStart;
                    if (elapsed > 60000)
                    {
                        bootMonitorDone = true;
                        Serial.println("[CAN2] 📊 Boot monitor complete (60s)");
                    }
                    else if (millis() - lastBootLog >= 5000)
                    {
                        uint8_t rec = mcp2515->errorCountRX();
                        uint8_t tec = mcp2515->errorCountTX();
                        uint8_t eflg = mcp2515->getErrorFlags();
                        Serial.printf("[CAN2] 📊 Boot +%us | REC:%u TEC:%u EFLG:0x%02X | RX:%u TX:%u\n",
                            (uint32_t)(elapsed / 1000), rec, tec, eflg,
                            (uint32_t)driverStatus.total_rx_messages, (uint32_t)driverStatus.total_tx_messages);
                        
                        // Flag large jumps
                        if (rec > prevRec + 10)
                            Serial.printf("[CAN2] ⚠️  REC jumped +%d (noise or bitrate mismatch?)\n", rec - prevRec);
                        
                        prevRec = rec;
                        prevTec = tec;
                        lastBootLog = millis();
                    }
                }

                // Check for new messages via SPI polling (Limited to 10 per cycle)
                // readMessage will return ERROR_OK if data exists in MCP2515 buffers
                int processed = 0;
                while (processed < 10)
                {
                    messageAvailable = false; // Reset flag just in case

                    MCP2515::ERROR result = mcp2515->readMessage(&frame);
                    if (result != MCP2515::ERROR_OK)
                    {
                        break; // No more messages OR error
                    }

                    // SUCCESS: Received a message
                    processed++;
                    uint32_t rxId = frame.can_id & CAN_EFF_MASK;
                    
                    // Strict software filter: Ignore anything besides the two required IDs
                    if (rxId != 0x1806E5F4UL && rxId != 0x18904001UL)
                    {
                        continue; 
                    }

                    // Clear EFLG if we were previously holding errors but are now successfully receiving
                    if (driverStatus.error_count > 0 || mcp2515->getErrorFlags() != 0) {
                        mcp2515->clearRXnOVRFlags();
                        mcp2515->clearMERR();
                        mcp2515->clearERRIF();
                        // Reset error tracking since we got a valid BMS packet
                        driverStatus.error_count = 0;
                        SafeSerial::println("[CAN2] ✅ Valid BMS message received — error flags cleared");
                    }

                    // Convert can_frame to CanMessage and enqueue
                    CanMessage canMsg;
                    canMsg.id = rxId;
                    canMsg.dlc = frame.can_dlc;
                    memcpy(canMsg.data, frame.data, 8);
                    canMsg.extended = (frame.can_id & CAN_EFF_FLAG) != 0;
                    canMsg.timestamp_ms = millis();

                    if (rxQueue != nullptr)
                    {
                        if (xQueueSend(rxQueue, &canMsg, 0) == pdTRUE)
                        {
                            driverStatus.total_rx_messages++;
                            driverStatus.last_activity_ms = millis();
                        }
                        else
                        {
                            // Queue full — drop oldest by receiving and re-sending
                            CanMessage discard;
                            xQueueReceive(rxQueue, &discard, 0);
                            xQueueSend(rxQueue, &canMsg, 0);
                            driverStatus.error_count++;
                        }
                    }
                }

                // Check for bus errors and auto-recover
                static unsigned long lastErrorLog = 0;
                uint8_t errorFlags = mcp2515->getErrorFlags();
                if (errorFlags != 0)
                {
                    // Clear RX overflow flags silently (normal under high load)
                    if (errorFlags & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR))
                    {
                        mcp2515->clearRXnOVRFlags();
                        mcp2515->clearInterrupts();
                    }
                    
                    // Only log critical bus errors once per 30s
                    if (errorFlags & (MCP2515::EFLG_TXBO | MCP2515::EFLG_RXEP | MCP2515::EFLG_TXEP))
                    {
                        // Suppress expected Error Passive logs when probing for a disconnected vehicle
                        bool isDisconnectedProbing = !SystemState::instance().getBatteryConnected() && 
                                                     !(errorFlags & MCP2515::EFLG_TXBO);

                        if (!isDisconnectedProbing && (millis() - lastErrorLog > 30000))
                        {
                            uint8_t tec = mcp2515->errorCountTX();
                            uint8_t rec = mcp2515->errorCountRX();
                            char eflgStr[100];
                            CAN_MCP2515::decodeErrorFlags(errorFlags, eflgStr, sizeof(eflgStr));
                            SafeSerial::printf("[CAN2] ⚠️  Bus error: 0x%02X%s (TEC: %d, REC: %d)\n", 
                                               errorFlags, eflgStr, tec, rec);
                            
                            // Automatically dump full diagnostics to help find the physical cause
                            CAN_MCP2515::readDiagnostics();
                            
                            lastErrorLog = millis();
                        }
                    }
                }
            }
            xSemaphoreGive(mcp2515RecoveryMutex);
        }

        prod::g_healthMonitor.feed();
        
        // Wait for interrupt notification with 100ms timeout (failsafe polling)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
}
