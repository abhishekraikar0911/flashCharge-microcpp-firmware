#include "../../include/drivers/can_mcp2515_driver.h"
#include "../../include/header.h"
#include "../../include/config/hardware.h"
#include "../../include/health_monitor.h"
#include "../../include/debug_logger.h"
#include "../../include/utils/can_status_logger.h"
#include <SPI.h>

// MCP2515 instance
static MCP2515 *mcp2515 = nullptr;

// Ring buffer for received messages
#define MCP2515_RX_BUFFER_SIZE 64
static CanMessage rxBuffer[MCP2515_RX_BUFFER_SIZE];
static volatile uint16_t rxHead = 0;
static volatile uint16_t rxTail = 0;

// Driver status
static CanMcp2515Status driverStatus = {false, false, 0, 0, 0, 0};
static SemaphoreHandle_t mcp2515RecoveryMutex = nullptr;

// ISR flag
static volatile bool messageAvailable = false;

// ISR handler
void IRAM_ATTR mcp2515_isr()
{
    messageAvailable = true;
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

        if (mcp2515RecoveryMutex && xSemaphoreTake(mcp2515RecoveryMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            // Initialize SPI
            Serial.println("[CAN2] Starting SPI bus...");
            
            // Pulse CS pin high then low to reset MCP2515 SPI interface
            pinMode(CAN2_CS_PIN, OUTPUT);
            digitalWrite(CAN2_CS_PIN, HIGH);
            delay(10);
            digitalWrite(CAN2_CS_PIN, LOW);
            delay(10);
            digitalWrite(CAN2_CS_PIN, HIGH);
            delay(50);

            SPI.end(); // Ensure clean state
            delay(20);
            SPI.begin(CAN2_SCK_PIN, CAN2_MISO_PIN, CAN2_MOSI_PIN, -1); 
            delay(50);
            
            // EXTREME DIAGNOSTIC: Manual SPI read (Optimized to 10MHz)
            Serial.println("[CAN2] 🧪 Manual SPI test (Read CANSTAT 0x0E at 10MHz)...");
            SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
            digitalWrite(CAN2_CS_PIN, LOW);
            SPI.transfer(0x03); // READ command
            SPI.transfer(0x0E); // CANSTAT address
            uint8_t stat = SPI.transfer(0x00);
            digitalWrite(CAN2_CS_PIN, HIGH);
            SPI.endTransaction();
            Serial.printf("[CAN2] 🧪 Manual SPI result: 0x%02X (Expected default ~0x80 or 0x00)\n", stat);

            // Create MCP2515 instance
            if (mcp2515 == nullptr)
            {
                mcp2515 = new MCP2515(CAN2_CS_PIN);
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
                Serial.println("[CAN2] 🔍 Checking SPI health...");
                uint8_t flags = mcp2515->getErrorFlags();
                if (flags == 0xFF) {
                    Serial.println("[CAN2] ❌ SPI Error: Received 0xFF (likely MISO pin HIGH issue)");
                } else if (flags == 0x00 && stat == 0x00) {
                    Serial.println("[CAN2] ❌ SPI Error: Received 0x00 consistently (MISO pin LOW or no power)");
                } else {
                    Serial.printf("[CAN2] 📊 Register check: 0x%02X, Manual: 0x%02X\n", flags, stat);
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

            // Setup interrupt
            pinMode(CAN2_INT_PIN, INPUT_PULLUP);
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
            detachInterrupt(digitalPinToInterrupt(CAN2_INT_PIN));
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

        MCP2515::ERROR result = mcp2515->sendMessage(&frame);
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
        if (rxHead == rxTail)
            return false;

        *msg = rxBuffer[rxTail];
        rxTail = (rxTail + 1) % MCP2515_RX_BUFFER_SIZE;
        return true;
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
        rxHead = rxTail = 0;
    }

    uint8_t getRxBufferUsage()
    {
        uint16_t count = (rxHead >= rxTail) ? (rxHead - rxTail) : (MCP2515_RX_BUFFER_SIZE - rxTail + rxHead);
        return (count * 100) / MCP2515_RX_BUFFER_SIZE;
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

    static void decodeErrorFlags(uint8_t flags)
    {
        if (flags == 0) return;
        
        Serial.print(" [");
        bool first = true;
        if (flags & MCP2515::EFLG_RX1OVR) { Serial.print("RX1OVR"); first = false; }
        if (flags & MCP2515::EFLG_RX0OVR) { if(!first) Serial.print("|"); Serial.print("RX0OVR"); first = false; }
        if (flags & MCP2515::EFLG_TXBO)   { if(!first) Serial.print("|"); Serial.print("Bus-Off"); first = false; }
        if (flags & MCP2515::EFLG_TXEP)   { if(!first) Serial.print("|"); Serial.print("TX-Passive"); first = false; }
        if (flags & MCP2515::EFLG_RXEP)   { if(!first) Serial.print("|"); Serial.print("RX-Passive"); first = false; }
        if (flags & MCP2515::EFLG_TXWAR)  { if(!first) Serial.print("|"); Serial.print("TX-Warn"); first = false; }
        if (flags & MCP2515::EFLG_RXWAR)  { if(!first) Serial.print("|"); Serial.print("RX-Warn"); first = false; }
        // Note: EFLG_EWAR is often missing in older lib versions, using bit 0 directly if needed
        if (flags & 0x01) { if(!first) Serial.print("|"); Serial.print("Error-Warn"); first = false; }
        Serial.print("]");
    }

    bool readDiagnostics()
    {
        if (!mcp2515 || !driverStatus.is_active)
        {
            Serial.println("[CAN2] ❌ Driver not initialized");
            return false;
        }

        // Read error flags and counters
        uint8_t eflg = mcp2515->getErrorFlags();
        uint8_t tec = mcp2515->errorCountTX();
        uint8_t rec = mcp2515->errorCountRX();
        
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

        Serial.printf("[CAN2] EFLG (0x%02X):", eflg);
        decodeErrorFlags(eflg);
        Serial.println();
        
        // Read interrupt flags
        uint8_t canintf = mcp2515->getInterrupts();
        Serial.printf("CANINTF: 0x%02X\n", canintf);
        
        // Check if SPI communication is working
        bool spiOk = (eflg != 0xFF && canintf != 0xFF);
        Serial.printf("SPI Communication: %s\n", spiOk ? "✅ OK" : "❌ FAILED");
        
        // Status
        uint8_t status = mcp2515->getStatus();
        Serial.printf("MCP Status: 0x%02X\n", status);
        
        Serial.println("================================");
        
        return spiOk;
    }

} // namespace CAN_MCP2515

// CAN2 RX Task (BMS messages)
void can2_rx_task(void *arg)
{
    Serial.println("[CAN2] RX task started");

    struct can_frame frame;

    while (true)
    {
        // Take mutex before accessing MCP2515
        if (mcp2515RecoveryMutex && xSemaphoreTake(mcp2515RecoveryMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (driverStatus.is_initialized && driverStatus.is_active)
            {
                // Check interrupt flag
                // Active LOW: messageAvailable flag OR digitalRead directly
                while (messageAvailable || !digitalRead(CAN2_INT_PIN))
                {
                    messageAvailable = false;

                    MCP2515::ERROR result = mcp2515->readMessage(&frame);
                    if (result != MCP2515::ERROR_OK)
                    {
                        break; // No more messages OR error
                    }

                    // SUCCESS: Received a message
                    uint32_t rxId = frame.can_id & CAN_EFF_MASK;
                    
                    // Strict software filter: Ignore anything besides the two required IDs
                    if (rxId != 0x1806E5F4UL && rxId != 0x18904001UL)
                    {
                        continue; 
                    }

                    // Check buffer overflow
                    uint16_t nextHead = (rxHead + 1) % MCP2515_RX_BUFFER_SIZE;
                    if (nextHead != rxTail)
                    {
                        // Convert can_frame to CanMessage
                        rxBuffer[rxHead].id = rxId;
                        rxBuffer[rxHead].dlc = frame.can_dlc;
                        memcpy(rxBuffer[rxHead].data, frame.data, 8);
                        rxBuffer[rxHead].extended = (frame.can_id & CAN_EFF_FLAG) != 0;
                        rxBuffer[rxHead].timestamp_ms = millis();

                        rxHead = nextHead;
                        driverStatus.total_rx_messages++;
                        driverStatus.last_activity_ms = millis();
                    }
                    else
                    {
                        // Buffer full - drop oldest
                        rxTail = (rxTail + 1) % MCP2515_RX_BUFFER_SIZE;
                        driverStatus.error_count++;
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
                        if (millis() - lastErrorLog > 30000)
                        {
                            uint8_t tec = mcp2515->errorCountTX();
                            uint8_t rec = mcp2515->errorCountRX();
                            Serial.printf("[CAN2] ⚠️  Bus error: 0x%02X", errorFlags);
                            CAN_MCP2515::decodeErrorFlags(errorFlags);
                            Serial.printf(" (TEC: %d, REC: %d)\n", tec, rec);
                            
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
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
