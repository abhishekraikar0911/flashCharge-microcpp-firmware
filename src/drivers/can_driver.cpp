#include "../../include/drivers/can_driver.h"
#include "../../include/header.h"

// New CAN driver buffer item (different from legacy RxBufItem)
struct CanRxItem
{
    twai_message_t frame;
    uint32_t timestamp_ms;
};

// Ring buffer for received messages
#define RX_BUFFER_SIZE 64
static CanRxItem rxBuffer[RX_BUFFER_SIZE];
static volatile uint16_t rxHead = 0;
static volatile uint16_t rxTail = 0;

// Legacy ring buffer for backward compatibility
static RxBufItem legacyRxBuffer[64];
static volatile uint16_t legacyRxHead = 0;
static volatile uint16_t legacyRxTail = 0;

// Driver status
static CanStatus driverStatus = {false, false, 0, 0, 0, 0};
static SemaphoreHandle_t canRecoveryMutex = nullptr;

// Legacy functions for backward compatibility
void pushFrame(const twai_message_t &msg)
{
    RxBufItem item;
    item.id = msg.identifier;
    item.dlc = msg.data_length_code;
    memcpy(item.data, msg.data, 8);
    item.ext = msg.extd;
    item.rtr = msg.rtr;

    legacyRxBuffer[legacyRxHead] = item;
    legacyRxHead = (legacyRxHead + 1) % 64;

    if (legacyRxHead == legacyRxTail)
    {
        legacyRxTail = (legacyRxTail + 1) % 64;
    }
}

bool popFrame(RxBufItem &out)
{
    if (legacyRxHead == legacyRxTail)
        return false;

    out = legacyRxBuffer[legacyRxTail];
    legacyRxTail = (legacyRxTail + 1) % 64;
    return true;
}

// Legacy twai_init function
void twai_init()
{
    CAN::init();
}

namespace CAN
{
    bool init()
    {
        Serial.println("[CAN] Initializing...");
        
        // Create recovery mutex on first init
        if (canRecoveryMutex == nullptr) {
            canRecoveryMutex = xSemaphoreCreateMutex();
        }
        
        // Take mutex to prevent RX task from accessing driver during init
        if (canRecoveryMutex && xSemaphoreTake(canRecoveryMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)21, (gpio_num_t)22, TWAI_MODE_NORMAL);
            twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
            twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

            esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
            if (err != ESP_OK)
            {
                Serial.printf("[CAN] Install failed: %d\n", err);
                xSemaphoreGive(canRecoveryMutex);
                return false;
            }

            err = twai_start();
            if (err != ESP_OK)
            {
                Serial.printf("[CAN] Start failed: %d\n", err);
                xSemaphoreGive(canRecoveryMutex);
                return false;
            }

            driverStatus.is_initialized = true;
            driverStatus.is_active = true;
            Serial.println("[CAN] Initialized successfully");
            xSemaphoreGive(canRecoveryMutex);
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

    bool receiveMessage(twai_message_t *frame, uint32_t *timestamp_ms)
    {
        if (rxHead == rxTail)
            return false;

        CanRxItem &item = rxBuffer[rxTail];
        *frame = item.frame;
        if (timestamp_ms)
            *timestamp_ms = item.timestamp_ms;

        rxTail = (rxTail + 1) % RX_BUFFER_SIZE;
        return true;
    }

    bool peekMessage(twai_message_t *frame)
    {
        if (rxHead == rxTail)
            return false;
        *frame = rxBuffer[rxTail].frame;
        return true;
    }

    CanStatus getStatus()
    {
        return driverStatus;
    }

    void flushRxBuffer()
    {
        rxHead = rxTail = 0;
    }

    uint8_t getRxBufferUsage()
    {
        uint16_t count = (rxHead >= rxTail) ? (rxHead - rxTail) : (RX_BUFFER_SIZE - rxTail + rxHead);
        return (count * 100) / RX_BUFFER_SIZE;
    }

    void resetStatistics()
    {
        driverStatus.total_rx_messages = 0;
        driverStatus.total_tx_messages = 0;
        driverStatus.error_count = 0;
    }

} // namespace CAN

// Task function for receiving CAN messages
void can_rx_task(void *arg)
{
    Serial.println("[CAN] RX task started");

    twai_message_t msg;
    while (true)
    {
        // Take mutex before accessing TWAI driver
        if (canRecoveryMutex && xSemaphoreTake(canRecoveryMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (driverStatus.is_initialized && driverStatus.is_active)
            {
                esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(100));
                if (err == ESP_OK)
                {
                    // FIX: Check buffer overflow before writing
                    uint16_t nextHead = (rxHead + 1) % RX_BUFFER_SIZE;
                    if (nextHead != rxTail)
                    {
                        rxBuffer[rxHead].frame = msg;
                        rxBuffer[rxHead].timestamp_ms = millis();
                        rxHead = nextHead;
                        driverStatus.total_rx_messages++;
                        driverStatus.last_activity_ms = millis();
                    }
                    else
                    {
                        // Buffer full - drop oldest message
                        rxTail = (rxTail + 1) % RX_BUFFER_SIZE;
                        driverStatus.error_count++;
                    }
                    
                    // Also push to legacy buffer for compatibility
                    pushFrame(msg);
                }
            }
            xSemaphoreGive(canRecoveryMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
