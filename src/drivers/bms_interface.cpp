#include "header.h"
#include "drivers/can_mcp2515_driver.h"
#include "debug_logger.h"
#include "utils/can_validator.h"
#include <Arduino.h>
#include <math.h>
#include "drivers/can_utils.h"
#include "modules/system_state.h"

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

void requestSOCFromBMS()
{
    uint8_t data[8] = {0};
    CAN_MCP2515::sendMessage(ID_SOC_REQUEST, data, 8, true);
}

void sendChargerFeedback()
{
    uint8_t data[8] = {0};
    data[0] = buildStatusFlags();
    // Usually, the charger would also send current voltage/current here, 
    // but the flags are the most critical for the BMS health watchdog.
    CAN_MCP2515::sendMessage(ID_HEARTBEAT, data, 8, true);
}

void handleBMSMessage(const twai_message_t &msg)
{
    if (!msg.extd)
        return;
    if ((msg.identifier & 0x1FFFFFFFUL) != (ID_BMS_REQUEST & 0x1FFFFFFFUL))
        return;

    // CRITICAL FIX: Validate message structure
    if (!CANValidator::validateMessage(msg))
    {
        LOG_BMS("⚠️  Invalid BMS message structure");
        return;
    }

    // CRITICAL FIX: Validate raw data
    if (!CANValidator::validateRawData(msg.data, msg.data_length_code))
    {
        LOG_BMS("⚠️  BMS message appears to be noise");
        return;
    }

    LOG_BMS("[CAN2-RX] 0x1806E5F4: %02X %02X %02X %02X %02X %02X %02X %02X",
            msg.data[0], msg.data[1], msg.data[2], msg.data[3],
            msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
    LOG_BMS("Vmax=%.1fV Imax=%.1fA Switch=%s Heating=%s", BMS_Vmax, BMS_Imax, 
            chargingswitch ? "ON" : "OFF", heating ? "YES" : "NO");

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        using namespace can_utils;
        batteryConnected = true;
        gunPhysicallyConnected = true;
        SystemState::instance().setBatteryConnected(true);
        SystemState::instance().setGunPhysicallyConnected(true);
        lastBMS = millis();

        const uint16_t vmax_raw = parseBEUint16(&msg.data[0]);
        const uint16_t imax_raw = parseBEUint16(&msg.data[2]);
        
        // SANITY CHECK: If both Vmax and Imax are 0xFFFF, the entire frame is likely garbage/bus-off noise
        if (vmax_raw == 0xFFFF && imax_raw == 0xFFFF) {
            LOG_BMS("⚠️  Ignoring frame: Critical fields 0xFFFF (Bus-Off / Noise)");
            xSemaphoreGive(dataMutex);
            return;
        }

        batteryConnected = true;
        gunPhysicallyConnected = true;
        SystemState::instance().setBatteryConnected(true);
        SystemState::instance().setGunPhysicallyConnected(true);
        lastBMS = millis();

        const uint8_t dlc = msg.data_length_code;
        memcpy(lastBMSData, msg.data, dlc > 8 ? 8 : dlc);

        // Individual field validation
        if (vmax_raw <= 1500 && imax_raw <= 2000) {
            BMS_Vmax = vmax_raw / 10.0f;
            BMS_Imax = imax_raw / 10.0f;
            
            // CRITICAL FIX: Validate parsed values
            if (!CANValidator::validateVoltage(BMS_Vmax))
            {
                LOG_BMS("⚠️  Invalid BMS_Vmax: %.1fV", BMS_Vmax);
                BMS_Vmax = 0.0f;
            }
            if (!CANValidator::validateCurrent(BMS_Imax))
            {
                LOG_BMS("⚠️  Invalid BMS_Imax: %.1fA", BMS_Imax);
                BMS_Imax = 0.0f;
            }
            
            SystemState::instance().setBMS_Vmax(BMS_Vmax);
            SystemState::instance().setBMS_Imax(BMS_Imax);

            // CRITICAL: Map BMS limits to Charger Module raw format
            // cachedRawV = float * 1024 (Charger spec)
            // cachedRawI = float * 30.5 (Charger spec)
            cachedRawV = (uint32_t)(BMS_Vmax * 1024.0f);
            cachedRawI = (uint32_t)(BMS_Imax * 30.5f);
        }
        
        // Diagnostic: Log raw and calculated values
        static unsigned long lastBmsLog = 0;
        if (millis() - lastBmsLog > 5000) {
            LOG_BMS("BMS: Vmax=%.1fV Imax=%.1fA (raw: 0x%04X, 0x%04X)",
                BMS_Vmax, BMS_Imax, vmax_raw, imax_raw);
            lastBmsLog = millis();
        }

        // SAFETY: Parse charging permission flags (only if frame is not garbage)
        bool newSafeToCharge = (msg.data[4] == 0x00);
        if (newSafeToCharge != bmsSafeToCharge) {
            LOG_BMS("BMS Safety: %s -> %s", bmsSafeToCharge ? "ENABLED" : "DISABLED", newSafeToCharge ? "ENABLED" : "DISABLED");
            bmsSafeToCharge = newSafeToCharge;
            SystemState::instance().setBmsSafeToCharge(bmsSafeToCharge);
        }

        chargingswitch = (msg.data[4] == 0x00);
        heating = (msg.data[5] == 0x01);

        xSemaphoreGive(dataMutex);
    }
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
        
        // SANITY CHECK: Ignore 0xFFFF or clearly invalid SOC values
        if (soc_raw == 0xFFFF || (soc_raw / 10.0f) > 100.0f) {
            LOG_BMS("⚠️  Ignoring invalid SOC data (Raw: 0x%04X)", soc_raw);
        } else {
            float newSoc = soc_raw / 10.0f;  // Divide by 10 to get percentage
            
            // Clamp to valid range (double safety)
            if (newSoc < 0.0f) newSoc = 0.0f;
            if (newSoc > 100.0f) newSoc = 100.0f;
            
            // CRITICAL FIX: Additional validation
            if (!CANValidator::validateSOC(newSoc))
            {
                LOG_BMS("⚠️  SOC validation failed: %.1f%%", newSoc);
                xSemaphoreGive(dataMutex);
                return;
            }
            
            SystemState::instance().setSocPercent(newSoc);
            
            LOG_BMS("[CAN2-RX] 0x18904001: %02X %02X %02X %02X %02X %02X %02X %02X -> SOC=%.1f%%",
                    msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                    msg.data[4], msg.data[5], msg.data[6], msg.data[7],
                    newSoc);
            
            // Update battery Ah and range based on SOC
            float maxCapacityAh;
            uint8_t newModel;
            if (BMS_Imax > 60.0f) {
                maxCapacityAh = 90.0f;
                newModel = 3;
            } else if (BMS_Imax > 30.0f) {
                maxCapacityAh = 60.0f;
                newModel = 2;
            } else {
                maxCapacityAh = 30.0f;
                newModel = 1;
            }
            
            SystemState::instance().setVehicleModel(newModel);
            
            float newAh = (newSoc / 100.0f) * maxCapacityAh;
            SystemState::instance().setBatteryAh(newAh);
            
            float newRange = newAh * 2.7f;
            SystemState::instance().setRangeKm(newRange);
            
            if (newSoc > 0.0f) {
                SystemState::instance().setBatteryConnected(true);
            }
        }

        xSemaphoreGive(dataMutex);
    }
}
