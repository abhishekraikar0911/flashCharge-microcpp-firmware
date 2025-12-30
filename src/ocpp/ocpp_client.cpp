// OCPP client bridge: maps existing firmware globals into MicroOCPP inputs
#include <Arduino.h>
#include <WiFi.h>
#include <MicroOcpp.h>
#include <MicroOcpp/Model/Transactions/Transaction.h>

#include "ocpp_client.h"
#include "secrets.h"

// These globals are defined in the main firmware (e.g. src/main.cpp).
// We declare them as extern so this translation unit can read/write them.
extern bool gunPhysicallyConnected;
extern bool chargingEnabled;
extern float energyWh;
extern float chargerVolt;
extern float chargerCurr;
extern bool sessionActive;
extern bool batteryConnected;

static TaskHandle_t ocppTaskHandle = nullptr;

static void ocppTask(void *arg)
{
    // Connect to WiFi
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Construct OCPP URL
    String url = "ws://" + String(SECRET_CSMS_HOST) + ":" + String(SECRET_CSMS_PORT) + SECRET_CSMS_URL;

    // Initialize MicroOCPP
    mocpp_initialize(url.c_str(), "RIVOT_100A_01", "Rivot Charger", "Rivot Motors");

    // Map existing firmware state into MicroOCPP inputs (connector 1)
    setConnectorPluggedInput([]()
                             { return gunPhysicallyConnected; }, 1);
    setEvseReadyInput([]()
                      { return chargingEnabled; }, 1);
    setEnergyMeterInput([]()
                        { return energyWh; }, 1); // in Wh
    setPowerMeterInput([]()
                       { return chargerVolt * chargerCurr; }, 1); // in W

    // Handle remote start/stop by setting chargingEnabled
    setTxNotificationOutput([](MicroOcpp::Transaction *tx, TxNotification notification)
                            {
        if (notification == TxNotification_RemoteStart || notification == TxNotification_StartTx) {
            chargingEnabled = true;
            sessionActive = true;
        } else if (notification == TxNotification_StopTx || notification == TxNotification_RemoteStop) {
            chargingEnabled = false;
            sessionActive = false;
        } }, 1);

    // Main OCPP loop
    while (true)
    {
        mocpp_loop();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void startOCPP()
{
    if (ocppTaskHandle)
        return; // already running

    xTaskCreatePinnedToCore(
        ocppTask,
        "OCPP",
        8192,
        nullptr,
        3,
        &ocppTaskHandle,
        1);
}

void ocpp_sendTelemetry()
{
    // MicroOCPP sends MeterValues automatically from the meter callbacks.
}
