#include "hal/esp32/Esp32Can.h"
#include <Arduino.h>

Esp32Can::Esp32Can(int txPin, int rxPin, int rxQueue, int txQueue)
    : txPin(txPin), rxPin(rxPin), rxQueueSize(rxQueue), txQueueSize(txQueue), 
      isInit(false), currentBaud(250000), mutex(nullptr) {
    mutex = xSemaphoreCreateMutex();
}

bool Esp32Can::init(uint32_t baudrate) {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    
    if (isInit) {
        xSemaphoreGive(mutex);
        return true;
    }

    currentBaud = baudrate;

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)txPin, 
        (gpio_num_t)rxPin, 
        TWAI_MODE_NORMAL
    );
    g_config.rx_queue_len = rxQueueSize;
    g_config.tx_queue_len = txQueueSize;
    
    twai_timing_config_t t_config = getTimingConfig(baudrate);
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        xSemaphoreGive(mutex);
        return false;
    }

    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        xSemaphoreGive(mutex);
        return false;
    }

    isInit = true;
    xSemaphoreGive(mutex);
    return true;
}

bool Esp32Can::send(const CanFrame& frame) {
    if (!isInit) return false;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;

    twai_message_t msg = {};
    msg.identifier = frame.id;
    msg.extd = frame.extended ? 1 : 0;
    msg.data_length_code = frame.len > 8 ? 8 : frame.len;
    for (int i = 0; i < msg.data_length_code; i++) {
        msg.data[i] = frame.data[i];
    }

    esp_err_t err = twai_transmit(&msg, 0); // Non-blocking transmit check

    if (err == ESP_ERR_INVALID_STATE) {
        twai_status_info_t status;
        twai_get_status_info(&status);
        if (status.state == TWAI_STATE_BUS_OFF) {
            Serial.println("[HAL_CAN1] ⚠️  Bus-Off detected — Triggering reset...");
            xSemaphoreGive(mutex); // Give it before calling reset() which takes it
            reset();
            return false;
        }
    }

    static uint32_t lastTxLog = 0;
    uint32_t now = millis();
    if (err != ESP_OK && now - lastTxLog > 5000) {
        lastTxLog = now;
        Serial.printf("[HAL_CAN1] TX FAIL id=0x%08lX err=0x%X\n", (long unsigned int)frame.id, err);
    }

    xSemaphoreGive(mutex);
    return err == ESP_OK;
}

bool Esp32Can::receive(CanFrame& frame) {
    if (!isInit) return false;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;

    twai_message_t msg;
    esp_err_t err = twai_receive(&msg, 0);
    
    if (err == ESP_OK) {
        frame.id = msg.identifier;
        frame.len = msg.data_length_code;
        frame.extended = (msg.extd != 0);
        frame.timestamp_ms = millis();
        for (int i = 0; i < frame.len; i++) {
            frame.data[i] = msg.data[i];
        }

        // Success logs removed to prevent console spam

        xSemaphoreGive(mutex);
        return true;
    } else if (err != ESP_ERR_TIMEOUT) {
        static uint32_t lastErrLog = 0;
        uint32_t now = millis();
        if (now - lastErrLog > 5000) {
            lastErrLog = now;
            twai_status_info_t status;
            twai_get_status_info(&status);
            Serial.printf("[HAL_CAN1] RX ERR: 0x%X | State: %d | TX_Err: %u | RX_Err: %u\n", 
                          err, status.state, status.tx_error_counter, status.rx_error_counter);
            
            if (status.state == TWAI_STATE_BUS_OFF) {
                Serial.println("[HAL_CAN1] 🚨 Bus-Off in RX loop — Triggering recovery...");
                xSemaphoreGive(mutex);
                reset();
                return false;
            }
        }
    }

    xSemaphoreGive(mutex);
    return false;
}

bool Esp32Can::isHealthy() {
    if (!isInit) return false;
    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK) {
        return status.state == TWAI_STATE_RUNNING;
    }
    return false;
}

void Esp32Can::reset() {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
    
    Serial.println("[HAL_CAN1] 🔄 Reinstalling TWAI Driver...");
    twai_stop();
    twai_driver_uninstall();
    
    isInit = false;
    xSemaphoreGive(mutex); // Release for init()
    init(currentBaud);
}

twai_timing_config_t Esp32Can::getTimingConfig(uint32_t baudrate) {
    switch (baudrate) {
        case 125000: return TWAI_TIMING_CONFIG_125KBITS();
        case 250000: return TWAI_TIMING_CONFIG_250KBITS();
        case 500000: return TWAI_TIMING_CONFIG_500KBITS();
        case 1000000: return TWAI_TIMING_CONFIG_1MBITS();
        default:     return TWAI_TIMING_CONFIG_250KBITS();
    }
}
