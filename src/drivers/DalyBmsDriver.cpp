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


// ─── Update (RX + TX poll) ───────────────────────────────────────────────────

void DalyBmsDriver::updateSystemStatus(float _terminalVolt, float _terminalCurr, uint8_t _statusFlags) {
    sysTerminalVolt = _terminalVolt;
    sysTerminalCurr = _terminalCurr;
    sysStatusFlags = _statusFlags;
}

void DalyBmsDriver::update() {
    uint32_t now = timer.millis();

    // --- [P1] Bus-Off Health Guard — Exponential Backoff Recovery ---
    //
    // Problem observed in logs: MCP2515 resets, rejoins bus, isHealthy()=true,
    // TX immediately attempted → ERROR_FAILTX (bus not yet settled) →
    // TEC skyrockets → Bus-Off in <50ms → repeat every 1 second forever.
    //
    // Fix 1: Exponential backoff — reset interval doubles each failure
    //         (1s → 2s → 4s → ... → 30s max). Prevents bus hammering.
    // Fix 2: 500ms post-reset TX blackout — RX is allowed, TX is blocked.
    //         Gives bus time to settle and TEC/REC counters to drain.
    //
    static uint32_t lastBusOffResetMs  = 0;
    static uint32_t busOffBackoffMs    = 1000;  // Start 1s, doubles on each failure
    static uint32_t consecutiveHealthy = 0;

    if (!can.isHealthy()) {
        consecutiveHealthy = 0;
        if (now - lastBusOffResetMs > busOffBackoffMs) {
            lastBusOffResetMs = now;
            logger.logf(ILogger::Level::ERROR, "DalyBMS",
                       "CAN Bus-Off — recovery reset (backoff=%lums)",
                       (unsigned long)busOffBackoffMs);
            can.reset();
            // Double backoff each failure, cap at 30s
            busOffBackoffMs = (busOffBackoffMs < 30000) ? (busOffBackoffMs * 2) : 30000;
        }
        return;
    }

    // CAN is healthy — count consecutive healthy poll cycles.
    // Reset backoff after 10 consecutive healthy cycles (~500ms at 50ms poll rate).
    consecutiveHealthy++;
    if (consecutiveHealthy >= 10) {
        busOffBackoffMs    = 1000; // Reset backoff — bus is stable again
        consecutiveHealthy = 10;   // Cap to prevent overflow
    }

    // --- [P1] Post-Reset TX Stabilization: 500ms blackout after Bus-Off recovery ---
    // During stabilization: RX is allowed (drain queue), TX is suppressed.
    // This prevents immediately hammering a freshly-reset bus with extended frames
    // before TEC/REC counters have had a chance to drain from the previous errors.
    bool inStabilization = (lastBusOffResetMs > 0) && (now - lastBusOffResetMs < 500);
    if (inStabilization) {
        // RX only — drain the software queue so frames don't get stale
        CanFrame rxFrame;
        while (can.receive(rxFrame)) {
            if (rxFrame.len < 8) continue;
            if (rxFrame.id == BMS_REQUEST_ID) {
                lastMessageTimeMs = now;
                uint16_t vmax_raw = ((uint16_t)rxFrame.data[0] << 8) | rxFrame.data[1];
                if (vmax_raw / 10.0f < 20.0f) continue;
                packVoltage = vmax_raw / 10.0f;
                uint16_t imax_raw = ((uint16_t)rxFrame.data[2] << 8) | rxFrame.data[3];
                maxCurrent  = imax_raw / 10.0f;
                faultFlags  = rxFrame.data[4];
                // SOC from bytes 5-6 (same frame)
                uint16_t soc_raw = ((uint16_t)rxFrame.data[5] << 8) | rxFrame.data[6];
                if (soc_raw != 0xFFFF && (soc_raw / 10.0f) <= 100.0f) {
                    soc = soc_raw / 10.0f;
                }
            }
        }
        return; // No TX during stabilization
    }

    // --- Periodic TX: send charger status heartbeat to BMS every 500ms ---
    // Fix for [RX-ERR-PASSIVE]: Only send CAN frames if the BMS is actively connected.
    // Transmitting on an unplugged CAN bus causes physical Ack errors, pushing
    // the MCP2515 into Error Passive mode and triggering TX FAIL logs.
    if (isConnected()) {
        if (now - lastTxTimeMs >= TX_INTERVAL_MS) {
            sendHeartbeat(sysTerminalVolt, sysTerminalCurr, sysStatusFlags);
            lastTxTimeMs = timer.millis();
        }
    }

    // --- RX: drain the MCP2515 software queue ---
    CanFrame rxFrame;

    while (can.receive(rxFrame)) {
        if (rxFrame.len < 8) continue;

        // --- Frame: BMS charge request + SOC (0x1806E5F4) ---
        //
        // Layout (all Big-Endian):
        //   data[0-1] : Vmax   (uint16 ×0.1V)  e.g. 0x035C = 860 → 86.0V
        //   data[2-3] : Imax   (uint16 ×0.1A)  e.g. 0x0410 = 1040 → 104.0A
        //   data[4]   : Fault flags
        //   data[5-6] : SOC    (uint16 ×0.1%)  e.g. 0x0263 = 611 → 61.1%
        //   data[7]   : Reserved
        if (rxFrame.id == BMS_REQUEST_ID) {
            lastMessageTimeMs = timer.millis();

            // Vmax — plausibility check: reject < 20V (bus noise / cold start)
            uint16_t vmax_raw = ((uint16_t)rxFrame.data[0] << 8) | rxFrame.data[1];
            if ((vmax_raw / 10.0f) < 20.0f) continue;
            packVoltage = vmax_raw / 10.0f;

            // Imax
            uint16_t imax_raw = ((uint16_t)rxFrame.data[2] << 8) | rxFrame.data[3];
            maxCurrent = imax_raw / 10.0f;

            // Fault flags
            faultFlags = rxFrame.data[4];

            // SOC — from bytes 5-6 (same frame, no separate request needed)
            uint16_t soc_raw = ((uint16_t)rxFrame.data[5] << 8) | rxFrame.data[6];
            if (soc_raw != 0xFFFF && (soc_raw / 10.0f) <= 100.0f) {
                soc = soc_raw / 10.0f;
                logger.logf(ILogger::Level::DEBUG, "DalyBMS", "RX 0x1806E5F4: Vmax=%.1fV Imax=%.1fA SOC=%.1f%% Flags=0x%02X",
                            packVoltage, maxCurrent, soc, faultFlags);
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
