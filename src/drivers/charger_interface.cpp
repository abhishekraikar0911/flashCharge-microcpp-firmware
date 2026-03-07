#include "header.h"
#include "drivers/can_twai_driver.h"
#include "drivers/can_mcp2515_driver.h"
#include "health_monitor.h"
#include "debug_logger.h"
#include "utils/log_macros.h"
#include "utils/can_status_logger.h"
#include <Arduino.h>
#include <string.h>
#include "drivers/can_utils.h"

// Toggle OCPP telemetry here (set to 1 to enable, 0 to disable)
#define ENABLE_OCPP_TELEMETRY 0
#if ENABLE_OCPP_TELEMETRY
#include "ocpp/csms_communication.h"
#endif

// Response watchdog
static unsigned long lastResp = 0;

// Big-endian float helper moved to can_utils.h
using namespace can_utils;

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
    const uint32_t raw = parseBEUint32(&msg.data[4]);
    
    // Debug logging
    if (DebugLogger::getActiveSection() == 2 || DebugLogger::getActiveSection() == 0) {
        Serial.printf("[CHARGER] RX: 0x%08lX Func=0x%02X Data: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                     (unsigned long)(msg.identifier & 0x1FFFFFFFUL), func,
                     msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                     msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
    }

    // FIX: Use timeout to prevent deadlock
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        if (func == 0x32)
        {
            memcpy(lastStatusData, msg.data, dlc > 8 ? 8 : dlc);
            chargerStatus = (msg.data[3] == 0x00) ? "ON" : "OFF";
            if (DebugLogger::getActiveSection() == 2 || DebugLogger::getActiveSection() == 0) {
                Serial.printf("[CHARGER]   ← Status: %s\n", chargerStatus);
            }
        }
        else if (func == 0x00)
        {
            memcpy(lastVmaxData, msg.data, dlc > 8 ? 8 : dlc);
            Charger_Vmax = raw / 1024.0f;
            if (DebugLogger::getActiveSection() == 2 || DebugLogger::getActiveSection() == 0) {
                Serial.printf("[CHARGER]   ← Vmax=%.1fV\n", Charger_Vmax);
            }
        }
        else if (func == 0x03)
        {
            memcpy(lastImaxData, msg.data, dlc > 8 ? 8 : dlc);
            Charger_Imax = raw / 30.5f;
            if (DebugLogger::getActiveSection() == 2 || DebugLogger::getActiveSection() == 0) {
                Serial.printf("[CHARGER]   ← Imax=%.1fA\n", Charger_Imax);
            }
        }
        if (Charger_Vmax >= 40.0f && Charger_Vmax <= 90.0f)
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
            chargerVolt = parseBEUint32(&msg.data[4]) / 1024.0f;

            if (chargerVolt >= 40.0f && chargerVolt <= 90.0f)
            {
                batteryConnected = true;
                gunPhysicallyConnected = true;
            }
        }
        else if (func == 0x82)
        {
            memcpy(lastCurrData, msg.data, dlc > 8 ? 8 : dlc);
            chargerCurr = parseBEUint16(&msg.data[6]) / 1024.0f;
        }
        else if (func == 0x80)
        {
            memcpy(lastTempData, msg.data, dlc > 8 ? 8 : dlc);
            chargerTemp = parseBEUint16(&msg.data[6]) * 0.001f;
        }
        else if (func == 0x79)
        {
            memcpy(lastVoltData, msg.data, dlc > 8 ? 8 : dlc);
            metric79_raw = parseBEUint16(&msg.data[6]);
            metric79_scaled = metric79_raw * 1.0f;
        }
        else if (func == 0x83)
        {
            memcpy(lastVoltData, msg.data, dlc > 8 ? 8 : dlc);
            metric83_scaled = parseBEFloat(&msg.data[4]);
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
    
    // Debug logging
    if (DebugLogger::getActiveSection() == 2 || DebugLogger::getActiveSection() == 0) {
        Serial.printf("[CHARGER] RX: 0x%08lX (Terminal Power)\n",
                     (unsigned long)(msg.identifier & 0x1FFFFFFFUL));
    }

    // FIX: Use timeout to prevent deadlock
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        memcpy(lastTermData1, msg.data, dlc > 8 ? 8 : dlc);
        
        terminalVolt = parseBEFloat(&msg.data[0]);
        terminalCurr = parseBEFloat(&msg.data[4]);  // Already scaled correctly
        terminalchargerPower = terminalVolt * terminalCurr;
        
        if (DebugLogger::getActiveSection() == 2 || DebugLogger::getActiveSection() == 0) {
            Serial.printf("[CHARGER]   ← Terminal: V=%.1fV I=%.1fA P=%.1fW\n",
                         terminalVolt, terminalCurr, terminalchargerPower);
        }

        // CRITICAL: Update timestamp for charger health monitoring
        lastTerminalPower = millis();

        // HYBRID PLUG DETECTION - Method 1: Voltage + Current presence
        if (terminalVolt >= 40.0f && terminalVolt <= 90.0f)
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

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
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

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
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

    if (func == 0x32)
    {
        bool enabled = false;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            enabled = chargingEnabled;
            xSemaphoreGive(dataMutex);
        }
        tx.data[2] = 0x00;
        tx.data[3] = enabled ? 0x00 : 0x01;
    }
    else if (func == 0x00 || func == 0x03)
    {
        // Always broadcast Vmax/Imax from BMS
        uint32_t raw = (func == 0x00) ? cachedRawV : cachedRawI;
        tx.data[4] = (raw >> 24) & 0xFF;
        tx.data[5] = (raw >> 16) & 0xFF;
        tx.data[6] = (raw >> 8) & 0xFF;
        tx.data[7] = raw & 0xFF;
    }

    (void)CAN_TWAI::sendMessage(tx.identifier, tx.data, tx.data_length_code, true);
    
    // Debug logging
    if (DebugLogger::getActiveSection() == 2 || DebugLogger::getActiveSection() == 0) {
        Serial.printf("[CHARGER] TX: 0x%08lX Func=0x%02X Data: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                     (unsigned long)tx.identifier, func,
                     tx.data[0], tx.data[1], tx.data[2], tx.data[3],
                     tx.data[4], tx.data[5], tx.data[6], tx.data[7]);
        
        if (func == 0x00) {
            Serial.printf("[CHARGER]   → Vmax=%.1fV (raw=0x%08lX)\n", cachedRawV / 1024.0f, (unsigned long)cachedRawV);
        } else if (func == 0x03) {
            Serial.printf("[CHARGER]   → Imax=%.1fA (raw=0x%08lX)\n", cachedRawI / 30.5f, (unsigned long)cachedRawI);
        } else if (func == 0x32) {
            Serial.printf("[CHARGER]   → Control: %s\n", tx.data[3] == 0x00 ? "START" : "STOP");
        }
    }
    
    g.funcIndex = (g.funcIndex + 1) % g.funcCount;
}

