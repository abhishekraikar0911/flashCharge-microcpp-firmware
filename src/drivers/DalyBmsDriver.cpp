#include "drivers/DalyBmsDriver.h"
#include <string.h>

DalyBmsDriver::DalyBmsDriver(ICan& can, ITimer& timer, ILogger& logger)
    : can(can), timer(timer), logger(logger),
      packVoltage(0.0f), soc(0.0f), maxCurrent(0.0f),
      faultFlags(0xFF), lastMessageTimeMs(0), lastTxTimeMs(0),
      sysTerminalVolt(0.0f), sysTerminalCurr(0.0f), sysStatusFlags(0) {}

bool DalyBmsDriver::init() {
    packVoltage       = 0.0f;
    soc               = 0.0f;
    faultFlags        = 0xFF; // Assume faulted until proven safe
    lastMessageTimeMs = 0;
    lastTxTimeMs      = 0;
    return true;
}

// ─── TX ─────────────────────────────────────────────────────────────────────

/**
 * @brief Send charger status heartbeat to BMS (CAN ID 0x18FF50E5)
 *
 * Rivot Motors BMS protocol frame:
 *   Byte 0-1 : Output Voltage  (Big-Endian, ×0.1V)
 *   Byte 2-3 : Output Current  (Big-Endian, ×0.1A)
 *   Byte 4   : Status flags
 *   Byte 5-7 : Reserved (0x00)
 *
 * Status flag bits:
 *   Bit 0 (0x01) = Hardware failure
 *   Bit 1 (0x02) = Over-temperature
 *   Bit 3 (0x08) = Battery not connected
 *   Bit 4 (0x10) = BMS comms timeout
 */
void DalyBmsDriver::sendHeartbeat(float terminalVolt, float terminalCurr, uint8_t statusFlags) {
    CanFrame frame;
    frame.id = HEARTBEAT_TX_ID;
    frame.len = 8;
    frame.extended = true;
    memset(frame.data, 0, 8);

    uint16_t v_raw = (uint16_t)(terminalVolt * 10.0f);
    frame.data[0] = (v_raw >> 8) & 0xFF;
    frame.data[1] = v_raw & 0xFF;

    uint16_t i_raw = (uint16_t)(terminalCurr * 10.0f);
    frame.data[2] = (i_raw >> 8) & 0xFF;
    frame.data[3] = i_raw & 0xFF;

    frame.data[4] = statusFlags;
    // Bytes 5-7 reserved = 0x00

    bool ok = can.send(frame);

    static uint32_t lastPrint = 0;
    if (timer.millis() - lastPrint > 5000) {
        lastPrint = timer.millis();
        if (ok || isConnected()) {
            logger.logf(ILogger::Level::DEBUG, "DalyBMS", "TX 0x18FF50E5: V=%.1fV I=%.1fA Flags=0x%02X %s",
                          terminalVolt, terminalCurr, statusFlags, ok ? "OK" : "FAIL");
        }
    }
}

/**
 * @brief Request SOC from BMS (CAN ID 0x18900140)
 * Sends an 8-byte zero frame — BMS responds with CAN ID 0x18904001
 */
void DalyBmsDriver::sendSocRequest() {
    CanFrame frame;
    frame.id = SOC_REQUEST_ID;
    frame.len = 8;
    frame.extended = true;
    memset(frame.data, 0, 8);

    bool ok = can.send(frame);

    static uint32_t lastPrint = 0;
    if (timer.millis() - lastPrint > 5000) {
        lastPrint = timer.millis();
        if (ok || isConnected()) {
            logger.logf(ILogger::Level::DEBUG, "DalyBMS", "TX 0x18900140 (SOC request) %s", ok ? "OK" : "FAIL");
        }
    }
}

// ─── Update (RX + TX poll) ───────────────────────────────────────────────────

void DalyBmsDriver::updateSystemStatus(float _terminalVolt, float _terminalCurr, uint8_t _statusFlags) {
    sysTerminalVolt = _terminalVolt;
    sysTerminalCurr = _terminalCurr;
    sysStatusFlags = _statusFlags;
}

void DalyBmsDriver::update() {
    // --- Periodic TX ---
    uint32_t txInterval = isConnected() ? TX_INTERVAL_MS : (TX_INTERVAL_MS * 5); // Slow down polling if unplugged
    if (timer.millis() - lastTxTimeMs >= txInterval) {
        sendHeartbeat(sysTerminalVolt, sysTerminalCurr, sysStatusFlags);
        sendSocRequest();
        lastTxTimeMs = timer.millis();
    }

    // --- RX: drain the MCP2515 queue ---
    CanFrame rxFrame;

    while (can.receive(rxFrame)) {
        if (rxFrame.len < 8) continue;

        // --- Frame: BMS charge request 0x1806E5F4 ---
        if (rxFrame.id == BMS_REQUEST_ID) {
            lastMessageTimeMs = timer.millis();

            // Byte 0-1: Vmax (×0.1V)
            uint16_t vmax_raw = ((uint16_t)rxFrame.data[0] << 8) | rxFrame.data[1];
            // Plausibility: reject values below 20V (bus noise)
            if (vmax_raw / 10.0f < 20.0f) continue;

            packVoltage = vmax_raw / 10.0f;

            // Byte 2-3: Imax (×0.1A)
            uint16_t imax_raw = ((uint16_t)rxFrame.data[2] << 8) | rxFrame.data[3];
            maxCurrent = imax_raw / 10.0f;

            // Byte 4: Fault/status flags
            faultFlags = rxFrame.data[4];

            logger.logf(ILogger::Level::DEBUG, "DalyBMS", "RX 0x1806E5F4: Vmax=%.1fV Imax=%.1fA Flags=0x%02X",
                          packVoltage, maxCurrent, faultFlags);
        }

        // --- Frame: SOC response 0x18904001 ---
        else if (rxFrame.id == SOC_RESPONSE_ID) {
            // Bytes 6-7: SOC as big-endian uint16 (×0.1 → %)
            uint16_t soc_raw = ((uint16_t)rxFrame.data[6] << 8) | rxFrame.data[7];
            if (soc_raw != 0xFFFF && (soc_raw / 10.0f) <= 100.0f) {
                soc = soc_raw / 10.0f;
                logger.logf(ILogger::Level::DEBUG, "DalyBMS", "RX 0x18904001: SOC=%.1f%%", soc);
            }
        }
    }
}

// ─── Getters ────────────────────────────────────────────────────────────────

float DalyBmsDriver::getPackVoltage()      { return packVoltage; }
float DalyBmsDriver::getSoc()              { return soc; }
float DalyBmsDriver::getMaxChargeCurrent() {
    if (!isConnected() || !isSafeToCharge()) return 0.0f;
    return maxCurrent;
}

bool DalyBmsDriver::isSafeToCharge() {
    // faultFlags == 0x00 means fully healthy
    return (faultFlags == 0x00) && isConnected();
}

bool DalyBmsDriver::isConnected() {
    return getLastMessageAgeMs() <= TIMEOUT_MS;
}

uint32_t DalyBmsDriver::getLastMessageAgeMs() {
    if (lastMessageTimeMs == 0) return 0xFFFFFFFF;
    uint32_t now = timer.millis();
    if (now >= lastMessageTimeMs) return now - lastMessageTimeMs;
    return (0xFFFFFFFF - lastMessageTimeMs) + now + 1; // millis() overflow safe
}
