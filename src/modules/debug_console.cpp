#include "header.h"
#include "utils/debug_logger.h"
#include "drivers/can_mcp2515_driver.h"
#include <Arduino.h>

// Define static members
bool DebugLogger::sectionEnabled[6] = {false, false, false, false, false, false};
int DebugLogger::activeSection = -1;

void processDebugCommand(char c) {
    // Echo received character for debugging
    if (c >= '0' && c <= '9') {
        Serial.printf("\n[DEBUG] Received: '%c'\n", c);
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
        CAN_MCP2515::readDiagnostics();
    } else if (c == 'h' || c == 'H' || c == '?') {
        DebugLogger::printMenu();
    }
}
