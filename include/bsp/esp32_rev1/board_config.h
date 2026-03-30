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

// CAN2 - MCP2515 (SPI) - Vehicle BMS
#define GPIO_SPI_CS     26
#define GPIO_SPI_SCK    14
#define GPIO_SPI_MOSI   27
#define GPIO_SPI_MISO   25
#define GPIO_CAN2_INT   34
#define CAN2_BAUDRATE   250000

// ========== GSM MODEM CONFIGURATION ==========
#define GPIO_GSM_TX     17
#define GPIO_GSM_RX     16
#define GPIO_GSM_RESET  23

// ========== LED AND INTERFACE CONFIGURATION ==========
#define GPIO_LED_CHARGER 13
#define GPIO_LED_NETWORK 15
#define GPIO_BTN_ESTOP   32
#define GPIO_BTN_REBOOT  35

// ========== CONTACTOR & SENSORS ==========
#define GPIO_RELAY_PIN      4     // Example pin for contactor
#define GPIO_NTC_ADC_PIN    33    // Example pin for thermistor
