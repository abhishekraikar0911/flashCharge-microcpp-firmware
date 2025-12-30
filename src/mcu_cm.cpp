#include "header.h"
#include <Arduino.h>
#include <string.h>

// Toggle OCPP telemetry here (set to 1 to enable, 0 to disable)
#define ENABLE_OCPP_TELEMETRY 1
#if ENABLE_OCPP_TELEMETRY
#include "ocpp/ocpp_client.h"
#endif

// Big-endian float helper (assumes payload stores MSB first)
static inline float beFloat(const uint8_t *b)
{
    uint8_t tmp[4] = {b[3], b[2], b[1], b[0]};
    float f;
    memcpy(&f, tmp, sizeof(f));
    return f;
}

// Groups
Group groups[] = {
    // Ctrl/limits group: status(0x32), Vmax(0x00), Imax(0x03)
    {0x068181FEUL, 0x0681817EUL, {0x32, 0x00, 0x03}, 3, 300, 0, 0},
    // Telemetry group: batt V(0x84), curr(0x82), temp(0x80), metric79, metric83
    {0x068182FEUL, 0x0681827EUL, {0x84, 0x82, 0x79, 0x80, 0x83}, 5, 200, 0, 0}};
const uint8_t NUM_GROUPS = sizeof(groups) / sizeof(Group);

// Forward decoders
static void decode_0681817E(const twai_message_t &msg);
static void decode_0681827E(const twai_message_t &msg);
static void decode_00433F01(const twai_message_t &msg);
static void decode_00473F01(const twai_message_t &msg);
static void decode_18FF50E5(const twai_message_t &msg);

void handleChargerMessage(const twai_message_t &msg)
{
    const uint8_t dlc = msg.data_length_code;
    memcpy(lastData, msg.data, dlc > 8 ? 8 : dlc);

    const uint32_t id = msg.extd ? (msg.identifier & 0x1FFFFFFFUL)
                                 : (msg.identifier & 0x7FF);

    switch (id)
    {
    case (ID_CTRL_RESP & 0x1FFFFFFFUL):
        decode_0681817E(msg);
        break;
    case (ID_TELEM_RESP & 0x1FFFFFFFUL):
        decode_0681827E(msg);
        break;
    case (ID_TERM_POWER & 0x1FFFFFFFUL):
        decode_00433F01(msg);
        break;
    case (ID_TERM_STATUS & 0x1FFFFFFFUL):
        decode_00473F01(msg);
        break;
    case (ID_HEARTBEAT & 0x1FFFFFFFUL):
        decode_18FF50E5(msg);
        break;
    default:
        break;
    }
}
// --- DECODERS ---

static void decode_0681817E(const twai_message_t &msg)
{
    const uint8_t dlc = msg.data_length_code;
    if (dlc < 8)
        return;
    const uint8_t func = msg.data[1];
    const uint32_t raw = (uint32_t(msg.data[4]) << 24) |
                         (uint32_t(msg.data[5]) << 16) |
                         (uint32_t(msg.data[6]) << 8) |
                         uint32_t(msg.data[7]);

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        if (func == 0x32)
        {
            memcpy(lastStatusData, msg.data, dlc > 8 ? 8 : dlc);
            chargerStatus = (msg.data[3] == 0x00) ? "ON" : "OFF";
        }
        else if (func == 0x00)
        {
            memcpy(lastVmaxData, msg.data, dlc > 8 ? 8 : dlc);
            Charger_Vmax = raw / 1024.0f;
        }
        else if (func == 0x03)
        {
            memcpy(lastImaxData, msg.data, dlc > 8 ? 8 : dlc);
            Charger_Imax = raw / 30.5f;
        }
        if (Charger_Vmax > 56.0f && Charger_Vmax < 85.5f)
        {
            batteryConnected = true;
            gunPhysicallyConnected = true;
            lastBMS = millis();
        }
        xSemaphoreGive(dataMutex);
    }
}

static void decode_0681827E(const twai_message_t &msg)
{
    const uint8_t dlc = msg.data_length_code;
    if (dlc < 8)
        return;
    const uint8_t func = msg.data[1];

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        if (func == 0x84)
        {
            memcpy(lastBattData, msg.data, dlc > 8 ? 8 : dlc);
            const uint32_t raw = (uint32_t(msg.data[4]) << 24) |
                                 (uint32_t(msg.data[5]) << 16) |
                                 (uint32_t(msg.data[6]) << 8) |
                                 uint32_t(msg.data[7]);
            chargerVolt = raw / 1024.0f;

            if (chargerVolt > 56.0f && chargerVolt < 84.5f)
            {
                batteryConnected = true;
                gunPhysicallyConnected = true;
            }
        }
        else if (func == 0x82)
        {
            memcpy(lastCurrData, msg.data, dlc > 8 ? 8 : dlc);
            chargerCurr = (float(((uint16_t)msg.data[6] << 8) | (uint16_t)msg.data[7])) / 1024.0f;
        }
        else if (func == 0x80)
        {
            memcpy(lastTempData, msg.data, dlc > 8 ? 8 : dlc);
            chargerTemp = (float(((uint16_t)msg.data[6] << 8) | (uint16_t)msg.data[7])) * 0.001f;
        }
        else if (func == 0x79)
        {
            memcpy(lastVoltData, msg.data, dlc > 8 ? 8 : dlc);
            metric79_raw = ((uint16_t)msg.data[6] << 8) | msg.data[7];
            metric79_scaled = metric79_raw * 1.0f;
        }
        else if (func == 0x83)
        {
            memcpy(lastVoltData, msg.data, dlc > 8 ? 8 : dlc);
            metric83_scaled = beFloat(&msg.data[4]);
        }
        xSemaphoreGive(dataMutex);
    }
}

