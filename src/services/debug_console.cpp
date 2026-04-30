#include "system/DebugLogger.h"
#include "services/TransactionService.h"
#include <Arduino.h>
#include <WiFi.h>

// Define static members
bool DebugLogger::sectionEnabled[6] = {false, false, false, false, false, false};
int DebugLogger::activeSection = -1;

void processDebugCommand(char c) {
    // 🔍 VERBOSE DIAGNOSTIC: Log EXACTLY what we received
    if (c >= 32 && c <= 126) {
        Serial.printf("[SERIAL_IN] Char='%c' (0x%02X)\n", c, (uint8_t)c);
    } else {
        Serial.printf("[SERIAL_IN] Non-printable (0x%02X)\n", (uint8_t)c);
    }

    if (c >= '0' && c <= '9') {
        Serial.printf("[DEBUG] Command: Digit '%c'\n", c);
    }
    
    if (c >= '0' && c <= '6') {
        int section = c - '0';
        DebugLogger::setActiveSection(section);
        
        if (section == 0) {
            Serial.println("\n✅ Debug ALL sections ENABLED\n");
        } else if (section >= 1 && section <= 6) {
            const char* names[] = {"BMS <--> MCU", "MCU <--> Charger", "OCPP Client", "WiFi", "State Machine", "System"};
            Serial.printf("\n✅ Debug: %s ENABLED\n\n", names[section - 1]);
        }
    } else if (c == '9') {
        DebugLogger::setActiveSection(-1);
        Serial.println("\n❌ Debug STOPPED\n");
    } else if (c == 'd' || c == 'D') {
        Serial.println("[HAL] MCP2515 diagnostics disabled (Refactoring)");
    } else if (c == 'h' || c == 'H' || c == '?') {
        DebugLogger::printMenu();
    } else if (c == 's' || c == 'S') {
        Serial.println("\n[CONSOLE] 🔌 Local START requested (Unified Trigger)");
        prod::g_transactionManager.startLocalTransaction("LOCAL_ADMIN_1"); // see hardware.h LOCAL_START_ID_TAG
    } else if (c == 't' || c == 'T') {
        Serial.println("\n[CONSOLE] 🛑 Local STOP requested (Unified Trigger)");
        prod::g_transactionManager.stopLocalTransaction();
    }
}
