#include "header.h"
#include <Arduino.h>
#include "ocpp/ocpp_client.h" // <-- re‑enable OCPP include

SemaphoreHandle_t dataMutex;

void setup()
{
    Serial.begin(115200);
    delay(100);

    // Initialize mutex for shared data protection
    dataMutex = xSemaphoreCreateMutex();
    if (dataMutex == NULL)
    {
        Serial.println("❌ Error: Could not create Mutex");
        while (1)
        {
            delay(1000);
        }
    }

    // Initialize CAN/TWAI
    twai_init();

    // Start tasks: RX on core 0 (I/O heavy), charger comm on core 1
    xTaskCreatePinnedToCore(can_rx_task, "CAN_RX", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(chargerCommTask, "ChargerComm", 6144, NULL, 10, NULL, 1);

    // Show operator menu immediately
    printMenu();

    // ✅ Start OCPP client task (network + telemetry)
    startOCPP();
}

void loop()
{
    // Always poll operator input for model confirmation and menu selection
    processSerialInput();

    // Periodic UI/data printing (1 Hz), regardless of chargingEnabled
    static unsigned long lastDisplay = 0;
    if (millis() - lastDisplay >= 1000)
    {
        printDecodedData();
        lastDisplay = millis();
    }

    // Yield time to FreeRTOS tasks
    vTaskDelay(pdMS_TO_TICKS(10));
}