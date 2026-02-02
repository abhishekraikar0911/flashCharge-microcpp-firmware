#include "../../include/drivers/can_mcp2515_driver.h"
#include "../../include/header.h"
#include "../../include/config/hardware.h"
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

            // CRITICAL: Don't use reset() - it clears filters
            // Manual initialization instead
            MCP2515::ERROR result = mcp2515->setConfigMode();
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Config mode failed: %d\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // Set bitrate (CRITICAL: 8MHz crystal)
            result = mcp2515->setBitrate(CAN_250KBPS, MCP_8MHZ);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Bitrate config failed: %d (check 8MHz crystal)\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // CRITICAL: Configure hardware filters for BMS IDs only
            // RXF0 (RXB0): 0x1806E5F4 - BMS Request (Vmax, Imax)
            result = mcp2515->setFilter(MCP2515::RXF0, true, 0x1806E5F4UL);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Filter RXF0 failed: %d\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // RXF1 (RXB1): 0x160B8001 - Charging Ah Response
            result = mcp2515->setFilter(MCP2515::RXF1, true, 0x160B8001UL);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Filter RXF1 failed: %d\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // RXF2 (RXB0): 0x160D8001 - Discharging Ah Response
            result = mcp2515->setFilter(MCP2515::RXF2, true, 0x160D8001UL);
            if (result != MCP2515::ERROR_OK)
            {
                Serial.printf("[CAN2] ❌ Filter RXF2 failed: %d\n", result);
                xSemaphoreGive(mcp2515RecoveryMutex);
                return false;
            }

            // RXF3-5: Unused, set to impossible ID
            mcp2515->setFilter(MCP2515::RXF3, true, 0x1FFFFFFFUL);
            mcp2515->setFilter(MCP2515::RXF4, true, 0x1FFFFFFFUL);
            mcp2515->setFilter(MCP2515::RXF5, true, 0x1FFFFFFFUL);

            // Set masks to match exact IDs (all bits must match)
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

            Serial.println("[CAN2] ✅ Hardware filters configured (3 BMS IDs only)");

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
                        // Check buffer overflow
                        uint16_t nextHead = (rxHead + 1) % MCP2515_RX_BUFFER_SIZE;
                        if (nextHead != rxTail)
                        {
                            // Convert can_frame to CanMessage
                            rxBuffer[rxHead].id = frame.can_id & CAN_EFF_MASK;
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
                uint8_t errorFlags = mcp2515->getErrorFlags();
                if (errorFlags != 0)
                {
                    // Clear RX overflow flags immediately (0x40 = EFLG_RX1OVR)
                    if (errorFlags & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR))
                    {
                        mcp2515->clearRXnOVRFlags();
                        driverStatus.error_count++;
                    }
                    
                    // Only log critical errors (bus-off, not overflow or warnings)
                    if (errorFlags & (MCP2515::EFLG_TXBO | MCP2515::EFLG_RXEP))
                    {
                        Serial.printf("[CAN2] 🚨 Critical error: 0x%02X\n", errorFlags);
                        mcp2515->clearRXnOVRFlags();
                        mcp2515->clearInterrupts();
                        mcp2515->clearTXInterrupts();
                        vTaskDelay(pdMS_TO_TICKS(100));
                        
                        // Reinitialize
                        xSemaphoreGive(mcp2515RecoveryMutex);
                        CAN_MCP2515::deinit();
                        vTaskDelay(pdMS_TO_TICKS(100));
                        CAN_MCP2515::init();
                        continue;
                    }
                }
            }
            xSemaphoreGive(mcp2515RecoveryMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
