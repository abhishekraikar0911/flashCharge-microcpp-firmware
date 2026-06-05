#include "drivers/charger/CM1ChargerDriver.h"
#include <string.h>

// CM1 Polling IDs (Solicited by MCU)
static const uint32_t CM1_CTRL_TX_ID   = 0x068181FE; // Group 1 requests
static const uint32_t CM1_TELEM_TX_ID  = 0x068182FE; // Group 2 requests
static const uint32_t CM1_CTRL_RX_ID   = 0x0681817E; // Group 1 responses
static const uint32_t CM1_TELEM_RX_ID  = 0x0681827E; // Group 2 responses

CM1ChargerDriver::CM1ChargerDriver(ICan& can, ITimer& timer, ILogger& logger) 
    : can(can), timer(timer), logger(logger), currentVmax(0), currentImax(0), isOutputEnabled(false),
      outVolts(0), outAmps(0), internalTemp(0), isTermCharging(false),
      lastTelemetryTime(0), lastControlSendTime(0) {}

bool CM1ChargerDriver::init() {
    outVolts = 0.0f;
    outAmps = 0.0f;
    internalTemp = 25.0f;
    isOutputEnabled = false;
    isTermCharging = false;
    lastTelemetryTime = 0;
    
    return stopCharging();
}

bool CM1ChargerDriver::startCharging(float targetVoltage, float maxCurrent) {
    currentVmax = targetVoltage;
    currentImax = maxCurrent;
    isOutputEnabled = true;

    uint32_t rawV = (uint32_t)(targetVoltage * 1024.0f);
    uint32_t rawI = (uint32_t)(maxCurrent * 30.5f);

    bool ok = true;
    ok &= sendControlMessage(0x00, rawV, 0);
    ok &= sendControlMessage(0x03, rawI, 0);
    ok &= sendControlMessage(0x32, 0, 0x00);
    
    lastControlSendTime = timer.millis();
    return ok;
}

void CM1ChargerDriver::updateLimits(float targetVoltage, float maxCurrent) {
    if (isOutputEnabled) {
        currentVmax = targetVoltage;
        currentImax = maxCurrent;
    }
}

bool CM1ChargerDriver::stopCharging() {
    isOutputEnabled = false;
    currentImax = 0.0f;
    bool ok = sendControlMessage(0x32, 0, 0x01);
    lastControlSendTime = timer.millis();
    return ok;
}

bool CM1ChargerDriver::getTelemetry(float& volts, float& amps, float& temp) {
    volts = outVolts;
    amps = outAmps;
    temp = internalTemp;
    
    if (lastTelemetryTime == 0 || (timer.millis() - lastTelemetryTime > 10000)) {
        return false;
    }
    return true;
}

bool CM1ChargerDriver::isReady() {
    uint32_t now = timer.millis();
    bool ready = (lastTelemetryTime > 0) && (now - lastTelemetryTime < 10000);
    
    static uint32_t lastLog = 0;
    if (now - lastLog > 10000) {  // 10s — WARN level, needs prompt visibility
        lastLog = now;
        if (!ready) {
            logger.logf(ILogger::Level::WARN, "CM1_DRV", "NOT READY - lastTel:%lu now:%lu", 
                        (long unsigned int)lastTelemetryTime, (long unsigned int)now);
        }
    }
    return ready;
}

bool CM1ChargerDriver::hasFault() {
    if (lastTelemetryTime > 0 && (timer.millis() - lastTelemetryTime > 15000)) {
        return true; 
    }
    return false;
}

