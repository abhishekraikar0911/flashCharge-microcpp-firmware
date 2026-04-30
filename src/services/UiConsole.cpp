#include "services/UiConsole.h"
#include <Arduino.h>
#include <math.h>
#include "esp_err.h" // for esp_err_to_name()
#include <MicroOcpp.h>
#include "services/OcppClient.h"
#include "system/SystemState.h"
#include "app/AppContext.h"
#include "services/TransactionService.h"
// ====== UI States ======
static bool uiInitialized = false;
static int userChoice = 0;

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

    auto snap = SystemState::instance().snapshot();

    if (!snap.batteryConnected)
    {
        printNoBatteryScreen();
        return;
    }

    if (!snap.chargingSwitch)
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

    if (userChoice == 1)
    {
        Serial.printf("[BMS→CCS] Vmax=%.2fV Imax=%.2fA Switch=%s Mode=%s\n",
                      snap.BMS_Vmax, snap.BMS_Imax,
                      snap.chargingSwitch ? "YES" : "NO",
                      snap.heating ? "HEATING" : "CHARGING");
    }
    else if (userChoice == 2)
    {
        Serial.print("Charger Status: ");
        Serial.println(snap.chargerModuleOnline ? "ONLINE" : "OFFLINE");
        Serial.printf("Charger Vmax: %.2f V  Charger Imax: %.2f A\n", snap.Charger_Vmax, snap.Charger_Imax);
    }
    else if (userChoice == 3)
    {
        Serial.printf("Output Voltage: %.2f V  Output Current: %.2f A  Temp: %.2f °C\n",
                      snap.chargerVolt, snap.chargerCurr, snap.chargerTemp);
    }
    else if (userChoice == 4)
    {
        Serial.printf("Terminal Voltage: %.2f V  Terminal Current: %.2f A  Power: %.2f W\n",
                      snap.terminalVolt, snap.terminalCurr, snap.terminalPower);
        Serial.print("Terminal Status: ");
        Serial.println(snap.transactionActive ? "TRANSACTION_ACTIVE" : "IDLE");
        Serial.printf("Accumulated Energy: %.2f Wh\n", (float)snap.energyWh);
    }
    else if (userChoice == 5)
    {
        Serial.println("=========== ALL DATA ===========");
        Serial.printf("[BMS] Vmax=%.2fV Imax=%.2fA\n", snap.BMS_Vmax, snap.BMS_Imax);
        Serial.printf("[Charger] Vmax=%.2fV Imax=%.2fA\n", snap.Charger_Vmax, snap.Charger_Imax);
        Serial.printf("[Output] V=%.2fV I=%.2fA T=%.2fC\n", snap.chargerVolt, snap.chargerCurr, snap.chargerTemp);
        Serial.printf("[Terminal] V=%.2fV I=%.2fA P=%.2fW\n", snap.terminalVolt, snap.terminalCurr, snap.terminalPower);
        Serial.printf("Accumulated Energy: %.2f Wh\n", snap.energyWh);
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
        Serial.println("[CONSOLE] 🔌 Local START requested (System Button)");
        prod::g_transactionManager.startLocalTransaction("LOCAL_ADMIN_1"); // see hardware.h LOCAL_START_ID_TAG
        break;
    case 't':
    case 'T':
        Serial.println("\n🚨 MANUAL STOP requested via Serial Console");
        prod::g_transactionManager.stopLocalTransaction();
        break;
    default:
        break;
    }
}
