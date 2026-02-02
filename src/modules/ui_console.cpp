#include "header.h"
#include <Arduino.h>
#include <math.h>
#include "esp_err.h" // for esp_err_to_name()
#include <MicroOcpp.h>

// ====== UI States ======
static bool uiInitialized = false;

// ====== Utility: print bytes ======
void printBytes(const uint8_t *data, uint8_t len)
{
    for (int i = 0; i < len; i++)
    {
        Serial.printf("%02X ", data[i]);
    }
    Serial.println();
}

// ====== Startup Screen ======
void printStartupScreen()
{
    Serial.println("\n============================================");
    Serial.println("⚡  WELCOME TO RIVOT FLASH CHARGER  ⚡");
    Serial.println("============================================");
    Serial.println("Initializing...");
}

// ====== Battery Not Connected ======
void printNoBatteryScreen()
{
    Serial.println("\n⛔  No Vehicle Detected");
    Serial.println("👉  Please connect the charging gun to the vehicle...");
}

// ====== Charger Switch OFF ======
void printSwitchOffAlert()
{
    Serial.println("\n⛔  Charger Switch is OFF!");
    Serial.println("👉  Please turn ON the charger switch in the vehicle.");
}

// ====== Menu ======
static unsigned long lastMenuPrint = 0;
void printMenu()
{
    // Throttle menu re-print to avoid spamming Serial
    if (millis() - lastMenuPrint < 2000)
        return;
    lastMenuPrint = millis();

    Serial.println("\n============== MAIN MENU ==============");
    Serial.println("1 → Show BMS Data");
    Serial.println("2 → Show Charger Data");
    Serial.println("3 → Show Output / Temperature");
    Serial.println("4 → Show Terminal Data");
    Serial.println("5 → Show All Data");
    Serial.println("---------------------------------------");
    Serial.println("s → Start Charging");
    Serial.println("t → 🚨 EMERGENCY STOP (immediate)");
    Serial.println("0 → Mute Output");
    Serial.println("=======================================\n");
}

// ====== Charging State ======
void printChargingState(bool enabled)
{
    Serial.println(enabled ? "\n⚡ Charging Started..." : "\n⛔ Charging Stopped.");
}

