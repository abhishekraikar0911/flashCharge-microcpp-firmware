#include "../../include/drivers/can_twai_driver.h"
#include "../../include/header.h"
#include "../../include/config/hardware.h"

// Ring buffer for received messages (unified format)
#define TWAI_RX_BUFFER_SIZE 64
static CanMessage rxBuffer[TWAI_RX_BUFFER_SIZE];
static volatile uint16_t rxHead = 0;
static volatile uint16_t rxTail = 0;

// Driver status
static CanTwaiStatus driverStatus = {false, false, 0, 0, 0, 0};
static SemaphoreHandle_t twaiRecoveryMutex = nullptr;

namespace CAN_TWAI
{
    bool init()
    {
        Serial.println("[CAN1] Initializing TWAI...");

        // Create recovery mutex
        if (twaiRecoveryMutex == nullptr)
        {
            twaiRecoveryMutex = xSemaphoreCreateMutex();
        }

        if (twaiRecoveryMutex && xSemaphoreTake(twaiRecoveryMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN1_TX_PIN, CAN1_RX_PIN, TWAI_MODE_NORMAL);
            twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
            twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

            esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
            if (err != ESP_OK)
            {
                Serial.printf("[CAN1] ❌ Install failed: %d\n", err);
                xSemaphoreGive(twaiRecoveryMutex);
                return false;
            }

            err = twai_start();
            if (err != ESP_OK)
            {
                Serial.printf("[CAN1] ❌ Start failed: %d\n", err);
                xSemaphoreGive(twaiRecoveryMutex);
                return false;
            }

            driverStatus.is_initialized = true;
            driverStatus.is_active = true;
            Serial.println("[CAN1] ✅ TWAI initialized successfully");

            xSemaphoreGive(twaiRecoveryMutex);
            return true;
        }
        return false;
    }

    bool deinit()
    {
        twai_stop();
        twai_driver_uninstall();
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
        twai_message_t msg = {};
        msg.identifier = id;
        msg.data_length_code = length;
        msg.extd = is_extended ? 1 : 0;
        memcpy(msg.data, data, length);

        esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(100));
        if (err == ESP_OK)
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
        rxTail = (rxTail + 1) % TWAI_RX_BUFFER_SIZE;
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

    CanTwaiStatus getStatus()
    {
        return driverStatus;
    }

    void flushRxBuffer()
    {
        rxHead = rxTail = 0;
    }

    uint8_t getRxBufferUsage()
    {
        uint16_t count = (rxHead >= rxTail) ? (rxHead - rxTail) : (TWAI_RX_BUFFER_SIZE - rxTail + rxHead);
        return (count * 100) / TWAI_RX_BUFFER_SIZE;
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

} // namespace CAN_TWAI

// CAN1 RX Task (Charger messages)
void can1_rx_task(void *arg)
{
    Serial.println("[CAN1] RX task started");

    twai_message_t msg;

    while (true)
    {
        // Take mutex before accessing TWAI driver
        if (twaiRecoveryMutex && xSemaphoreTake(twaiRecoveryMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (driverStatus.is_initialized && driverStatus.is_active)
            {
                esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(100));
                if (err == ESP_OK)
                {
                    // Check buffer overflow
                    uint16_t nextHead = (rxHead + 1) % TWAI_RX_BUFFER_SIZE;
                    if (nextHead != rxTail)
                    {
                        // Convert twai_message_t to CanMessage
                        rxBuffer[rxHead].id = msg.identifier;
                        rxBuffer[rxHead].dlc = msg.data_length_code;
                        memcpy(rxBuffer[rxHead].data, msg.data, 8);
                        rxBuffer[rxHead].extended = (msg.extd != 0);
                        rxBuffer[rxHead].timestamp_ms = millis();

                        rxHead = nextHead;
                        driverStatus.total_rx_messages++;
                        driverStatus.last_activity_ms = millis();
                    }
                    else
                    {
                        // Buffer full - drop oldest
                        rxTail = (rxTail + 1) % TWAI_RX_BUFFER_SIZE;
                        driverStatus.error_count++;
                    }
                }
            }
            xSemaphoreGive(twaiRecoveryMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
