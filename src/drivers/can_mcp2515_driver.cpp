#include "../../include/drivers/can_mcp2515_driver.h"
#include "../../include/header.h"
#include "../../include/config/hardware.h"
#include "../../include/health_monitor.h"
#include "../../include/debug_logger.h"
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
            SPI.begin(CAN2_SCK_PIN, CAN2_MISO_PIN, CAN2_MOSI_PIN, CAN2_CS_PIN);

            // Create MCP2515 instance
            if (mcp2515 == nullptr)
            {
                mcp2515 = new MCP2515(CAN2_CS_PIN);
            }

            // CRITICAL: Hardware reset first (like test code)
            Serial.println("[CAN2] Resetting MCP2515...");
            MCP2515::ERROR result = mcp2515->reset();
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Reset failed: %d\n", result);
                Serial.println("[CAN2] ⚠️  Possible causes:");
                Serial.println("[CAN2]    - MCP2515 not connected (check wiring)");
                Serial.println("[CAN2]    - Wrong pins: CS=5 SCK=18 MISO=19 MOSI=23");
                Serial.println("[CAN2]    - Module not powered (VCC/GND)");
                Serial.println("[CAN2] 🔧 Continuing without BMS\n");
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }
            delay(10); // Give chip time to reset

            // Set bitrate (CRITICAL: 8MHz crystal)
            result = mcp2515->setBitrate(CAN_250KBPS, MCP_8MHZ);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Bitrate config failed: %d (check 8MHz crystal)\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // Configure hardware filters for BMS messages only
            // RXB0: Filter 0 = 0x1806E5F4 (BMS Vmax/Imax)
            result = mcp2515->setFilter(MCP2515::RXF0, true, 0x1806E5F4UL);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Filter0 failed: %d\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // RXB0: Filter 1 = 0x18904001 (SOC response)
            result = mcp2515->setFilter(MCP2515::RXF1, true, 0x18904001UL);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Filter1 failed: %d\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // RXB1: Filter 2 = 0x18904001 (SOC response - duplicate for redundancy)
            result = mcp2515->setFilter(MCP2515::RXF2, true, 0x18904001UL);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Filter2 failed: %d\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // Set masks to exact match (only 3 BMS IDs pass)
            result = mcp2515->setFilterMask(MCP2515::MASK0, true, 0x1FFFFFFFUL);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Mask0 failed: %d\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            result = mcp2515->setFilterMask(MCP2515::MASK1, true, 0x1FFFFFFFUL);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Mask1 failed: %d\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            Serial.println("[CAN2] ✅ Hardware filters: ONLY 2 BMS IDs (0x1806E5F4, 0x18904001)");

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

    bool readDiagnostics()
    {
        if (!mcp2515 || !driverStatus.is_active)
        {
            Serial.println("[CAN2] ❌ Driver not initialized");
            return false;
        }

        Serial.println("\n[CAN2] === MCP2515 Diagnostics ===");
        
        // Read error flags
        uint8_t eflg = mcp2515->getErrorFlags();
        Serial.printf("EFLG: 0x%02X\n", eflg);
        
        // Read TEC/REC (transmit/receive error counters)
        uint8_t tec = mcp2515->errorCountTX();
        uint8_t rec = mcp2515->errorCountRX();
        Serial.printf("TEC: %d, REC: %d\n", tec, rec);
        
        // Read interrupt flags
        uint8_t canintf = mcp2515->getInterrupts();
        Serial.printf("CANINTF: 0x%02X\n", canintf);
        
        // Check if SPI communication is working
        bool spiOk = (eflg != 0xFF && canintf != 0xFF);
        Serial.printf("SPI Communication: %s\n", spiOk ? "✅ OK" : "❌ FAILED");
        
        // Status
        uint8_t status = mcp2515->getStatus();
        Serial.printf("Status: 0x%02X\n", status);
        
        Serial.println("================================\n");
        
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
                if (messageAvailable || !digitalRead(CAN2_INT_PIN))
                {
                    messageAvailable = false;

                    MCP2515::ERROR result = mcp2515->readMessage(&frame);
                    if (result == MCP2515::ERROR_OK)
                    {
                        // DEBUG: Log received CAN ID
                        uint32_t rxId = frame.can_id & CAN_EFF_MASK;
                        LOG_BMS("RX: ID=0x%08X DLC=%d", rxId, frame.can_dlc);

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
                            Serial.printf("[CAN2] ⚠️  Bus error: 0x%02X (check BMS wiring/termination)\n", errorFlags);
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