// ====== Main periodic printing ======
void printDecodedData()
{
    if (!uiInitialized)
    {
        printStartupScreen();
        uiInitialized = true;
        return;
    }

    if (!batteryConnected)
    {
        printNoBatteryScreen();
        return;
    }

    if (!chargingswitch)
    {
        printSwitchOffAlert();
        return;
    }

    // If muted, don't spam output
    if (userChoice == 0)
    {
        printMenu();
        return;
    }

    // printMenu();

    if (userChoice == 1)
    {
        Serial.printf("[BMS→CCS] Vmax=%.2fV Imax=%.2fA Switch=%s Mode=%s\n",
                      BMS_Vmax, BMS_Imax,
                      chargingswitch ? "YES" : "NO",
                      heating ? "HEATING" : "CHARGING");
        Serial.print("Raw BMS Data: ");
        printBytes(lastBMSData, 8);
        // ====== Charger Feedback ======
    }
    else if (userChoice == 2)
    {
        Serial.print("Charger Status: ");
        Serial.println(chargerStatus);
        Serial.printf("Charger Vmax: %.2f V  Charger Imax: %.2f A\n", Charger_Vmax, Charger_Imax);
        Serial.print("Raw Charger Data: ");
        printBytes(lastStatusData, 8);
    }
    else if (userChoice == 3)
    {
        Serial.printf("Output Voltage: %.2f V  Output Current: %.2f A  Temp: %.2f °C\n",
                      chargerVolt, chargerCurr, chargerTemp);
        Serial.print("Raw Output Data V: ");
        printBytes(lastBattData, 8);
        Serial.print("Raw Output Data I: ");
        printBytes(lastCurrData, 8);
        Serial.print("Raw Output Data T: ");
        printBytes(lastTempData, 8);
    }
    else if (userChoice == 4)
    {
        Serial.printf("Terminal Voltage: %.2f V  Terminal Current: %.2f A  Power: %.2f W\n",
                      terminalVolt, terminalCurr, terminalchargerPower);
        Serial.print("Terminal Status: ");
        Serial.println(terminalStatus);
        Serial.print("Raw Terminal Data 1: ");
        printBytes(lastTermData1, 8);
        Serial.print("Raw Terminal Data 2: ");
        printBytes(lastTermData2, 8);
        Serial.printf("Accumulated Energy: %.2f Wh\n", energyWh);
    }
    else if (userChoice == 5)
    {
        Serial.println("=========== ALL DATA ===========");
        Serial.printf("[BMS] Vmax=%.2fV Imax=%.2fA\n", BMS_Vmax, BMS_Imax);
        Serial.printf("[Charger] Vmax=%.2fV Imax=%.2fA\n", Charger_Vmax, Charger_Imax);
        Serial.printf("[Output] V=%.2fV I=%.2fA T=%.2fC\n", chargerVolt, chargerCurr, chargerTemp);
        Serial.printf("[Terminal] V=%.2fV I=%.2fA P=%.2fW\n", terminalVolt, terminalCurr, terminalchargerPower);
        Serial.printf("Accumulated Energy: %.2f Wh\n", energyWh);
        Serial.print("Raw BMS: ");
        printBytes(lastBMSData, 8);
        Serial.print("Raw Charger: ");
        printBytes(lastStatusData, 8);
        Serial.print("Raw Output V: ");
        printBytes(lastBattData, 8);
        Serial.print("Raw Output I: ");
        printBytes(lastCurrData, 8);
        Serial.print("Raw Output T: ");
        printBytes(lastTempData, 8);
        Serial.print("Raw Terminal1: ");
        printBytes(lastTermData1, 8);
        Serial.print("Raw Terminal2: ");
        printBytes(lastTermData2, 8);
        Serial.print("Heartbeat: ");
        printBytes(lastHData, 8);
        Serial.println("=================================");
    }
}

// ====== Serial input handling ======
void processSerialInput()
{
    if (!Serial.available())
        return;
    char c = (char)Serial.read();

    switch (c)
    {
    case '0':
        userChoice = 0;
        break;
    case '1':
        userChoice = 1;
        break;
    case '2':
        userChoice = 2;
        break;
    case '3':
        userChoice = 3;
        break;
    case '4':
        userChoice = 4;
        break;
    case '5':
        userChoice = 5;
        break;
    case 's':
    case 'S':
        if (!ocppInitialized)
        {
            Serial.println("\n⛔ Cannot start charging: OCPP not initialized.");
        }
        else if (!batteryConnected)
        {
            Serial.println("\n⛔ Cannot start charging: No vehicle detected.");
        }
        else if (!chargingswitch)
        {
            Serial.println("\n⛔ Charger switch is OFF. Please turn ON the charger switch in the vehicle.");
        }
        else
        {
            // Removed: charging control now handled by OCPP RemoteStart
            Serial.println("🔌 EV connected and ready - Charging will start via OCPP RemoteStart from SteVe");
        }
        break;
    case 't':
    case 'T':
        Serial.println("\n🚨 EMERGENCY STOP TRIGGERED!");
        
        // IMMEDIATE hardware disable
        chargingEnabled = false;
        
        if (ocppInitialized && isTransactionRunning(1))
        {
            Serial.println("⏹️  Stopping transaction via OCPP...");
            endTransaction(nullptr, "Local");
            sessionActive = false;
        }
        else if (ocppInitialized && transactionActive)
        {
            Serial.println("⏹️  Clearing transaction state...");
            transactionActive = false;
            activeTransactionId = -1;
            remoteStartAccepted = false;
        }
        else if (!ocppInitialized)
        {
            Serial.println("⚠️  OCPP not initialized - hardware disabled only");
        }
        else
        {
            Serial.println("ℹ️  No active transaction - hardware already safe");
        }
        
        Serial.println("✅ EMERGENCY STOP COMPLETE - Charger disabled\n");
        break;
    default:
        break;
    }
}
