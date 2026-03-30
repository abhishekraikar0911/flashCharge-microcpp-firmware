#ifndef LOG_FORMATTER_H
#define LOG_FORMATTER_H

#include <Arduino.h>

/**
 * @file log_formatter.h
 * @brief Structured logging system for clean serial output
 */

namespace LogFormatter {

// Log levels
enum Level {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL
};

// Log categories
enum Category {
    SYSTEM,
    CAN,
    OCPP,
    WIFI,
    BMS,
    CHARGER,
    SAFETY,
    HEALTH
};

// Print formatted log with timestamp
void print(Category cat, Level level, const char* msg);
void printf(Category cat, Level level, const char* format, ...);

// Section headers
void printSectionHeader(const char* title);
void printSectionFooter();

// Status boxes
void printStatusBox(const char* title, const char** items, int count);

// Data tables
void printDataRow(const char* label, const char* value);
void printDataRow(const char* label, float value, const char* unit = "");
void printDataRow(const char* label, int value);
void printDataRow(const char* label, unsigned int value);
void printDataRow(const char* label, long value);
void printDataRow(const char* label, unsigned long value);

// Separators
void printSeparator();
void printDoubleSeparator();

// Get category/level strings
const char* getCategoryStr(Category cat);
const char* getLevelIcon(Level level);

} // namespace LogFormatter

#endif // LOG_FORMATTER_H
