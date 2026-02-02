#include "header.h"
#include "drivers/can_twai_driver.h"
#include "drivers/can_mcp2515_driver.h"
#include <Arduino.h>
#include <string.h>

// Toggle OCPP telemetry here (set to 1 to enable, 0 to disable)
#define ENABLE_OCPP_TELEMETRY 0
#if ENABLE_OCPP_TELEMETRY
#include "ocpp/csms_communication.h"
#endif

// Response watchdog
static unsigned long lastResp = 0;

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

    // FIX: Use timeout to prevent deadlock
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
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
    else
    {
        // FIX: Log mutex timeout to detect deadlocks
        Serial.println("[CAN] ⚠️  Mutex timeout in decode_0681817E");
    }
    // Update response watchdog
    lastResp = millis();
    // Serial.println("📥 Control response received");
}

static void decode_0681827E(const twai_message_t &msg)
{
    const uint8_t dlc = msg.data_length_code;
    if (dlc < 8)
        return;
    const uint8_t func = msg.data[1];

    // FIX: Use timeout to prevent deadlock
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
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
            chargerCurr = (float(((uint16_t)msg.data[6] << 8) | (uint16_t)msg.data[7])) / 10.0f;
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
    else
    {
        // FIX: Log mutex timeout to detect deadlocks
        Serial.println("[CAN] ⚠️  Mutex timeout in decode_0681827E");
    }
    // Update response watchdog
    lastResp = millis();
    // Serial.println("📥 Telemetry response received");
}

static void decode_00433F01(const twai_message_t &msg)
{
    const uint8_t dlc = msg.data_length_code;
    if (dlc < 8)
        return;

    // FIX: Use timeout to prevent deadlock
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        memcpy(lastTermData1, msg.data, dlc > 8 ? 8 : dlc);
        
        terminalVolt = beFloat(&msg.data[0]);
        terminalCurr = beFloat(&msg.data[4]);  // Already scaled correctly
        terminalchargerPower = terminalVolt * terminalCurr;

        // CRITICAL: Update timestamp for charger health monitoring
        lastTerminalPower = millis();

        // HYBRID PLUG DETECTION - Method 1: Voltage + Current presence
        if (terminalVolt > 56.0f && terminalVolt < 85.5f)
        {
            batteryConnected = true;
            gunPhysicallyConnected = true;
            lastBMS = millis();
        }

        xSemaphoreGive(dataMutex);
    }
    else
    {
        // FIX: Log mutex timeout to detect deadlocks
        Serial.println("[CAN] ⚠️  Mutex timeout in decode_00433F01");
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

        // CRITICAL: Update timestamp for charger health monitoring
        lastTerminalStatus = millis();

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
        
        // CRITICAL: Update timestamp for charger health monitoring
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
    tx.data[0] = 0x01;
    tx.data[1] = func;

    static bool lastEnabled = false;

    if (func == 0x32)
    {
        bool enabled = false;
        bool gunConnected = false;
        bool battConnected = false;
        
        // SAFETY: Read all conditions atomically
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            enabled = chargingEnabled;
            gunConnected = gunPhysicallyConnected;
            battConnected = batteryConnected;
            xSemaphoreGive(dataMutex);
        }
        else
        {
            Serial.println("[SAFETY] ⚠️  Mutex timeout in sendGroupRequest - ABORTING charge command");
            return; // CRITICAL: Do not send command if mutex fails
        }
        
        // SAFETY: All conditions must be true
        bool safeToCharge = enabled && gunConnected && battConnected;

        // RACE CONDITION FIX: Only send on state change
        if (safeToCharge == lastEnabled)
            return;

        lastEnabled = safeToCharge;
        tx.data[2] = 0x00;
        tx.data[3] = safeToCharge ? 0x00 : 0x01;
        
        Serial.printf("[SAFETY] Charging command: %s (gun=%d batt=%d enabled=%d)\n",
            safeToCharge ? "START" : "STOP", gunConnected, battConnected, enabled);
    }
    else if (func == 0x00 || func == 0x03)
    {
        bool enabled = false;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            enabled = chargingEnabled;
            xSemaphoreGive(dataMutex);
        }
        else
        {
            return; // SAFETY: Skip if mutex fails
        }
        
        if (!enabled)
            return;

        uint32_t raw = (func == 0x00) ? cachedRawV : cachedRawI;
        tx.data[4] = (raw >> 24) & 0xFF;
        tx.data[5] = (raw >> 16) & 0xFF;
        tx.data[6] = (raw >> 8) & 0xFF;
        tx.data[7] = raw & 0xFF;
    }

    (void)CAN_TWAI::sendMessage(tx.identifier, tx.data, tx.data_length_code, true);
    g.funcIndex = (g.funcIndex + 1) % g.funcCount;
}

