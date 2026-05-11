#include "services/charging/MeterService.h"
#include "services/ocpp/OcppClient.h"
#include "system/state/SystemState.h"
#include "config/hardware.h"
#include <MicroOcpp.h>
#include <MicroOcpp/Core/Configuration.h>
#include <functional>

using namespace ocpp;  // For OcppLock, isOperative, sendRequest

// Static flag for compact telemetry rate-limiting (accessible from lambda callback)
static bool s_compactTelemetryPending = false;

namespace prod {

OcppMeterService g_meterService;

void OcppMeterService::begin() {
    Serial.println("[METER_SVC] Metering service started");
    registerMeters();
}

void OcppMeterService::registerMeters() {

    // 1. Energy Meter
    setEnergyMeterInput([]() {
        return SystemState::instance().getEnergyWh();
    });

    // 2. Power Meter
    setPowerMeterInput([]() {
        auto snap = SystemState::instance().snapshot();
        if (snap.terminalVolt >= ALERT_VOLTAGE_MIN_V && snap.terminalVolt <= ALERT_VOLTAGE_MAX_V) {
            return (int)(snap.terminalVolt * snap.terminalCurr);
        }
        return 0;
    });

    // 3. Measurands (standard OCPP — sent every 60s for compliance)
    addMeterValueInput(std::function<float()>([]() { return SystemState::instance().getSocPercent(); }), "SoC", "Percent", nullptr, nullptr, 1);
    addMeterValueInput(std::function<float()>([]() { return SystemState::instance().getTerminalVolt(); }), "Voltage", "V", nullptr, nullptr, 1);
    addMeterValueInput(std::function<float()>([]() { return SystemState::instance().getTerminalCurr(); }), "Current.Import", "A", nullptr, nullptr, 1);
    addMeterValueInput(std::function<float()>([]() { return SystemState::instance().getBMS_Imax(); }), "Current.Offered", "A", nullptr, nullptr, 1);
    addMeterValueInput(std::function<float()>([]() { return SystemState::instance().getChargerTemp(); }), "Temperature", "Celsius", nullptr, nullptr, 1);
}

void OcppMeterService::resetScaling(unsigned long txStartTime) {
    _txStartTime = txStartTime;
    _firstMeterValueSent = false;
    _currentMvInterval = 5; 
    _meterStep = 0;
    _lastMeterValueSent = 0;
    _lastScaledElapsed = 0;
    _lastCompactSent = 0;
    _compactPending = false;
    s_compactTelemetryPending = false;
    
    // Set OCPP standard MeterValues to 30s (Phase 4-F: improved visibility)
    if (auto config = MicroOcpp::getConfigurationPublic("MeterValueSampleInterval")) {
        config->setInt(30);
    }
    Serial.printf("[METER_SVC] 📊 Initialized: Standard MV=30s, Compact Telemetry=%ds\n", _compactInterval);
}

void OcppMeterService::poll() {
    auto& state = SystemState::instance();
    if (!state.getTransactionActive()) {
        return;
    }

    // Sync member flag with static flag (lambda clears static, we sync here)
    if (!s_compactTelemetryPending) {
        _compactPending = false;
    }

    // ══════════════════════════════════════════════════════════════
    // STRATEGY 1: Keep OCPP MeterValues at 30s (Phase 4-F: better visibility)
    // ══════════════════════════════════════════════════════════════
    _meterStep = 2;
    _currentMvInterval = 30;
    static bool _forcedMvIntervalLogged = false;
    if (auto config = MicroOcpp::getConfigurationPublic("MeterValueSampleInterval")) {
        config->setInt(30);
        if (!_forcedMvIntervalLogged) {
            Serial.println("[METER_SVC] 📊 OCPP MeterValues locked at 30s");
            _forcedMvIntervalLogged = true;
        }
    }

    // ══════════════════════════════════════════════════════════════
    // STRATEGY 4: Compact DataTransfer telemetry at 5-10s
    // ══════════════════════════════════════════════════════════════
    if (!_compactPending && 
        (millis() - _lastCompactSent >= (unsigned long)(_compactInterval * 1000))) {
        sendCompactTelemetry();
    }
}

void OcppMeterService::sendCompactTelemetry() {
    // QUEUE GUARD (Strategy 5): Don't send if OCPP isn't connected
    OcppLock lk;
    if (!lk.ok()) return;
    if (!isOperative()) return;

    auto snap = SystemState::instance().snapshot();
    
    // Only send if we have valid readings
    if (snap.terminalVolt < 1.0f) return;

    s_compactTelemetryPending = true;  // Rate-limit via static (lambda clears this)
    _compactPending = true;
    _lastCompactSent = millis();

    // Capture values for lambda
    float v = snap.terminalVolt;
    float i = snap.terminalCurr;
    float s = snap.socPercent;
    float e = snap.energyWh;
    float p = snap.terminalVolt * snap.terminalCurr;

    sendRequest("DataTransfer",
        [v, i, s, e, p]() -> std::unique_ptr<MicroOcpp::JsonDoc> {
            // Compact payload: ~80 bytes vs ~500 for full MeterValues
            auto doc = std::unique_ptr<MicroOcpp::JsonDoc>(new MicroOcpp::JsonDoc(256));
            JsonObject payload = doc->to<JsonObject>();
            payload["vendorId"] = "RivotMotors";
            payload["messageId"] = "LiveTelemetry";

            // Compact data string: {"v":78.4,"i":9.6,"s":89.2,"e":1234.5,"p":752.6}
            MicroOcpp::JsonDoc dataDoc(128);
            JsonObject dataObj = dataDoc.to<JsonObject>();
            dataObj["v"] = serialized(String(v, 1));  // Voltage (1 decimal)
            dataObj["i"] = serialized(String(i, 1));  // Current (1 decimal)
            dataObj["s"] = serialized(String(s, 1));  // SoC (1 decimal)
            dataObj["e"] = serialized(String(e, 1));  // Energy Wh (1 decimal)
            dataObj["p"] = serialized(String(p, 1));  // Power W (1 decimal)

            String dataStr;
            serializeJson(dataObj, dataStr);
            payload["data"] = dataStr;
            
            return doc;
        },
        [](JsonObject response) {
            // ACK received — clear pending flag so next send can proceed
            s_compactTelemetryPending = false;
        }
    );
}

void OcppMeterService::triggerManualMeterValue() {
    // MicroOcpp sends based on the interval config.
    // This is a placeholder for manual trigger if needed.
}

} // namespace prod
