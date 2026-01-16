#include <Arduino.h>
#include <WiFi.h>
#include <MicroOcpp.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Model/Transactions/Transaction.h>
#include "../include/secrets.h"
#include "../include/header.h"
#include "ocpp/ocpp_client.h"

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println("Starting ESP32 OCPP EVSE Controller");

    // Initialize your existing systems (CAN, etc.)
    // This is where your existing setup code goes
    // For example: twai_init(), etc.

    // Initialize CAN bus
    twai_init();

    // Create CAN RX task
    xTaskCreatePinnedToCore(
        can_rx_task,
        "CAN_RX",
        4096,
        nullptr,
        5,
        nullptr,
        1);

    // Create charger communication task
    xTaskCreatePinnedToCore(
        chargerCommTask,
        "CHARGER_COMM",
        4096,
        nullptr,
        4,
        nullptr,
        1);

    // Create UI task for serial menu
    xTaskCreatePinnedToCore(
        [](void *arg)
        {
            static bool menuPrinted = false;
            while (true)
            {
                processSerialInput();
                // Only print menu once at startup, or when user presses a key
                if (!menuPrinted)
                {
                    printMenu();
                    menuPrinted = true;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        },
        "UI_TASK",
        4096,
        nullptr,
        2,
        nullptr,
        1);

    // Connect to WiFi
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    // Start OCPP client
    startOCPP();

    Serial.println("OCPP client started");
}

void loop()
{
    // OCPP runs in separate task

    // Your existing loop logic here
    // This includes CAN processing, UI, etc.

    // Accumulate energy when charging
    static unsigned long lastEnergyTime = millis();
    if (chargingEnabled && chargerVolt > 0 && chargerCurr > 0)
    {
        unsigned long now = millis();
        float dt_hours = (now - lastEnergyTime) / 3600000.0f; // convert ms to hours
        energyWh += chargerVolt * chargerCurr * dt_hours;
        lastEnergyTime = now;
    }
    else
    {
        lastEnergyTime = millis(); // reset timer when not charging
    }

    // Transaction handling is done via OCPP notifications

    // Debug: Print status every 5 seconds
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug >= 5000)
    {
        Serial.printf("DEBUG: gunConnected=%d, batteryConnected=%d, chargingEnabled=%d, vehicleConfirmed=%d\n",
                      gunPhysicallyConnected, batteryConnected, chargingEnabled, vehicleConfirmed);
        Serial.printf("DEBUG: chargerVolt=%.1fV, chargerCurr=%.1fA, terminalVolt=%.1fV, energyWh=%.2f\n",
                      chargerVolt, chargerCurr, terminalVolt, energyWh);
        lastDebug = millis();
    }

    delay(50);
}