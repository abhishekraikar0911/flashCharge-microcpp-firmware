#include "header.h"
#include <Arduino.h>
#include <math.h>
#include "esp_err.h" // for esp_err_to_name()
#include <MicroOcpp.h>
#include "ocpp/ocpp_client.h"
#include "modules/system_state.h"

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
        Serial.print("Raw BMS Data: ");
        printBytes(lastBMSData, 8);
    }
    else if (userChoice == 2)
    {
        Serial.print("Charger Status: ");
        Serial.println(chargerStatus);
        Serial.printf("Charger Vmax: %.2f V  Charger Imax: %.2f A\n", snap.Charger_Vmax, snap.Charger_Imax);
        Serial.print("Raw Charger Data: ");
        printBytes(lastStatusData, 8);
    }
    else if (userChoice == 3)
    {
        Serial.printf("Output Voltage: %.2f V  Output Current: %.2f A  Temp: %.2f °C\n",
                      snap.chargerVolt, snap.chargerCurr, snap.chargerTemp);
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
                      snap.terminalVolt, snap.terminalCurr, snap.terminalPower);
        Serial.print("Terminal Status: ");
        Serial.println(terminalStatus);
        Serial.print("Raw Terminal Data 1: ");
        printBytes(lastTermData1, 8);
        Serial.print("Raw Terminal Data 2: ");
        printBytes(lastTermData2, 8);
        Serial.printf("Accumulated Energy: %.2f Wh\n", snap.energyWh);
    }
    else if (userChoice == 5)
    {
        Serial.println("=========== ALL DATA ===========");
        Serial.printf("[BMS] Vmax=%.2fV Imax=%.2fA\n", snap.BMS_Vmax, snap.BMS_Imax);
        Serial.printf("[Charger] Vmax=%.2fV Imax=%.2fA\n", snap.Charger_Vmax, snap.Charger_Imax);
        Serial.printf("[Output] V=%.2fV I=%.2fA T=%.2fC\n", snap.chargerVolt, snap.chargerCurr, snap.chargerTemp);
        Serial.printf("[Terminal] V=%.2fV I=%.2fA P=%.2fW\n", snap.terminalVolt, snap.terminalCurr, snap.terminalPower);
        Serial.printf("Accumulated Energy: %.2f Wh\n", snap.energyWh);
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
    auto& state = SystemState::instance();

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
        Serial.println("\n🔌 MANUAL START requested");
        if (!state.getOcppInitialized())
        {
            Serial.println("⛔ Cannot start: OCPP not initialized");
        }
        else if (!state.getBatteryConnected())
        {
            Serial.println("⛔ Cannot start: No vehicle detected");
        }
        else if (!state.getBmsSafeToCharge())
        {
            Serial.println("⛔ Cannot start: BMS not ready");
        }
        else if (state.getTransactionActive() || ocpp::isTransactionRunningSafe(1))
        {
            Serial.println("⚠️  Transaction already active");
        }
        else
        {
            Serial.println("📤 Sending StartTransaction to server...");
            if (ocpp::beginTransactionSafe("MANUAL_START", 1)) {
                Serial.println("✅ StartTransaction sent - waiting for server confirmation");
                Serial.println("   (Charging will start when server responds)");
            } else {
                Serial.println("❌ Failed to send StartTransaction (check OCPP connection)");
            }
        }
        break;
    case 't':
    case 'T':
        Serial.println("\n🚨 MANUAL STOP requested");
        
        if (!state.getOcppInitialized())
        {
            Serial.println("⚠️  OCPP not initialized - cannot send StopTransaction");
            state.setChargingEnabled(false);
            Serial.println("✅ Hardware disabled locally");
        }
        else if (!state.getTransactionActive() && !ocpp::isTransactionRunningSafe(1))
        {
            Serial.println("ℹ️  No active transaction to stop");
        }
        else
        {
            Serial.println("📤 Sending StopTransaction to server...");
            Serial.printf("   (txId=%d)\n", state.getActiveTransactionId());
            
            // Disable hardware immediately
            state.setChargingEnabled(false);
            sendImmediateChargerStop();
            Serial.println("✅ Hardware stopped immediately");
            
            // Send StopTransaction to server
            if (ocpp::endTransactionSafe(nullptr, "Local", 1)) {
                Serial.println("✅ StopTransaction sent to server");
            } else {
                Serial.println("⚠️  Failed to send StopTransaction (check OCPP connection)");
                Serial.println("   (Hardware is stopped, but server may not be notified)");
            }
        }
        break;
    default:
        break;
    }
}