static void decode_00433F01(const twai_message_t &msg)
{
    const uint8_t dlc = msg.data_length_code;
    if (dlc < 8)
        return;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        memcpy(lastTermData1, msg.data, dlc > 8 ? 8 : dlc);
        terminalVolt = beFloat(&msg.data[0]);
        terminalCurr = beFloat(&msg.data[4]);
        terminalchargerPower = terminalVolt * terminalCurr;

        if (terminalVolt > 56.0f && terminalVolt < 85.5f)
        {
            batteryConnected = true;
            gunPhysicallyConnected = true;
            lastBMS = millis();
        }

        xSemaphoreGive(dataMutex);
    }
}

static void decode_00473F01(const twai_message_t &msg)
{
    const uint8_t dlc = msg.data_length_code;
    if (dlc < 8)
        return;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        memcpy(lastTermData2, msg.data, dlc > 8 ? 8 : dlc);
        const uint8_t b6 = msg.data[6], b7 = msg.data[7];
        if (b6 == 0x03 && b7 == 0x01)
            terminalStatus = "NOT CHARGING";
        else if (b6 == 0x03 && b7 == 0x02)
            terminalStatus = "CHARGING";
        else
            terminalStatus = "UNKNOWN";

        xSemaphoreGive(dataMutex);
    }
}

static void decode_18FF50E5(const twai_message_t &msg)
{
    const uint8_t dlc = msg.data_length_code;
    if (dlc < 8)
        return;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        memcpy(lastHData, msg.data, dlc > 8 ? 8 : dlc);
        const bool alive = (msg.data[4] & 0x08) != 0; // bit 3 alive
        terminalchargerStatus = alive ? "HEARTBEAT ALIVE" : "NO HEARTBEAT";
        lastHeartbeat = millis();
        xSemaphoreGive(dataMutex);
    }
}

void sendGroupRequest(Group &g)
{
    const unsigned long now = millis();
    if ((unsigned long)(now - g.lastReq) < g.period)
        return;
    g.lastReq = now;

    if (g.funcIndex >= g.funcCount)
        g.funcIndex = 0;
    const uint8_t func = g.funcs[g.funcIndex];

    twai_message_t tx = {};
    tx.identifier = g.reqId & 0x1FFFFFFFUL;
    tx.extd = 1;
    tx.rtr = 0;
    tx.data_length_code = 8;

    memset(tx.data, 0, 8);
    tx.data[0] = 0x01; // command class
    tx.data[1] = func;

    if (func == 0x32)
    {
        bool enabled = false;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            enabled = chargingEnabled && gunPhysicallyConnected && batteryConnected;
            xSemaphoreGive(dataMutex);
        }
        tx.data[2] = 0x00;
        tx.data[3] = enabled ? 0x00 : 0x01;
    }
    else if (func == 0x00 || func == 0x03)
    {
        // Always broadcast Vmax/Imax, even if charging disabled
        uint32_t raw = (func == 0x00) ? cachedRawV : cachedRawI;
        tx.data[4] = (raw >> 24) & 0xFF;
        tx.data[5] = (raw >> 16) & 0xFF;
        tx.data[6] = (raw >> 8) & 0xFF;
        tx.data[7] = raw & 0xFF;
    }

    (void)twai_transmit(&tx, pdMS_TO_TICKS(20));
    g.funcIndex = (g.funcIndex + 1) % g.funcCount;
}

// --- Main comms task ---
void chargerCommTask(void *arg)
{
    static unsigned long lastFeedback = 0;
    static unsigned long lastEnergyTick = 0;

    while (true)
    {
        // Send all group requests
        for (uint8_t i = 0; i < NUM_GROUPS; i++)
        {
            sendGroupRequest(groups[i]);
        }
        sendChargerFeedback();

        // 4. Drain RX queue and dispatch
        RxBufItem item;
        while (popFrame(item))
        {
            twai_message_t msg = {};
            msg.identifier = item.id;
            msg.extd = item.ext ? 1 : 0;
            msg.rtr = item.rtr ? 1 : 0;
            msg.data_length_code = item.dlc;
            memcpy(msg.data, item.data, 8);

            if ((msg.identifier & 0x1FFFFFFFUL) == (ID_BMS_REQUEST & 0x1FFFFFFFUL))
            {
                handleBMSMessage(msg);
            }
            else
            {
                handleChargerMessage(msg);
            }
        }

        // 5. Energy accumulation (1 Hz)
        if (millis() - lastEnergyTick >= 1000)
        {
            float v = 0.0f, i = 0.0f;
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) == pdTRUE)
            {
                v = chargerVolt;
                i = chargerCurr;
                xSemaphoreGive(dataMutex);
            }
            if (v > 0 && i > 0 && i < 300.0f)
            {
                if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(5)) == pdTRUE)
                {
                    energyWh += (v * i) / 3600.0f;
                    xSemaphoreGive(dataMutex);
                }
            }
            lastEnergyTick = millis();
        }

        // 6. Watchdog: only reset if EV present
        twai_status_info_t s;
        if (twai_get_status_info(&s) == ESP_OK)
        {
            if (s.state == TWAI_STATE_BUS_OFF || s.state == TWAI_STATE_STOPPED)
            {
                Serial.println("⚠ TWAI bus-off detected, recovering...");
                twai_stop();
                twai_driver_uninstall();
                vTaskDelay(pdMS_TO_TICKS(100));
                twai_init();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}