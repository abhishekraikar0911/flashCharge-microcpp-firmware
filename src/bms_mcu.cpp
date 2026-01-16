#include "header.h"
#include <Arduino.h>
#include <math.h>

// SOC related
float batteryAh = 0.0f;
float batterySoc = 0.0f; // 0–100 %

// ====== Build status flags for 0x18FF50E5 ======
static uint8_t buildStatusFlags()
{
    uint8_t flags = 0;
    // Bit0: Hardware failure (optional future use)
    // if (hardwareFaultDetected) flags |= 0x01;
    // Bit1: Over temperature
    if (chargerTemp > 70.0f)
        flags |= 0x02;
    // Bit3: Battery not connected / reversed
    if (!batteryConnected)
        flags |= 0x08;
    // Bit4: Communication timeout (no BMS request in >5s)
    if ((millis() - lastBMS) > 5000)
        flags |= 0x10;
    return flags;
}

void handleBMSMessage(const twai_message_t &msg)
{
    if (!msg.extd)
        return;
    if ((msg.identifier & 0x1FFFFFFFUL) != (ID_BMS_REQUEST & 0x1FFFFFFFUL))
        return;

    // Serial.println("BMS message received");

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        batteryConnected = true;
        lastBMS = millis();

        const uint8_t dlc = msg.data_length_code;
        memcpy(lastBMSData, msg.data, dlc > 8 ? 8 : dlc);

        const uint16_t vmax_raw = (uint16_t(msg.data[0]) << 8) | msg.data[1];
        const uint16_t imax_raw = (uint16_t(msg.data[2]) << 8) | msg.data[3];
        BMS_Vmax = vmax_raw / 10.0f;
        BMS_Imax = imax_raw / 10.0f;

        chargingswitch = (msg.data[4] == 0x00);
        heating = (msg.data[5] == 0x01);

        cachedRawV = (uint32_t)lroundf(BMS_Vmax * 1024.0f);
        cachedRawI = (uint32_t)lroundf(BMS_Imax * 30.5f);

        if (BMS_Vmax > 56.0f && BMS_Vmax < 85.5f)
        {
            batteryConnected = true;
            lastBMS = millis();
        }
        xSemaphoreGive(dataMutex);
    }
}

void sendChargerFeedback()
{
    twai_message_t tx = {};
    tx.identifier = ID_HEARTBEAT & 0x1FFFFFFFUL;
    tx.extd = 1;
    tx.rtr = 0;
    tx.data_length_code = 8;

    int vraw_i = (int)lroundf(chargerVolt * 10.0f);
    int iraw_i = (int)lroundf(chargerCurr * 10.0f);
    if (vraw_i < 0)
        vraw_i = 0;
    if (vraw_i > 0xFFFF)
        vraw_i = 0xFFFF;
    if (iraw_i < 0)
        iraw_i = 0;
    if (iraw_i > 0xFFFF)
        iraw_i = 0xFFFF;

    const uint16_t vraw = (uint16_t)vraw_i;
    const uint16_t iraw = (uint16_t)iraw_i;

    tx.data[0] = (vraw >> 8) & 0xFF;
    tx.data[1] = vraw & 0xFF;
    tx.data[2] = (iraw >> 8) & 0xFF;
    tx.data[3] = iraw & 0xFF;
    tx.data[4] = buildStatusFlags();
    tx.data[5] = 0x00;
    tx.data[6] = 0x00;
    tx.data[7] = 0x00;

    esp_err_t res = twai_transmit(&tx, pdMS_TO_TICKS(20));
    // printChargerFeedback(chargerVolt, chargerCurr, tx.data[4], res);
}

void requestSOCFromBMS()
{
    twai_message_t tx = {};
    tx.identifier = ID_SOC_REQUEST & 0x1FFFFFFFUL;
    tx.extd = 1;
    tx.rtr = 0;
    tx.data_length_code = 8;

    memset(tx.data, 0x00, 8);

    esp_err_t res = twai_transmit(&tx, pdMS_TO_TICKS(20));
    // Optional debug
    // Serial.printf("SOC request sent, res=%d\n", res);
}

void handleSOCMessage(const twai_message_t &msg)
{
    if (!msg.extd)
        return;

    if ((msg.identifier & 0x1FFFFFFFUL) != (ID_SOC_RESPONSE & 0x1FFFFFFFUL))
        return;

    if (msg.data_length_code < 4)
        return;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        // Bytes 2 & 3 contain Ah value
        uint16_t ah_raw = (uint16_t(msg.data[2]) << 8) | msg.data[3];

        // Convert to Ah
        batteryAh = ah_raw * 0.001f;

        // Clamp Ah to 0–30 range
        if (batteryAh < 0.0f)
            batteryAh = 0.0f;
        if (batteryAh > 30.0f)
            batteryAh = 30.0f;

        // Calculate SOC
        batterySoc = (batteryAh / 30.0f) * 100.0f;

        // Clamp SOC
        if (batterySoc < 0.0f)
            batterySoc = 0.0f;
        if (batterySoc > 100.0f)
            batterySoc = 100.0f;

        // Update global socPercent
        socPercent = batterySoc;

        // Set battery connected when SOC is available
        if (socPercent > 0.0f)
        {
            batteryConnected = true;
        }

        xSemaphoreGive(dataMutex);
    }
}