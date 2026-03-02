#include "../../include/utils/log_formatter.h"
#include <stdarg.h>

namespace LogFormatter {

const char* getCategoryStr(Category cat) {
    switch(cat) {
        case SYSTEM:  return "SYS";
        case CAN:     return "CAN";
        case OCPP:    return "OCPP";
        case WIFI:    return "WIFI";
        case BMS:     return "BMS";
        case CHARGER: return "CHRG";
        case SAFETY:  return "SAFE";
        case HEALTH:  return "HLTH";
        default:      return "????";
    }
}

const char* getLevelIcon(Level level) {
    switch(level) {
        case DEBUG:    return "🔍";
        case INFO:     return "ℹ️ ";
        case WARN:     return "⚠️ ";
        case ERROR:    return "❌";
        case CRITICAL: return "🚨";
        default:       return "  ";
    }
}

void print(Category cat, Level level, const char* msg) {
    Serial.printf("[%s] %s %s\n", getCategoryStr(cat), getLevelIcon(level), msg);
}

void printf(Category cat, Level level, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Serial.printf("[%s] %s %s\n", getCategoryStr(cat), getLevelIcon(level), buffer);
}

void printSectionHeader(const char* title) {
    Serial.println("\n╔═══════════════════════════════════════════════════════════════╗");
    Serial.printf("║  %-59s  ║\n", title);
    Serial.println("╚═══════════════════════════════════════════════════════════════╝");
}

void printSectionFooter() {
    Serial.println("╚═══════════════════════════════════════════════════════════════╝\n");
}

void printStatusBox(const char* title, const char** items, int count) {
    Serial.println("\n┌───────────────────────────────────────────────────────────────┐");
    Serial.printf("│  %-59s  │\n", title);
    Serial.println("├───────────────────────────────────────────────────────────────┤");
    for(int i = 0; i < count; i++) {
        Serial.printf("│  %-59s  │\n", items[i]);
    }
    Serial.println("└───────────────────────────────────────────────────────────────┘\n");
}

void printDataRow(const char* label, const char* value) {
    Serial.printf("  %-25s : %s\n", label, value);
}

void printDataRow(const char* label, float value, const char* unit) {
    if(strlen(unit) > 0) {
        Serial.printf("  %-25s : %.2f %s\n", label, value, unit);
    } else {
        Serial.printf("  %-25s : %.2f\n", label, value);
    }
}

void printDataRow(const char* label, int value) {
    Serial.printf("  %-25s : %d\n", label, value);
}

void printDataRow(const char* label, unsigned int value) {
    Serial.printf("  %-25s : %u\n", label, value);
}

void printDataRow(const char* label, long value) {
    Serial.printf("  %-25s : %ld\n", label, value);
}

void printDataRow(const char* label, unsigned long value) {
    Serial.printf("  %-25s : %lu\n", label, value);
}

void printSeparator() {
    Serial.println("───────────────────────────────────────────────────────────────");
}

void printDoubleSeparator() {
    Serial.println("═══════════════════════════════════════════════════════════════");
}

} // namespace LogFormatter
