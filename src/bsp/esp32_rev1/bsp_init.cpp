/**
 * @file bsp_init.cpp
 * @brief ESP32 Rev1.1 Board Support Package — Initialization
 * @layer BSP
 *
 * This is the ONLY file that:
 *   1. Knows about physical pin numbers (includes board_config.h)
 *   2. Creates concrete HAL objects (Esp32Gpio, Esp32Can, etc.)
 *   3. Creates concrete device driver objects (CM1ChargerDriver, DalyBmsDriver, etc.)
 *   4. Wires them together via dependency injection
 *   5. Registers all pointers into the global AppContext (g_app)
 *
 * All objects are static (no heap allocation) for deterministic lifetime.
 *
 * @author Rivot Motors
 * @date 2026
 */
#include "bsp/esp32_rev1/bsp_init.h"
#include "bsp/esp32_rev1/board_config.h"
#include "app/AppContext.h"
#include <SPI.h>

// HAL Implementations

#include "hal/esp32/Esp32Gpio.h"
#include "hal/esp32/Esp32Flash.h"
#include "hal/esp32/Esp32Timer.h"
#include "hal/esp32/Esp32Watchdog.h"
#include "hal/esp32/Esp32Uart.h"
#include "hal/esp32/Esp32UartLogger.h"
#include "hal/esp32/Esp32Can.h"
#include "hal/esp32/Esp32MCP2515.h"

// System Layer
#include "system/FlashConfig.h"

// Device Drivers
#include "drivers/SingleRelay.h"
#include "drivers/NtcSensor.h"
#include "drivers/A7670ModemDriver.h"
#include "drivers/DalyBmsDriver.h"
#include "drivers/CM1ChargerDriver.h"

// Board constants (pin numbers, timeouts) from legacy hardware.h
#include "config/hardware.h"

// Global AppContext definition (declared extern in AppContext.h)
AppContext g_app;

// =============================================================================
// STATIC INSTANCES — No heap allocations below this line.
// =============================================================================

// ---- HAL Layer ----
static Esp32Gpio         s_gpio;
static Esp32Flash        s_flash;
static Esp32Timer        s_timer;
static Esp32Watchdog     s_wdt;
static Esp32UartLogger   s_logger;

// UART for GSM Modem (Serial2) — constructor: (uartNum, txPin, rxPin)
static Esp32Uart         s_modemUart(2, GSM_TX_PIN, GSM_RX_PIN);

// CAN1: Internal TWAI (Charger Module) — (txPin, rxPin, rxQueueSize, txQueueSize)
static Esp32Can          s_can1(CAN1_TX_PIN, CAN1_RX_PIN, CAN_RX_QUEUE_SIZE, CAN_TX_QUEUE_SIZE);

// CAN2: External MCP2515 over SPI (BMS) — (csPin, oscillatorFreq)
static Esp32MCP2515      s_can2(CAN2_CS_PIN, MCP_8MHZ);

// ---- System Layer ----
static FlashConfig       s_config(s_flash);

// ---- Device Drivers ----
static SingleRelay       s_relay(s_gpio, GPIO_RELAY_PIN, true);          // Active HIGH contactor
static NtcSensor         s_tempSensor(s_gpio, GPIO_NTC_ADC_PIN);         // NTC thermistor
static A7670ModemDriver  s_modem(s_modemUart, s_gpio, s_timer, GSM_RESET_PIN);
static DalyBmsDriver     s_bms(s_can2, s_timer, s_logger);
static CM1ChargerDriver  s_charger(s_can1, s_timer, s_logger);

// =============================================================================
// BSP_Init()
// =============================================================================
bool BSP_Init() {
    // ---- Step 0: Pre-register all static pointers to AppContext ----
    // This prevents NULL pointer errors for services if any init fails.
    g_app.logger     = &s_logger;
    g_app.flash      = &s_flash;
    g_app.config     = &s_config;
    g_app.timer      = &s_timer;
    g_app.wdt        = &s_wdt;
    g_app.charger    = &s_charger;
    g_app.bms        = &s_bms;
    g_app.modem      = &s_modem;
    g_app.relay      = &s_relay;
    g_app.gpio       = &s_gpio;
    g_app.tempSensor = nullptr; // Not populated

    // ---- Step 1: Logger init ----
    s_logger.init(115200);
    s_logger.log(ILogger::Level::INFO, "BSP", "BSP_Init() starting...");

    // ---- Step 2: Flash  ----
    s_flash.open("bsp");

    // ---- Step 3: Watchdog ----
    s_wdt.init(WATCHDOG_TIMEOUT_S * 1000);

    // ---- Step 4: CAN1 (Charger TWAI) ----
    if (!s_can1.init(CAN1_BAUDRATE)) {
        s_logger.log(ILogger::Level::ERROR, "BSP", "CAN1 (TWAI) init failed!");
        return false;
    }

    // ---- Step 5: CAN2 (BMS MCP2515 over SPI) ----
    SPI.begin(CAN2_SCK_PIN, CAN2_MISO_PIN, CAN2_MOSI_PIN, CAN2_CS_PIN);
    delay(10); 

    if (!s_can2.init(CAN2_BAUDRATE)) {
        s_logger.log(ILogger::Level::ERROR, "BSP", "CAN2 (MCP2515) init failed!");
        return false;
    }

    // ---- Step 6: GSM UART ----
    s_modemUart.begin(GSM_BAUD_RATE);

    // ---- Step 7: Relay ----
    s_relay.init();

    // ---- Step 8: Device Drivers base init ----
    s_bms.init();
    s_charger.init();

    s_logger.log(ILogger::Level::INFO, "BSP", "BSP_Init() complete.");
    return true;
}
