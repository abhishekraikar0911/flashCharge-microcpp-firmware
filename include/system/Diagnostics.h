#pragma once

#include <Arduino.h>

namespace Diagnostics {

// System Status Display
void printSystemStatus() {
    Serial.println("\n╔════════════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                          SYSTEM STATUS DASHBOARD                           ║");
    Serial.println("╚════════════════════════════════════════════════════════════════════════════╝");
}

// Transaction Gate Status
void printGateStatus(bool txActive, int txId, bool remoteStart) {
    Serial.println("\n┌─ TRANSACTION GATE ─────────────────────────────────────────────────────────┐");
    Serial.printf("│ Active: %-5s │ TxID: %-6d │ RemoteStart: %-5s │ Status: %s\n",
        txActive ? "TRUE" : "FALSE",
        txId,
        remoteStart ? "TRUE" : "FALSE",
        (txActive && txId > 0 && remoteStart) ? "🟢 OPEN" : "🔴 CLOSED");
    Serial.println("└────────────────────────────────────────────────────────────────────────────┘");
}

// Hardware Status
void printHardwareStatus(float volt, float curr, float temp, float soc, float range) {
    Serial.println("\n┌─ HARDWARE METRICS ─────────────────────────────────────────────────────────┐");
    Serial.printf("│ Voltage: %6.2fV │ Current: %6.2fA │ Power: %7.2fW │ Temp: %5.1f°C │\n",
        volt, curr, volt * curr, temp);
    Serial.printf("│ SOC: %6.1f%%   │ Range: %7.1fkm │ Energy: %7.2fWh │              │\n",
        soc, range, 0.0f);
    Serial.println("└────────────────────────────────────────────────────────────────────────────┘");
}

// OCPP Status
void printOCPPStatus(bool connected, const char* state, bool txActive, bool txRunning) {
    Serial.println("\n┌─ OCPP STATUS ──────────────────────────────────────────────────────────────┐");
    Serial.printf("│ Connection: %-10s │ State: %-12s │ TX: %-6s │ Running: %-5s │\n",
        connected ? "🟢 ONLINE" : "🔴 OFFLINE",
        state,
        txActive ? "ACTIVE" : "IDLE",
        txRunning ? "YES" : "NO");
    Serial.println("└────────────────────────────────────────────────────────────────────────────┘");
}

// CAN Bus Status
void printCANStatus(int state, int txErr, int rxErr, int txQ, int rxQ) {
    Serial.println("\n┌─ CAN BUS STATUS ───────────────────────────────────────────────────────────┐");
    Serial.printf("│ State: %-8s │ TX_Err: %3d │ RX_Err: %3d │ TX_Q: %3d │ RX_Q: %3d │\n",
        (state == 1) ? "RUNNING" : "ERROR",
        txErr, rxErr, txQ, rxQ);
    Serial.println("└────────────────────────────────────────────────────────────────────────────┘");
}

// Compact Status Line (for frequent updates)
void printCompactStatus(uint32_t uptime, bool wifi, bool ocpp, const char* state, 
                       float volt, float curr, float soc, bool charging) {
    Serial.printf("[%6us] WiFi:%s OCPP:%s State:%-10s V:%.1f I:%.1f SOC:%.0f%% Charge:%s\n",
        uptime,
        wifi ? "✓" : "✗",
        ocpp ? "✓" : "✗",
        state,
        volt, curr, soc,
        charging ? "ON " : "OFF");
}

// Error Display
void printError(const char* component, const char* message) {
    Serial.println("\n╔════════════════════════════════════════════════════════════════════════════╗");
    Serial.printf("║ ⚠️  ERROR: %-66s ║\n", component);
    Serial.printf("║ Message: %-68s ║\n", message);
    Serial.println("╚════════════════════════════════════════════════════════════════════════════╝");
}

// Transaction Event
void printTransactionEvent(const char* event, int txId, const char* idTag) {
    Serial.println("\n╔════════════════════════════════════════════════════════════════════════════╗");
    Serial.printf("║ 🔄 TRANSACTION EVENT: %-54s ║\n", event);
    Serial.printf("║ Transaction ID: %-60d ║\n", txId);
    Serial.printf("║ ID Tag: %-68s ║\n", idTag ? idTag : "N/A");
    Serial.println("╚════════════════════════════════════════════════════════════════════════════╝");
}

// Memory Stats
void printMemoryStats() {
    Serial.println("\n┌─ MEMORY USAGE ─────────────────────────────────────────────────────────────┐");
    Serial.printf("│ Free Heap: %7u bytes │ Min Free: %7u bytes │ Largest Block: %7u │\n",
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        ESP.getMaxAllocHeap());
    Serial.println("└────────────────────────────────────────────────────────────────────────────┘");
}

// Full Dashboard (call every 10s)
void printFullDashboard(
    uint32_t uptime, bool wifi, bool ocpp, const char* state,
    float volt, float curr, float temp, float soc, float range, float energy,
    bool txActive, int txId, bool remoteStart, bool charging,
    int canState, int txErr, int rxErr) {
    
    Serial.println("\n\n");
    printSystemStatus();
    
    Serial.printf("\n⏱️  Uptime: %us | 📡 WiFi: %s | 🔌 OCPP: %s | 🔋 State: %s\n",
        uptime,
        wifi ? "✅" : "❌",
        ocpp ? "✅" : "❌",
        state);
    
    printGateStatus(txActive, txId, remoteStart);
    printHardwareStatus(volt, curr, temp, soc, range);
    printOCPPStatus(ocpp, state, txActive, charging);
    printCANStatus(canState, txErr, rxErr, 0, 0);
    printMemoryStats();
    
    Serial.println("\n");
}

} // namespace Diagnostics