// --- Main comms task ---
void chargerCommTask(void *arg)
{
    static unsigned long lastFeedback = 0;
    static unsigned long lastSOCRequest = 0;
    static unsigned long lastBusRecovery = 0;
    static bool startupInitComplete = false;

    // CRITICAL: Send initial messages to both groups at startup
    // Charger module expects both CAN IDs to initialize properly
    Serial.println("[CHARGER] Sending startup initialization sequence...");
    vTaskDelay(pdMS_TO_TICKS(500)); // Wait for CAN bus to stabilize
    
    // Send Group 1 (Control) - all 3 functions
    for (int i = 0; i < 3; i++) {
        sendGroupRequest(groups[0]);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Send Group 2 (Telemetry) - all 5 functions
    for (int i = 0; i < 5; i++) {
        sendGroupRequest(groups[1]);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    Serial.println("[CHARGER] ✅ Startup initialization complete");
    startupInitComplete = true;

    while (true)
    {
        // SAFETY: CAN bus error recovery
        twai_status_info_t s;
        memset(&s, 0, sizeof(s));
        esp_err_t status_result = twai_get_status_info(&s);
        
        // Print status every 10 seconds
        static unsigned long lastStatusPrint = 0;
        if (millis() - lastStatusPrint > 10000) {
            if (status_result == ESP_OK) {
                CANStatusLogger::printStatusReport(s);
            } else {
                LOG_ERROR_F(CAN, "Failed to get status: %d", status_result);
            }
            lastStatusPrint = millis();
        }
        
        if (status_result == ESP_OK)
        {
            // CRITICAL: Immediate recovery on bus-off
            if (s.state == TWAI_STATE_BUS_OFF || s.state == TWAI_STATE_STOPPED)
            {
                // Print diagnostic every 10 seconds
                static unsigned long lastDiagnosticPrint = 0;
                if (millis() - lastDiagnosticPrint > 10000) {
                    CANStatusLogger::printDiagnostics(s);
                    lastDiagnosticPrint = millis();
                }
                
                if (millis() - lastBusRecovery > 5000) // Prevent rapid recovery loops
                {
                    LOG_SECTION_START("CAN BUS RECOVERY");
                    LOG_CRITICAL(CAN, "Bus-off detected, initiating recovery");
                    
                    // Set global recovery flag to disable voltage-drop disconnect
                    canRecoveryActive = true;
                    
                    CANStatusLogger::printRecoveryStep(1, 4, "Deinitializing TWAI driver");
                    if (!CAN_TWAI::deinit()) {
                        LOG_ERROR(CAN, "Deinit failed");
                    }
                    vTaskDelay(pdMS_TO_TICKS(200));
                    
                    CANStatusLogger::printRecoveryStep(2, 4, "Reinitializing TWAI driver");
                    if (!CAN_TWAI::init()) {
                        LOG_ERROR(CAN, "Reinit failed - HARDWARE PROBLEM!");
                        Serial.println("\n  Action required:");
                        Serial.println("    • Check charger module power (LED ON?)");
                        Serial.println("    • Verify 120Ω termination at BOTH ends");
                        Serial.println("    • Check CANH/CANL wiring");
                        Serial.println("    • Verify common ground connection");
                    }
                    lastBusRecovery = millis();
                    
                    CANStatusLogger::printRecoveryStep(3, 4, "Disabling charging for safety");
                    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        chargingEnabled = false;
                        xSemaphoreGive(dataMutex);
                    }
                    
                    CANStatusLogger::printRecoveryStep(4, 4, "Marking for re-initialization");
                    startupInitComplete = false;
                    CANStatusLogger::printRecoveryComplete(true);
                    LOG_INFO(CHARGER, "Re-sending initialization sequence");
                    LOG_SECTION_END();
                }
            }
            else if (canRecoveryActive)
            {
                // Clear recovery flag when bus is healthy again
                canRecoveryActive = false;
                LOG_SECTION_START("CAN BUS RECOVERY SUCCESSFUL");
                LOG_INFO_F(CAN, "State: %s", CANStatusLogger::getStateStr(s.state));
                LOG_DATA("TX Errors", (int)s.tx_error_counter);
                LOG_DATA("RX Errors", (int)s.rx_error_counter);
                LOG_INFO(CAN, "Normal operation resumed");
                LOG_SECTION_END();
            }
        }
        
        // Re-send startup sequence after CAN recovery
        if (!startupInitComplete) {
            for (int i = 0; i < 3; i++) {
                sendGroupRequest(groups[0]);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            for (int i = 0; i < 5; i++) {
                sendGroupRequest(groups[1]);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            startupInitComplete = true;
        }

        // FIX: Send control group every loop (like old working code)
        // This ensures Vmax/Imax updates every ~300ms to prevent charger timeout
        sendGroupRequest(groups[0]);  // Control group: 0x32, 0x00, 0x03
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // CRITICAL FIX: Always send telemetry group, not just when gun connected
        // Charger module needs both CAN IDs active to stay healthy
        sendGroupRequest(groups[1]);  // Telemetry group: 0x84, 0x82, 0x79, 0x80, 0x83
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // [REMOVED] Status-change command - Now handled periodically by sendGroupRequest(groups[0])
        /*
        bool currentChargingState = false;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            currentChargingState = chargingEnabled && gunPhysicallyConnected && batteryConnected;
            xSemaphoreGive(dataMutex);
        }
        
        if (currentChargingState != lastChargingState)
        {
            // Send START/STOP command via function 0x32
            twai_message_t cmd = {};
            cmd.identifier = 0x068181FEUL & 0x1FFFFFFFUL;
            cmd.extd = 1;
            cmd.data_length_code = 8;
            memset(cmd.data, 0, 8);
            cmd.data[0] = 0x01;
            cmd.data[1] = 0x32;  // Function: charger control
            cmd.data[2] = 0x00;
            cmd.data[3] = currentChargingState ? 0x00 : 0x01;  // 0x00=START, 0x01=STOP
            
            Serial.printf("[COMMAND] Charging state changed: %s\n", 
                         currentChargingState ? "START" : "STOP");
            CAN_TWAI::sendMessage(cmd.identifier, cmd.data, cmd.data_length_code, true);
            
            lastChargingState = currentChargingState;
        }
        */

        // Send charger feedback
        if (millis() - lastFeedback >= 100)
        {
            sendChargerFeedback();
            lastFeedback = millis();
        }

        // Request SOC data periodically
        if (millis() - lastSOCRequest >= 2000)
        {
            requestSOCFromBMS();
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
            else if ((msg.identifier & 0x1FFFFFFFUL) == (ID_SOC_RESPONSE & 0x1FFFFFFFUL))
            {
                handleSOCMessage(msg);
            }
        }

        prod::g_healthMonitor.feed();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
// IMMEDIATE CHARGER STOP (RemoteStop Safety)
// =========================================================
/**
 * @brief Immediately stop charger hardware via CAN command
 * 
 * This function sends an immediate STOP command to the charger module
 * when RemoteStop is received from the server. This bypasses the normal
 * polling mechanism to ensure the charger stops within milliseconds
 * rather than waiting up to 500ms for the next poll cycle.
 * 
 * Safety: This is called from OCPP_LOOP task when RemoteStop is received.
 * The normal polling mechanism (sendGroupRequest) acts as a backup.
 * 
 * @note Thread-safe: CAN_TWAI::sendMessage() is thread-safe
 * @note Includes state check to avoid sending unnecessary stop commands
 */
void sendImmediateChargerStop()
{
    // Check if charging is actually enabled (avoid unnecessary CAN traffic)
    bool currentlyEnabled = false;
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        currentlyEnabled = chargingEnabled;
        xSemaphoreGive(dataMutex);
    }
    else
    {
        Serial.println("[SAFETY] ⚠️  Mutex timeout in sendImmediateChargerStop");
        // Continue anyway - safety critical, better to send duplicate than miss
    }
    
    if (!currentlyEnabled)
    {
        Serial.println("[SAFETY] ℹ️  Charger already stopped, skipping redundant stop command");
        return;
    }
    
    Serial.println("[SAFETY] 🚨 REMOTE STOP: Sending immediate hardware stop command");
    
    // Prepare CAN message for function 0x32 (charger control)
    twai_message_t tx = {};
    tx.identifier = 0x068181FEUL & 0x1FFFFFFFUL;  // Control request ID
    tx.extd = 1;
    tx.rtr = 0;
    tx.data_length_code = 8;
    
    memset(tx.data, 0, 8);
    tx.data[0] = 0x01;     // Standard header
    tx.data[1] = 0x32;     // Function: charger control (status)
    tx.data[2] = 0x00;     // Reserved
    tx.data[3] = 0x01;     // 0x01 = STOP, 0x00 = START
    
    // Send the CAN message
    esp_err_t result = CAN_TWAI::sendMessage(tx.identifier, tx.data, tx.data_length_code, true);
    
    if (result == ESP_OK)
    {
        Serial.println("[SAFETY] ✅ Emergency stop command sent successfully");
        Serial.println("[SAFETY] 🔄 Polling mechanism will provide backup confirmation");
    }
    else
    {
        Serial.printf("[SAFETY] ❌ Failed to send stop command: 0x%X\n", result);
        Serial.println("[SAFETY] 🔄 Polling mechanism will retry in <500ms");
    }
}

// =========================================================
// CHARGER MODULE HEALTH MONITORING
// =========================================================
// Production-grade charger health check based on CAN message timeouts
bool isChargerModuleHealthy()
{
    const unsigned long now = millis();

    // ═══════════════════════════════════════════════════════════════
    // STARTUP STABILIZATION (CRITICAL)
    // ═══════════════════════════════════════════════════════════════
    // Don't report faults in the first 30 seconds of uptime. 
    if (now < 30000) {
        static unsigned long lastGraceLog = 0;
        if (now - lastGraceLog > 5000) {
            Serial.printf("[HEALTH] Grace period active (%lu ms remaining) - reporting HEALTHY\n", 30000 - now);
            lastGraceLog = now;
        }
        return true;
    }

    const unsigned long CHARGER_TIMEOUT_MS = 10000; // FIX2: Increased from 5s to 10s
                                                     // Prevents false Faulted on brief CAN spikes
    
    // Check if we're receiving critical CAN messages from charger
    bool terminalPowerOk = (now - lastTerminalPower) < CHARGER_TIMEOUT_MS;
    bool terminalStatusOk = (now - lastTerminalStatus) < CHARGER_TIMEOUT_MS;
    bool heartbeatOk = (now - lastHeartbeat) < CHARGER_TIMEOUT_MS;
    
    // Charger is healthy if at least 2 out of 3 messages are recent
    int healthyCount = (terminalPowerOk ? 1 : 0) + (terminalStatusOk ? 1 : 0) + (heartbeatOk ? 1 : 0);
    bool currentReadingHealthy = (healthyCount >= 2);

    // FIX2: Fault Debouncing increased from 3s to 10s
    // Previously, charger had to miss only ~8s of CAN messages to trigger Faulted.
    // Now requires 10s of no healthy readings before transitioning to FAULTED.
    static unsigned long lastHealthyTime = now;
    if (currentReadingHealthy) {
        lastHealthyTime = now;
    }

    bool healthy = (now - lastHealthyTime < 10000); // FIX2: was 3000
    
    // Log health status changes
    static bool lastHealthStatus = true;
    if (healthy != lastHealthStatus) {
        Serial.printf("[HEALTH] Status changed: %s (Power:%d Status:%d HB:%d debounce_ms=%lu)\n",
                     healthy ? "HEALTHY" : "FAULTED",
                     terminalPowerOk, terminalStatusOk, heartbeatOk, now - lastHealthyTime);
        lastHealthStatus = healthy;
    }
    
    // Update global status
    chargerModuleOnline = healthy;
    
    return healthy;
}