void CM1ChargerDriver::update() {
    uint32_t now = timer.millis();
    
    // Group 1: Send periodic control (Vmax, Imax, Status) one at a time every 300ms
    if (now - lastControlSendTime >= 300) {
        lastControlSendTime = now;
        static uint8_t ctrlSeq = 0;
        
        uint32_t rawV = (uint32_t)(currentVmax * 1024.0f);
        uint32_t rawI = (uint32_t)(currentImax * 30.5f);
        
        if (ctrlSeq == 0) {
            sendControlMessage(0x32, 0, isOutputEnabled ? 0x00 : 0x01);
        } else if (ctrlSeq == 1) {
            sendControlMessage(0x00, rawV, 0);
        } else if (ctrlSeq == 2) {
            sendControlMessage(0x03, rawI, 0);
        }
        ctrlSeq = (ctrlSeq + 1) % 3;
    }

    // Group 2: Send periodic telemetry requests one at a time every 200ms
    static uint32_t lastTelemReqTime = 0;
    if (now - lastTelemReqTime >= 200) {
        lastTelemReqTime = now;
        static uint8_t telemSeq = 0;
        
        uint8_t reqfuncs[5] = {0x84, 0x82, 0x79, 0x80, 0x83};
        CanFrame req;
        req.id = CM1_TELEM_TX_ID;
        req.len = 8;
        req.extended = true;
        memset(req.data, 0, 8);
        req.data[0] = 0x01;
        req.data[1] = reqfuncs[telemSeq];
        
        (void)can.send(req);
        
        telemSeq = (telemSeq + 1) % 5;
    }

    // 2. Poll RX Queue
    CanFrame frame;

    while (can.receive(frame)) {
        if (frame.len < 8) continue;
        uint32_t maskedId = frame.id & 0x1FFFFFFFUL;
        
        // DEBUG: Log ALL Control Responses (Group 1) without rate limiting to see if charger rejects command
        if (maskedId == CM1_CTRL_RX_ID) {
            logger.logf(ILogger::Level::DEBUG, "CM1_CTRL_RX", "Func: 0x%02X R2: %02X R3: %02X Raw: %02X %02X %02X %02X",
                          frame.data[1], frame.data[2], frame.data[3], frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
        }
        
        // DEBUG: Rate-limited logging for telemetry frames
        static uint32_t lastFrameLog = 0;
        if (timer.millis() - lastFrameLog > 1000) { 
            if (maskedId != CM1_CTRL_RX_ID) { // Only log others
                logger.logf(ILogger::Level::DEBUG, "CM1_RX", "ID: 0x%08lX Data: %02X %02X %02X %02X %02X %02X %02X %02X", 
                               (long unsigned int)maskedId, frame.data[0], frame.data[1], frame.data[2], frame.data[3], frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
            }
            lastFrameLog = timer.millis();
        }

        // Any valid frame from the charger counts as a heartbeat/telemetry activity
        lastTelemetryTime = timer.millis();

        if (maskedId == CM1_TELEM_RX_ID) {
            uint8_t func = frame.data[1];
            if (func == 0x84) {
                uint32_t rawVolt = (frame.data[4] << 24) | (frame.data[5] << 16) | (frame.data[6] << 8) | frame.data[7];
                outVolts = (float)rawVolt / 1024.0f;
            } else if (func == 0x82) {
                uint16_t rawCurr = (frame.data[6] << 8) | frame.data[7];
                outAmps = (float)rawCurr / 1024.0f;
            } else if (func == 0x80) {
                uint16_t rawTemp = (frame.data[6] << 8) | frame.data[7];
                internalTemp = (float)rawTemp * 0.001f;
            }
        } 
        else if (maskedId == (CM1_ID_TERM_POWER & 0x1FFFFFFFUL)) {
            outVolts = decodeBEFloat(&frame.data[0]);
            outAmps = decodeBEFloat(&frame.data[4]);
        } 
        else if (maskedId == (CM1_ID_TERM_STATUS & 0x1FFFFFFFUL)) {
            isTermCharging = (frame.data[6] == 0x03 && frame.data[7] == 0x02);
        }
    }
}

bool CM1ChargerDriver::sendControlMessage(uint8_t func, uint32_t rawData, uint8_t byte3) {
    CanFrame frame;
    frame.id = CM1_CTRL_TX_ID;
    frame.len = 8;
    frame.extended = true;
    memset(frame.data, 0, 8);
    
    frame.data[0] = 0x01;
    frame.data[1] = func;
    frame.data[2] = 0x00;
    frame.data[3] = byte3;
    
    if (func == 0x00 || func == 0x03) {
        frame.data[4] = (rawData >> 24) & 0xFF;
        frame.data[5] = (rawData >> 16) & 0xFF;
        frame.data[6] = (rawData >> 8) & 0xFF;
        frame.data[7] = rawData & 0xFF;
    }

    bool ok = can.send(frame);
    
    static uint32_t lastLog = 0;
    if (timer.millis() - lastLog > 5000) {
        lastLog = timer.millis();
        logger.logf(ILogger::Level::DEBUG, "CM1", "TX ctrl func=0x%02X %s (ID=0x%08lX)", 
                    func, ok ? "OK" : "FAIL", (long unsigned int)CM1_CTRL_TX_ID);
    }
    return ok;
}

float CM1ChargerDriver::decodeBEFloat(const uint8_t* b) {
    uint8_t tmp[4] = {b[3], b[2], b[1], b[0]};
    float f;
    memcpy(&f, tmp, sizeof(f));
    return f;
}
