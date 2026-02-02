#pragma once

#include <Arduino.h>
#include "debug_monitor.h"
#include "diagnostics.h"

namespace DebugCommands {

void printHelp() {
    Serial.println("\n╔════════════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                          DEBUG COMMAND MENU                                ║");
    Serial.println("╠════════════════════════════════════════════════════════════════════════════╣");
    Serial.println("║ SYSTEM COMMANDS                                                            ║");
    Serial.println("║   h - Show this help menu                                                  ║");
    Serial.println("║   s - Show full system status                                              ║");
    Serial.println("║   m - Show memory statistics                                               ║");
    Serial.println("║   r - Reboot system                                                        ║");
    Serial.println("║                                                                            ║");
    Serial.println("║ TRANSACTION COMMANDS                                                       ║");
    Serial.println("║   t - Show transaction gate status                                         ║");
    Serial.println("║   b - Begin transaction (manual)                                           ║");
    Serial.println("║   e - End transaction (manual)                                             ║");
    Serial.println("║                                                                            ║");
    Serial.println("║ HARDWARE COMMANDS                                                          ║");
    Serial.println("║   v - Show voltage/current/power                                           ║");
    Serial.println("║   c - Show CAN bus status                                                  ║");
    Serial.println("║   p - Toggle plug status (simulate)                                        ║");
    Serial.println("║                                                                            ║");
    Serial.println("║ OCPP COMMANDS                                                              ║");
    Serial.println("║   o - Show OCPP connection status                                          ║");
    Serial.println("║   a - Send Authorize request                                               ║");
    Serial.println("║   n - Send StatusNotification                                              ║");
    Serial.println("║                                                                            ║");
    Serial.println("║ LOG LEVEL COMMANDS                                                         ║");
    Serial.println("║   0 - Set log level: DEBUG                                                 ║");
    Serial.println("║   1 - Set log level: INFO                                                  ║");
    Serial.println("║   2 - Set log level: WARN                                                  ║");
    Serial.println("║   3 - Set log level: ERROR                                                 ║");
    Serial.println("║                                                                            ║");
    Serial.println("║ DISPLAY COMMANDS                                                           ║");
    Serial.println("║   d - Toggle dashboard auto-display                                        ║");
    Serial.println("║   + - Increase update interval                                             ║");
    Serial.println("║   - - Decrease update interval                                             ║");
    Serial.println("╚════════════════════════════════════════════════════════════════════════════╝");
}

void printBanner() {
    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                                                                            ║");
    Serial.println("║              ⚡ ESP32 OCPP EVSE CONTROLLER - DEBUG MODE ⚡                ║");
    Serial.println("║                                                                            ║");
    Serial.println("║                        Rivot Motors - v2.5.0                               ║");
    Serial.println("║                     Production Debug Edition                               ║");
    Serial.println("║                                                                            ║");
    Serial.println("╚════════════════════════════════════════════════════════════════════════════╝");
    Serial.println("\n  Type 'h' for help menu\n");
}

// Command processor
class CommandProcessor {
private:
    static bool autoDashboard;
    static uint32_t updateInterval;
    static unsigned long lastUpdate;

public:
    static void init() {
        autoDashboard = true;
        updateInterval = 10000; // 10 seconds
        lastUpdate = 0;
    }

    static void process(char cmd) {
        switch (cmd) {
            case 'h':
            case 'H':
                printHelp();
                break;

            case 's':
            case 'S':
                LOG_I("CMD", "Displaying full system status");
                // Call full dashboard
                break;

            case 'm':
            case 'M':
                LOG_I("CMD", "Memory statistics");
                Diagnostics::printMemoryStats();
                break;

            case 'r':
            case 'R':
                LOG_W("CMD", "Rebooting system in 3 seconds...");
                delay(3000);
                ESP.restart();
                break;

            case 't':
            case 'T':
                LOG_I("CMD", "Transaction gate status");
                // Print gate status
                break;

            case 'v':
            case 'V':
                LOG_I("CMD", "Hardware metrics");
                // Print hardware status
                break;

            case 'c':
            case 'C':
                LOG_I("CMD", "CAN bus status");
                // Print CAN status
                break;

            case 'o':
            case 'O':
                LOG_I("CMD", "OCPP connection status");
                // Print OCPP status
                break;

            case '0':
                Debug.setLevel(LOG_DEBUG);
                LOG_I("CMD", "Log level set to DEBUG");
                break;

            case '1':
                Debug.setLevel(LOG_INFO);
                LOG_I("CMD", "Log level set to INFO");
                break;

            case '2':
                Debug.setLevel(LOG_WARN);
                LOG_I("CMD", "Log level set to WARN");
                break;

            case '3':
                Debug.setLevel(LOG_ERROR);
                LOG_I("CMD", "Log level set to ERROR");
                break;

            case 'd':
            case 'D':
                autoDashboard = !autoDashboard;
                LOG_I("CMD", "Auto-dashboard: %s", autoDashboard ? "ENABLED" : "DISABLED");
                break;

            case '+':
                updateInterval += 5000;
                LOG_I("CMD", "Update interval: %u ms", updateInterval);
                break;

            case '-':
                if (updateInterval > 5000) {
                    updateInterval -= 5000;
                    LOG_I("CMD", "Update interval: %u ms", updateInterval);
                }
                break;

            default:
                if (cmd >= 32 && cmd <= 126) { // Printable character
                    LOG_W("CMD", "Unknown command: '%c' (type 'h' for help)", cmd);
                }
                break;
        }
    }

    static bool shouldUpdateDashboard() {
        if (!autoDashboard) return false;
        if (millis() - lastUpdate >= updateInterval) {
            lastUpdate = millis();
            return true;
        }
        return false;
    }

    static uint32_t getUpdateInterval() { return updateInterval; }
    static bool isAutoDashboardEnabled() { return autoDashboard; }
};

bool CommandProcessor::autoDashboard = true;
uint32_t CommandProcessor::updateInterval = 10000;
unsigned long CommandProcessor::lastUpdate = 0;

} // namespace DebugCommands
