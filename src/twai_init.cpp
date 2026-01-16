#include "header.h"
#include <Arduino.h>
#include <driver/twai.h>
#include <string.h>

// ====== Global State Variables (Definitions) ======

// 0. Synchronization
SemaphoreHandle_t dataMutex = nullptr;
SemaphoreHandle_t serialMutex = nullptr;

// 1. Energy & Time
float energyWh = 0.0f;
unsigned long lastHeartbeat = 0;

// 2. System Flags
bool batteryConnected = false;
bool chargingEnabled = false;
bool chargingswitch = false;

// OCPP initialization status
bool ocppInitialized = false;

// 3. Vehicle Detection Globals
String vehicleModel = "Unknown";
bool vehicleConfirmed = false;
bool gunPhysicallyConnected = false;

// 4. BMS & Charger Values
float BMS_Vmax = 0.0f, BMS_Imax = 0.0f;
float Charger_Vmax = 0.0f, Charger_Imax = 0.0f;
float chargerVolt = 0.0f, chargerCurr = 0.0f, chargerTemp = 0.0f, terminalchargerPower = 0.0f;
float terminalVolt = 0.0f, terminalCurr = 0.0f;
float socPercent = 0.0f;
// float batteryAh = 0.0f; // Defined in bms_mcu.cpp
// float batterySoc = 0.0f; // Defined in bms_mcu.cpp

// 5. Metrics
uint16_t metric79_raw = 0;
float metric79_scaled = 0;
uint32_t metric83_raw = 0;
float metric83_scaled = 0;

// 6. Status & Timers
unsigned long lastBMS = 0;
uint8_t heating = 0;

const char *chargerStatus = "UNKNOWN";
const char *terminalchargerStatus = "NO HEARTBEAT";
const char *terminalStatus = "UNKNOWN";

int userChoice = 0;
unsigned long lastPrint = 0;

// 7. Data Buffers
uint8_t stopCmd = 0;
uint8_t lastData[8] = {0}, lastHData[8] = {0};
uint8_t lastBMSData[8] = {0}, lastStatusData[8] = {0}, lastVmaxData[8] = {0}, lastImaxData[8] = {0};
uint8_t lastBattData[8] = {0}, lastVoltData[8] = {0}, lastCurrData[8] = {0}, lastTempData[8] = {0};
uint8_t lastTermData1[8] = {0}, lastTermData2[8] = {0};

uint32_t cachedRawV = 0;
uint32_t cachedRawI = 0;

// CAN Update Flag
volatile bool updateCAN = false;

// Last charger response
unsigned long lastChargerResponse = 0;

// Session lock
bool sessionActive = false;

// ====== Ring Buffer Definitions ======
volatile uint16_t rb_head = 0, rb_tail = 0;
RxBufItem rxRing[64];

// ====== Ring Buffer Helpers ======
bool popFrame(RxBufItem &out)
{
    if (rb_head == rb_tail)
        return false; // empty
    out = rxRing[rb_tail];
    rb_tail = (rb_tail + 1) % 64;
    return true;
}

void pushFrame(const twai_message_t &msg)
{
    RxBufItem item;
    item.id = msg.identifier;
    item.dlc = msg.data_length_code;
    memcpy(item.data, msg.data, 8);
    item.ext = msg.extd;
    item.rtr = msg.rtr;
    rxRing[rb_head] = item;
    rb_head = (rb_head + 1) % 64;
    // Optional: overwrite oldest if buffer full
    if (rb_head == rb_tail)
    {
        rb_tail = (rb_tail + 1) % 64;
    }
}

// ====== TWAI Initialization ======
void twai_init()
{
    // Initialize semaphores if not already done
    if (dataMutex == nullptr)
    {
        dataMutex = xSemaphoreCreateMutex();
    }
    if (serialMutex == nullptr)
    {
        serialMutex = xSemaphoreCreateMutex();
    }

    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        Serial.println("🔧 Initializing CAN bus...");

        // Use TX=21, RX=22 default pins
        twai_general_config_t gcfg = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_21, GPIO_NUM_22, TWAI_MODE_NORMAL);
        gcfg.rx_queue_len = 64;
        gcfg.tx_queue_len = 16;

        twai_timing_config_t tcfg = TWAI_TIMING_CONFIG_250KBITS();
        twai_filter_config_t fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        Serial.println("📡 CAN Config:");
        Serial.println("  - Mode: Normal");
        Serial.println("  - Baud: 250 kbps");
        Serial.println("  - TX Pin: 21, RX Pin: 22");
        Serial.println("  - Filter: Accept All");

        if (twai_driver_install(&gcfg, &tcfg, &fcfg) != ESP_OK)
        {
            Serial.println("❌ TWAI driver install failed");
            xSemaphoreGive(serialMutex);
            while (true)
            {
                delay(1000);
            }
        }

        if (twai_start() != ESP_OK)
        {
            Serial.println("❌ TWAI start failed");
            xSemaphoreGive(serialMutex);
            while (true)
            {
                delay(1000);
            }
        }

        Serial.println("✅ TWAI started at 250 kbps");
        Serial.println("🔍 Monitoring CAN bus health...");
        xSemaphoreGive(serialMutex);
    }

    lastChargerResponse = millis();
}

// ====== RX Task ======
void can_rx_task(void *arg)
{
    twai_message_t rx;
    while (true)
    {
        if (twai_receive(&rx, portMAX_DELAY) == ESP_OK)
        {
            pushFrame(rx);
        }
    }
}