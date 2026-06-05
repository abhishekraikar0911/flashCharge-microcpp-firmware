#pragma once

#include <Arduino.h>

// Debug sections
enum DebugSection {
    DBG_BMS = 0,      // BMS <--> MCU communication
    DBG_CHARGER = 1,  // MCU <--> Charger Module
    DBG_OCPP = 2,     // OCPP Client (MicroOcpp)
    DBG_WIFI = 3,     // WiFi connection
    DBG_STATE = 4,    // State machine
    DBG_SYSTEM = 5    // System/General
};

class DebugLogger {
private:
    static bool sectionEnabled[6];
    static int activeSection; // -1=none, 0-5=specific, 6=all
    
public:
    static void init() {
        activeSection = -1; // Start with no debug
        for (int i = 0; i < 6; i++) {
            sectionEnabled[i] = false;
        }
    }
    
    static void setActiveSection(int section) {
        // section: 0=all, 1-6=specific section
        activeSection = section;
        
        if (section == 0) {
            // Enable all
            for (int i = 0; i < 6; i++) sectionEnabled[i] = true;
        } else if (section >= 1 && section <= 6) {
            // Enable only selected section
            for (int i = 0; i < 6; i++) sectionEnabled[i] = false;
            sectionEnabled[section - 1] = true;
        } else {
            // Disable all
            for (int i = 0; i < 6; i++) sectionEnabled[i] = false;
        }
    }
    
    static const char* getSectionName(DebugSection section) {
        switch (section) {
            case DBG_BMS: return "BMS";
            case DBG_CHARGER: return "CHARGER";
            case DBG_OCPP: return "OCPP";
            case DBG_WIFI: return "WIFI";
            case DBG_STATE: return "STATE";
            case DBG_SYSTEM: return "SYSTEM";
            default: return "UNKNOWN";
        }
    }
    
    template<typename... Args>
    static void log(DebugSection section, const char* format, Args... args) {
        if (!sectionEnabled[section]) return;
        
        Serial.printf("[%s] ", getSectionName(section));
        Serial.printf(format, args...);
        Serial.println();
    }
    
    static void printMenu() {
        Serial.println("\n========== DEBUG MENU ==========");
        Serial.println("1 - BMS <--> MCU");
        Serial.println("2 - MCU <--> Charger Module");
        Serial.println("3 - OCPP Client");
        Serial.println("4 - WiFi");
        Serial.println("5 - State Machine");
        Serial.println("6 - System");
        Serial.println("0 - Debug ALL");
        Serial.println("9 - Stop Debug");
        Serial.println("s - Local START session");
        Serial.println("t - Local STOP session");
        Serial.println("--------------------------------");
        Serial.println("i - Show current NVS identity");
        Serial.println("r - Re-provision (change Charger ID)");
        Serial.println("================================\n");
    }
    
    static int getActiveSection() { return activeSection; }
};

// Convenience macros
#define LOG_BMS(...) DebugLogger::log(DBG_BMS, __VA_ARGS__)
#define LOG_CHARGER(...) DebugLogger::log(DBG_CHARGER, __VA_ARGS__)
#define LOG_OCPP(...) DebugLogger::log(DBG_OCPP, __VA_ARGS__)
#define LOG_WIFI(...) DebugLogger::log(DBG_WIFI, __VA_ARGS__)
#define LOG_STATE(...) DebugLogger::log(DBG_STATE, __VA_ARGS__)
#define LOG_SYSTEM(...) DebugLogger::log(DBG_SYSTEM, __VA_ARGS__)
