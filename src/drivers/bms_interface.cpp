#include "header.h"
#include "drivers/can_mcp2515_driver.h"
#include "debug_logger.h"
#include <Arduino.h>
#include <math.h>
#include "drivers/can_utils.h"

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

    LOG_BMS("[CAN2-RX] 0x1806E5F4: %02X %02X %02X %02X %02X %02X %02X %02X",
            msg.data[0], msg.data[1], msg.data[2], msg.data[3],
            msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
    LOG_BMS("Vmax=%.1fV Imax=%.1fA Switch=%s Heating=%s", BMS_Vmax, BMS_Imax, 
            chargingswitch ? "ON" : "OFF", heating ? "YES" : "NO");

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        using namespace can_utils;
        batteryConnected = true;
        lastBMS = millis();

        const uint8_t dlc = msg.data_length_code;
        memcpy(lastBMSData, msg.data, dlc > 8 ? 8 : dlc);

        const uint16_t vmax_raw = parseBEUint16(&msg.data[0]);
        const uint16_t imax_raw = parseBEUint16(&msg.data[2]);
        BMS_Vmax = vmax_raw / 10.0f;
        BMS_Imax = imax_raw / 10.0f;
        
        // Diagnostic: Log raw and calculated values
        static unsigned long lastBmsLog = 0;
        if (millis() - lastBmsLog > 5000) {
            LOG_BMS("BMS: Vmax=%.1fV Imax=%.1fA (raw: 0x%04X=%.1f, 0x%04X=%.1f)",
                BMS_Vmax, BMS_Imax, vmax_raw, vmax_raw/10.0f, imax_raw, imax_raw/10.0f);
            lastBmsLog = millis();
        }

        // SAFETY: Parse charging permission flags
        bool newSafeToCharge = (msg.data[4] == 0x00);
        bool newHeatingActive = (msg.data[5] == 0x01);
        
        // Log state changes
        if (newSafeToCharge != bmsSafeToCharge) {
            LOG_BMS("%s Charging permission: %s (byte4=0x%02X)",
                newSafeToCharge ? "✅" : "🚨",
                newSafeToCharge ? "GRANTED" : "DENIED",
                msg.data[4]);
        }
        if (newHeatingActive != bmsHeatingActive) {
            LOG_BMS("%s Heating: %s (byte5=0x%02X)",
                newHeatingActive ? "⚠️" : "✅",
                newHeatingActive ? "ACTIVE" : "OFF",
                msg.data[5]);
        }
        
        bmsSafeToCharge = newSafeToCharge;
        bmsHeatingActive = newHeatingActive;
        
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
    // Skip if CAN2 driver not initialized
    if (!CAN_MCP2515::isActive()) {
        return;
    }
    
    uint8_t txData[8];
    
    int vraw_i = (int)lroundf(terminalVolt * 10.0f);
    int iraw_i = (int)lroundf(terminalCurr * 10.0f);
    
    if (vraw_i < 0) vraw_i = 0;
    if (vraw_i > 0xFFFF) vraw_i = 0xFFFF;
    if (iraw_i < 0) iraw_i = 0;
    if (iraw_i > 0xFFFF) iraw_i = 0xFFFF;
    
    const uint16_t vraw = (uint16_t)vraw_i;
    const uint16_t iraw = (uint16_t)iraw_i;
    
    txData[0] = (vraw >> 8) & 0xFF;
    txData[1] = vraw & 0xFF;
    txData[2] = (iraw >> 8) & 0xFF;
    txData[3] = iraw & 0xFF;
    txData[4] = buildStatusFlags();
    txData[5] = 0x00;
    txData[6] = 0x00;
    txData[7] = 0x00;
    
    LOG_BMS("[CAN2-TX] 0x18FF50E5: %02X %02X %02X %02X %02X %02X %02X %02X",
        txData[0], txData[1], txData[2], txData[3], txData[4], txData[5], txData[6], txData[7]);
    
    CAN_MCP2515::sendMessage(ID_HEARTBEAT & 0x1FFFFFFFUL, txData, 8, true);
}

void requestSOCFromBMS()
{
    if (!CAN_MCP2515::isActive()) return;
    uint8_t txData[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    LOG_BMS("[CAN2-TX] 0x18900140: 00 00 00 00 00 00 00 00");
    CAN_MCP2515::sendMessage(ID_SOC_REQUEST & 0x1FFFFFFFUL, txData, 8, true);
}

void handleSOCMessage(const twai_message_t &msg)
{
    if (!msg.extd)
        return;

    if ((msg.identifier & 0x1FFFFFFFUL) != (ID_SOC_RESPONSE & 0x1FFFFFFFUL))
        return;

    if (msg.data_length_code < 8)
        return;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        // Last 2 bytes (bytes 6-7) contain SOC as big-endian uint16
        uint16_t soc_raw = (msg.data[6] << 8) | msg.data[7];
        socPercent = soc_raw / 10.0f;  // Divide by 10 to get percentage
        
        // Clamp to valid range
        if (socPercent < 0.0f) socPercent = 0.0f;
        if (socPercent > 100.0f) socPercent = 100.0f;
        
        LOG_BMS("[CAN2-RX] 0x18904001: %02X %02X %02X %02X %02X %02X %02X %02X -> SOC=%.1f%%",
                msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                msg.data[4], msg.data[5], msg.data[6], msg.data[7],
                socPercent);
        
        // Update battery Ah and range based on SOC
        float maxCapacityAh;
        if (BMS_Imax > 60.0f) {
            maxCapacityAh = 90.0f;
            vehicleModel = 3;
        } else if (BMS_Imax > 30.0f) {
            maxCapacityAh = 60.0f;
            vehicleModel = 2;
        } else {
            maxCapacityAh = 30.0f;
            vehicleModel = 1;
        }
        
        batteryAh = (socPercent / 100.0f) * maxCapacityAh;
        rangeKm = batteryAh * 2.7f;
        
        if (socPercent > 0.0f) {
            batteryConnected = true;
        }

        xSemaphoreGive(dataMutex);
    }
}