// --- Main comms task ---
void chargerCommTask(void *arg)
{
    static unsigned long lastFeedback = 0;
    static unsigned long lastSOCRequest = 0;
    static unsigned long lastGroupRequest = 0;
    static unsigned long lastBusRecovery = 0;

    while (true)
    {
        // SAFETY: CAN bus error recovery
        twai_status_info_t s;
        if (twai_get_status_info(&s) == ESP_OK)
        {
            // CRITICAL: Immediate recovery on bus-off
            if (s.state == TWAI_STATE_BUS_OFF || s.state == TWAI_STATE_STOPPED)
            {
                if (millis() - lastBusRecovery > 5000) // Prevent rapid recovery loops
                {
                    Serial.println("[CAN] 🚨 BUS-OFF detected, initiating recovery...");
                    
                    // CRITICAL: Stop driver completely before reinstalling
                    esp_err_t err = twai_stop();
                    if (err != ESP_OK) {
                        Serial.printf("[CAN1] Stop failed: %d\n", err);
                    }
                    vTaskDelay(pdMS_TO_TICKS(50));
                    
                    err = twai_driver_uninstall();
                    if (err != ESP_OK) {
                        Serial.printf("[CAN1] Uninstall failed: %d\n", err);
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                    
                    // Reinitialize
                    CAN_TWAI::init();
                    lastBusRecovery = millis();
                    
                    // SAFETY: Disable charging during CAN recovery
                    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
                    {
                        chargingEnabled = false;
                        xSemaphoreGive(dataMutex);
                    }
                }
            }
            
            // Log bus status periodically
            static unsigned long lastBusStatus = 0;
            if (millis() - lastBusStatus >= 10000)
            {
                if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE)
                {
                    Serial.printf("📊 CAN1: State=%d TX_Err=%d RX_Err=%d TX_Q=%d RX_Q=%d\n",
                        s.state, s.tx_error_counter, s.rx_error_counter, s.msgs_to_tx, s.msgs_to_rx);
                    xSemaphoreGive(serialMutex);
                }
                lastBusStatus = millis();
            }
        }

        // Send group requests with proper spacing
        if (millis() - lastGroupRequest >= 500)
        {
            sendGroupRequest(groups[0]);
            vTaskDelay(pdMS_TO_TICKS(50));
            sendGroupRequest(groups[1]);
            vTaskDelay(pdMS_TO_TICKS(50));
            lastGroupRequest = millis();
        }

        // Send charger feedback
        if (millis() - lastFeedback >= 100)
        {
            sendChargerFeedback();
            lastFeedback = millis();
        }

        // Request Ah data periodically
        if (millis() - lastSOCRequest >= 2000)
        {
            requestChargingAh();
            vTaskDelay(pdMS_TO_TICKS(10));
            requestDischargingAh();
            lastSOCRequest = millis();
        }

        // Drain RX queues and dispatch
        RxBufItem item;
        
        // Poll CAN1 (Charger messages)
        while (CAN_TWAI::popFrame(item))
        {
            twai_message_t msg = {};
            msg.identifier = item.id;
            msg.extd = item.ext ? 1 : 0;
            msg.rtr = item.rtr ? 1 : 0;
            msg.data_length_code = item.dlc;
            memcpy(msg.data, item.data, 8);

            handleChargerMessage(msg);
        }
        
        // Poll CAN2 (BMS messages)
        while (CAN_MCP2515::popFrame(item))
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
            else if ((msg.identifier & 0x1FFFFFFFUL) == (ID_CHARGE_AH_RESPONSE & 0x1FFFFFFFUL))
            {
                handleChargingAhMessage(msg);
            }
            else if ((msg.identifier & 0x1FFFFFFFUL) == (ID_DISCHARGE_AH_RESPONSE & 0x1FFFFFFFUL))
            {
                handleDischargingAhMessage(msg);
            }
            else if ((msg.identifier & 0x1FFFFFFFUL) == (ID_SOC_RESPONSE & 0x1FFFFFFFUL))
            {
                handleSOCMessage(msg);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =========================================================
// CHARGER MODULE HEALTH MONITORING
// =========================================================
// Production-grade charger health check based on CAN message timeouts
bool isChargerModuleHealthy()
{
    const unsigned long now = millis();
    const unsigned long CHARGER_TIMEOUT_MS = 3000; // 3 seconds timeout
    
    // Check if we're receiving critical CAN messages from charger
    bool terminalPowerOk = (now - lastTerminalPower) < CHARGER_TIMEOUT_MS;
    bool terminalStatusOk = (now - lastTerminalStatus) < CHARGER_TIMEOUT_MS;
    bool heartbeatOk = (now - lastHeartbeat) < CHARGER_TIMEOUT_MS;
    
    // Charger is healthy if at least 2 out of 3 messages are recent
    int healthyCount = (terminalPowerOk ? 1 : 0) + (terminalStatusOk ? 1 : 0) + (heartbeatOk ? 1 : 0);
    bool healthy = (healthyCount >= 2);
    
    // Update global status
    chargerModuleOnline = healthy;
    
    return healthy;
}
