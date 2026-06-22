/**
 * @file board_config.h
 * @brief Physical hardware pin mapping for ESP32 Rev1.1 PCB
 * @layer BSP
 *
 * These pin constants should ONLY be included by the BSP layer.
 * No driver or application file should hardcode or include these!
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once

// ========== CAN BUS CONFIGURATION ==========
// CAN1 - ISO1050 (TWAI) - Charger Module
#define GPIO_CAN1_TX    21
#define GPIO_CAN1_RX    22
#define CAN1_BAUDRATE   250000

// CAN2 - MCP2515 (SPI) - Vehicle BMS (Aligned with hardware.h)
#define GPIO_SPI_CS     5
#define GPIO_SPI_SCK    18
#define GPIO_SPI_MOSI   23
#define GPIO_SPI_MISO   19
#define GPIO_CAN2_INT   34
#define CAN2_BAUDRATE   250000

// ========== GSM MODEM CONFIGURATION ==========
#define GPIO_GSM_TX     17
#define GPIO_GSM_RX     16
#define GPIO_GSM_RESET  27

// ========== LED AND INTERFACE CONFIGURATION ==========
#define GPIO_LED_CHARGER 4     // Charging (Reverted to fixed hardware pin)
#define GPIO_LED_NETWORK 15    // Server Connection
#define GPIO_LED_FAULT   13    // Fault Detection

// #define GPIO_BTN_ESTOP   32    // Unused (Freed for I2C)
// #define GPIO_BTN_REBOOT  35    // Unused
#define GPIO_BTN_START   33    // Confirmed
#define GPIO_BTN_STOP    26    // Confirmed

// ========== RFID / NFC (PN532) ==========
//#define GPIO_I2C_SDA        0     // Safe for I2C pull-ups    (for RFID Integration)
//#define GPIO_I2C_SCL        32    // Safe for I2C pull-ups (Freed from ESTOP)
// #define GPIO_NTC_ADC_PIN    39 // Unused
