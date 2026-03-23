#include "../../include/drivers/can_twai_driver.h"
#include "../../include/header.h"
#include "../../include/config/hardware.h"
#include "../../include/health_monitor.h"

// FreeRTOS Queue for received messages (replaces manual ring buffer)
static QueueHandle_t rxQueue = nullptr;

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

        // Create RX queue (thread-safe, replaces volatile ring buffer)
        if (rxQueue == nullptr)
        {
            rxQueue = xQueueCreate(CAN_RX_QUEUE_SIZE, sizeof(CanMessage));
            if (rxQueue == nullptr)
            {
                Serial.println("[CAN1] ❌ Failed to create RX queue!");
                return false;
            }
        }

        if (twaiRecoveryMutex && xSemaphoreTake(twaiRecoveryMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN1_TX_PIN, CAN1_RX_PIN, TWAI_MODE_NORMAL);
            g_config.rx_queue_len = 32;  // Increase from default 5 to prevent missed frames during boot
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
        if (twaiRecoveryMutex == nullptr)
        {
            twaiRecoveryMutex = xSemaphoreCreateMutex();
        }

        if (twaiRecoveryMutex && xSemaphoreTake(twaiRecoveryMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            twai_stop();
            twai_driver_uninstall();
            driverStatus.is_initialized = false;
            driverStatus.is_active = false;
            xSemaphoreGive(twaiRecoveryMutex);
            return true;
        }
        return false;
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

    CanTwaiStatus getStatus()
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
                    // Convert twai_message_t to CanMessage and enqueue
                    CanMessage canMsg;
                    canMsg.id = msg.identifier;
                    canMsg.dlc = msg.data_length_code;
                    memcpy(canMsg.data, msg.data, 8);
                    canMsg.extended = (msg.extd != 0);
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
            }
            xSemaphoreGive(twaiRecoveryMutex);
        }

        prod::g_healthMonitor.feed();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
