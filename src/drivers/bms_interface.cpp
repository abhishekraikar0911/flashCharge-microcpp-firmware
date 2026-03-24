#include "header.h"
#include "drivers/can_mcp2515_driver.h"
#include "utils/debug_logger.h"
#include "utils/can_validator.h"
#include <Arduino.h>
#include <math.h>
#include "drivers/can_utils.h"
#include "modules/system_state.h"

// ====== Build status flags for 0x18FF50E5 ======
static uint8_t buildStatusFlags()
{
    uint8_t flags = 0;
    auto snap = SystemState::instance().snapshot();
    
    // Bit0: Hardware failure (0: Normal. 1: Hardware Failure)
    if (!isChargerModuleHealthy()) {
        flags |= 0x01;
    }
    
    // Bit1: Over temperature (0: Normal. 1: Over temperature protection)
    if (snap.chargerTemp > 70.0f) {
        flags |= 0x02;
    }
    
    // Bit2: Input Voltage (0: Normal. 1: Input failure)
    // Need a specific signal for this, keeping 0 (Normal) for now.
    
    // Bit3: Starting state (0: Connected. 1: Not connected/reversed)
    if (!snap.batteryConnected) {
        flags |= 0x08;
    }
    
    // Bit4: Communication State (0: Normal. 1: Timeout)
    if ((millis() - snap.lastBMS) > 5000) {
        flags |= 0x10;
    }
    
    return flags;
}

void requestSOCFromBMS()
{
    uint8_t data[8] = {0};
    CAN_MCP2515::sendMessage(ID_SOC_REQUEST, data, 8, true);
}

