/**
 * @file test/stubs/Arduino.h
 * @brief Minimal Arduino stub for native (PC) unit testing.
 *        Replaces <Arduino.h> so that production headers compile on Windows/Linux.
 */
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <string>

// --- Basic types the ESP32 framework provides ---
using byte = uint8_t;
using String = std::string;

// --- Pin modes / digital levels ---
#define INPUT        0
#define INPUT_PULLUP 2
#define OUTPUT       1
#define LOW          0
#define HIGH         1

// --- Controllable clock: tests set g_mock_millis to simulate time passing ---
extern unsigned long g_mock_millis;
inline unsigned long millis() { return g_mock_millis; }
inline void delay(unsigned long) {}
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t) { return 0; }
inline int  analogRead(uint8_t)  { return 0; }

// --- Minimal Serial stub (prints to stdout so Unity can capture it) ---
struct FakeSerial {
    template<typename T>         void print(T v)             { printf("%s", std::to_string(v).c_str()); }
    void print(const char* s)                                 { fputs(s, stdout); }
    void println(const char* s = "")                          { printf("%s\n", s); }
    template<typename T>         void println(T v)           { printf("%s\n", std::to_string(v).c_str()); }
    template<typename... Args>   void printf(const char* fmt, Args... args) { ::printf(fmt, args...); }
};
extern FakeSerial Serial;
