#include "hal/esp32/Esp32MCP2515.h"
#include <Arduino.h>

Esp32MCP2515::Esp32MCP2515(int csPin, CAN_CLOCK freq) 
    : mcp(nullptr), clockFreq(freq), isInit(false), currentBaud(250000) {
    // Save csPin to use in init()
    this->csPin = csPin;
}

bool Esp32MCP2515::init(uint32_t baudrate) {
    if (mcp == nullptr) {
        mcp = new MCP2515(csPin);
    }
    currentBaud = baudrate;
    mcp->reset();

    CAN_SPEED speed = getMcpSpeed(baudrate);
    if (mcp->setBitrate(speed, clockFreq) != MCP2515::ERROR_OK) {
        return false;
    }

    if (mcp->setNormalMode() != MCP2515::ERROR_OK) {
        return false;
    }

    isInit = true;
    return true;
}

bool Esp32MCP2515::send(const CanFrame& frame) {
    if (!isInit) return false;

    can_frame mcpFrame;
    mcpFrame.can_id = frame.id;
    if (frame.extended) {
        mcpFrame.can_id |= CAN_EFF_FLAG; // Mark as Extended Frame Format
    }
    mcpFrame.can_dlc = frame.len > 8 ? 8 : frame.len;
    for (int i = 0; i < mcpFrame.can_dlc; i++) {
        mcpFrame.data[i] = frame.data[i];
    }

    MCP2515::ERROR err = mcp->sendMessage(&mcpFrame);
    if (err != MCP2515::ERROR_OK) {
        static uint32_t lastErrLog = 0;
        uint32_t interval = (err == MCP2515::ERROR_ALLTXBUSY) ? 30000 : 5000;
        if (millis() - lastErrLog > interval) {
            lastErrLog = millis();
            Serial.printf("[HAL_CAN2] TX FAIL 0x%08lX err=%d\n", (long unsigned int)frame.id, (int)err);
        }
    }
    return err == MCP2515::ERROR_OK;
}

bool Esp32MCP2515::receive(CanFrame& frame) {
    if (!isInit) return false;

    can_frame mcpFrame;
    if (mcp->readMessage(&mcpFrame) == MCP2515::ERROR_OK) {
        // Strip out the EFF/RTR flags to get the raw ID
        frame.id = mcpFrame.can_id & 0x1FFFFFFF;
        frame.len = mcpFrame.can_dlc;
        frame.extended = (mcpFrame.can_id & CAN_EFF_FLAG) != 0;
        frame.timestamp_ms = millis();
        for (int i = 0; i < frame.len; i++) {
            frame.data[i] = mcpFrame.data[i];
        }

        static uint32_t lastRxLog = 0;
        if (millis() - lastRxLog > 5000) {
            lastRxLog = millis();
            Serial.printf("[HAL_CAN2] RX OK ID: 0x%08lX len:%d\n", (long unsigned int)frame.id, (int)frame.len);
        }

        return true;
    }
    return false;
}

bool Esp32MCP2515::isHealthy() {
    if (!isInit) return false;
    // Basic health check: Ensure we aren't in configuration mode magically and check error registers if needed.
    // getErrorFlags() could be used here. For simplicity, check if checkReceive works without bus crash.
    uint8_t errorFlags = mcp->getErrorFlags();
    return (errorFlags & MCP2515::EFLG_TXBO) == 0; // Return false if bus-off
}

void Esp32MCP2515::reset() {
    if (!isInit) return;
    // Re-initialize to clear errors
    mcp->reset();
    CAN_SPEED speed = getMcpSpeed(currentBaud);
    mcp->setBitrate(speed, clockFreq);
    mcp->setNormalMode();
}

CAN_SPEED Esp32MCP2515::getMcpSpeed(uint32_t baudrate) {
    switch (baudrate) {
        case 125000: return CAN_125KBPS;
        case 250000: return CAN_250KBPS;
        case 500000: return CAN_500KBPS;
        case 1000000: return CAN_1000KBPS;
        default:     return CAN_250KBPS;
    }
}