void sendChargerFeedback()
{
    auto snap = SystemState::instance().snapshot();

    uint8_t data[8] = {0};

    // BYTE1-2: Output Voltage (Big-Endian, 0.1V/bit)
    uint16_t v_raw = (uint16_t)(snap.terminalVolt * 10.0f);
    data[0] = (uint8_t)(v_raw >> 8);
    data[1] = (uint8_t)(v_raw & 0xFF);

    // BYTE3-4: Output Current (Big-Endian, 0.1A/bit)
    uint16_t i_raw = (uint16_t)(snap.terminalCurr * 10.0f);
    data[2] = (uint8_t)(i_raw >> 8);
    data[3] = (uint8_t)(i_raw & 0xFF);

    // BYTE5: Status Flags
    data[4] = buildStatusFlags();

    // BYTE6-8: Reserved (0x00)
    
    // Diagnostic: Log what we are sending to the BMS via CAN ID 18FF50E5
    static unsigned long lastFeedLog = 0;
    if (millis() - lastFeedLog > 5000) {
        Serial.printf("[CAN2-TX] 18FF50E5: V=%.1fV I=%.1fA Flags=0x%02X\n", 
                     snap.terminalVolt, snap.terminalCurr, data[4]);
        lastFeedLog = millis();
    }

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
    auto snap = SystemState::instance().snapshot();
    LOG_BMS("Vmax=%.1fV Imax=%.1fA Switch=%s Heating=%s", snap.BMS_Vmax, snap.BMS_Imax, 
            snap.chargingSwitch ? "ON" : "OFF", snap.heating ? "YES" : "NO");

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        using namespace can_utils;
        auto& state = SystemState::instance();

        // PLAUSIBILITY GUARD: Only mark vehicle present if reported voltage is realistic.
        const uint16_t vmax_preview = (((uint16_t)msg.data[0]) << 8) | msg.data[1];
        const float vmax_float = vmax_preview / 10.0f;
        if (vmax_float < 20.0f) {
            LOG_BMS("⚠️  BMS frame ignored: Vmax=%.1fV too low — likely CAN noise, not a real vehicle", vmax_float);
            xSemaphoreGive(dataMutex);
            return;
        }

        state.setBatteryConnected(true);
        state.setGunPhysicallyConnected(true);
        state.setLastBMS(millis());

        const uint16_t vmax_raw = parseBEUint16(&msg.data[0]);
        const uint16_t imax_raw = parseBEUint16(&msg.data[2]);
        
        // SANITY CHECK: If both Vmax and Imax are 0xFFFF, the entire frame is likely garbage/bus-off noise
        if (vmax_raw == 0xFFFF && imax_raw == 0xFFFF) {
            LOG_BMS("⚠️  Ignoring frame: Critical fields 0xFFFF (Bus-Off / Noise)");
            xSemaphoreGive(dataMutex);
            return;
        }

        const uint8_t dlc = msg.data_length_code;

        // Individual field validation
        if (vmax_raw <= 1500 && imax_raw <= 2000) {
            float bmsVmax = vmax_raw / 10.0f;
            float bmsImax = imax_raw / 10.0f;
            
            // CRITICAL FIX: Validate parsed values
            if (!CANValidator::validateVoltage(bmsVmax))
            {
                LOG_BMS("⚠️  Invalid BMS_Vmax: %.1fV", bmsVmax);
                bmsVmax = 0.0f;
            }
            if (!CANValidator::validateCurrent(bmsImax))
            {
                LOG_BMS("⚠️  Invalid BMS_Imax: %.1fA", bmsImax);
                bmsImax = 0.0f;
            }
            
            state.setBMS_Vmax(bmsVmax);
            state.setBMS_Imax(bmsImax);

            // CRITICAL: Map BMS limits to Charger Module raw format
            cachedRawV = (uint32_t)(bmsVmax * 1024.0f);
            cachedRawI = (uint32_t)(bmsImax * 30.5f);
        }
        
        // Diagnostic: Log raw and calculated values
        static unsigned long lastBmsLog = 0;
        if (millis() - lastBmsLog > 5000) {
            float bv = state.getBMS_Vmax();
            float bi = state.getBMS_Imax();
            LOG_BMS("BMS: Vmax=%.1fV Imax=%.1fA (raw: 0x%04X, 0x%04X)",
                bv, bi, vmax_raw, imax_raw);
            lastBmsLog = millis();
        }

        // H4 FIX: Parse individual BMS fault bits from byte 4 for granular fault reporting
        // Byte 4 bit definitions per Rivot Motors BMS protocol v1.0:
        //   0x00 = All OK / safe to charge
        //   Bit 0 (0x01) = Over-current protection active
        //   Bit 1 (0x02) = Cell voltage fault (over/under voltage)
        //   Bit 2 (0x04) = Thermal warning (battery temperature)
        //   Bit 3 (0x08) = Communication fault (internal BMS)
        //   Bit 7 (0x80) = Emergency stop from BMS
        uint8_t faultByte = msg.data[4];
        bool newSafeToCharge = (faultByte == 0x00);
        bool curSafe = state.getBmsSafeToCharge();
        if (newSafeToCharge != curSafe) {
            if (!newSafeToCharge) {
                // Log which specific fault bits are set
                LOG_BMS("BMS FAULT byte=0x%02X:%s%s%s%s%s", faultByte,
                    (faultByte & 0x01) ? " OVER_CURRENT" : "",
                    (faultByte & 0x02) ? " CELL_VOLTAGE" : "",
                    (faultByte & 0x04) ? " THERMAL"      : "",
                    (faultByte & 0x08) ? " COMM_FAULT"   : "",
                    (faultByte & 0x80) ? " EMERGENCY_STOP" : "");
            } else {
                LOG_BMS("BMS Safety: RESTORED (all faults cleared)");
            }
            state.setBmsSafeToCharge(newSafeToCharge);
        }

        state.setChargingSwitch(faultByte == 0x00);
        // Byte 5: 0x01 = heating active
        state.setHeating(msg.data[5] == 0x01 ? 1 : 0);

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
            float bmsImax = SystemState::instance().getBMS_Imax();
            if (bmsImax > 60.0f) {
                maxCapacityAh = 90.0f;
                newModel = 3;
            } else if (bmsImax > 30.0f) {
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
