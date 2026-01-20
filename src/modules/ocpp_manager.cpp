// OCPP Manager: All OCPP-related logic isolated for easy debugging
#include <Arduino.h>
#include <WiFi.h>
#include <MicroOcpp.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Model/Transactions/Transaction.h>

#include "../../include/ocpp/ocpp_client.h"
#include "../../include/secrets.h"
#include "../../include/header.h"

// External globals from main firmware
extern bool gunPhysicallyConnected;
extern bool chargingEnabled;
extern float energyWh;
extern float terminalVolt;  // FIXED: Use terminal values (real measurements)
extern float terminalCurr;  // FIXED: Use terminal values (real measurements)
extern bool batteryConnected;
extern float batteryAh;
extern float BMS_Imax;
extern float chargerTemp;
extern float socPercent;    // SOC percentage
extern float rangeKm;       // Range in km
extern uint8_t vehicleModel;    // Vehicle model (1=Classic, 2=Pro, 3=Max)

// Charger health check
extern bool isChargerModuleHealthy();

// Charger fault state
static bool chargerFaultActive = false;

// Error code for OCPP
const char* getChargerModuleFaultCode() {
    return chargerFaultActive ? "OtherError" : nullptr;
}

void ocpp::init()
{
    Serial.println("[OCPP] 🔌 Initializing OCPP...");

    // Wait for WiFi
    uint32_t wifiWaitStart = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (millis() - wifiWaitStart > 30000)
        {
            Serial.println("[OCPP] ❌ WiFi timeout!");
            return;
        }
    }

    // Initialize MicroOCPP
    mocpp_initialize(
        SECRET_CSMS_URL,
        SECRET_CHARGER_ID,
        SECRET_CHARGER_MODEL,
        SECRET_CHARGER_VENDOR);

    // Configure MeterValues sample interval (30s)
    if (auto config = MicroOcpp::getConfigurationPublic("MeterValueSampleInterval")) {
        config->setInt(30);
    }

    // Configure which measurands to send (OCPP 1.6 configuration)
    if (auto config = MicroOcpp::getConfigurationPublic("MeterValuesSampledData")) {
        config->setString("Energy.Active.Import.Register,Power.Active.Import,SoC,Current.Offered,Temperature,Voltage,Current.Import");
    }

    // Configure Heartbeat interval (60s for quick offline detection)
    if (auto config = MicroOcpp::getConfigurationPublic("HeartbeatInterval")) {
        config->setInt(60);
    }

    // Energy meter with validation
    setEnergyMeterInput([]() {
        if (energyWh < 0.0f) energyWh = 0.0f;
        return (int)energyWh;
    });

    // FIXED: Power meter using terminal values (real measurements)
    setPowerMeterInput([]() {
        if (terminalVolt < 56.0f || terminalVolt > 85.5f) return 0;
        if (terminalCurr < 0.0f || terminalCurr > 300.0f) return 0;
        return (int)(terminalVolt * terminalCurr);
    });

    // Plug detection
    setConnectorPluggedInput([]() {
        return gunPhysicallyConnected;
    });

    // EVSE ready (charger + battery + gun + health)
    setEvseReadyInput([]() {
        return batteryConnected && gunPhysicallyConnected && isChargerModuleHealthy();
    });

    // EV ready to charge (J1772 State C)
    setEvReadyInput([]() {
        return batteryConnected;  // EV is ready when battery is connected
    });

    // Standard OCPP 1.6 MeterValues
    // State of Charge (standard measurand)
    addMeterValueInput([]() -> float {
        return socPercent;
    }, "SoC", "Percent", nullptr, nullptr, 1);

    // Current Offered (standard measurand)
    addMeterValueInput([]() -> float {
        return BMS_Imax;
    }, "Current.Offered", "A", nullptr, nullptr, 1);

    // Temperature (standard measurand)
    addMeterValueInput([]() -> float {
        return chargerTemp;
    }, "Temperature", "Celsius", nullptr, nullptr, 1);

    // Voltage (standard measurand)
    addMeterValueInput([]() -> float {
        return terminalVolt;
    }, "Voltage", "V", nullptr, nullptr, 1);

    // Current Import (standard measurand)
    addMeterValueInput([]() -> float {
        return terminalCurr;
    }, "Current.Import", "A", nullptr, nullptr, 1);

    // Transaction notifications
    setTxNotificationOutput([](MicroOcpp::Transaction *tx, TxNotification notification) {
        if (notification == TxNotification_RemoteStart || notification == TxNotification_StartTx) {
            chargingEnabled = true;
            Serial.println("[OCPP] ▶️  Charging enabled (RemoteStart)");
        } else if (notification == TxNotification_StopTx || notification == TxNotification_RemoteStop) {
            chargingEnabled = false;
            Serial.println("[OCPP] ⏹️  Charging disabled (RemoteStop)");
        }
    });

    // Error code reporting
    addErrorCodeInput(getChargerModuleFaultCode);

    Serial.println("[OCPP] ✅ OCPP initialized");
}

void ocpp::poll()
{
    mocpp_loop();
}

bool ocpp::isConnected()
{
    return isOperative();
}

void ocpp::notifyChargerFault(bool faulted)
{
    chargerFaultActive = faulted;
    Serial.printf("[OCPP] %s Charger fault %s\n", 
        faulted ? "🚨" : "✅",
        faulted ? "ACTIVE" : "CLEARED");
}
