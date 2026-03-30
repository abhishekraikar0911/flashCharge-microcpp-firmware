#include "system/NetworkService.h"
#include "app/AppContext.h"
#include "config/hardware.h"
#include "system/NetworkManager.h"
#include "system/SystemState.h"
#include "services/OcppClient.h"

namespace prod {

void NetworkService::begin() {
    _lastNetworkTime = g_app.timer ? g_app.timer->millis() : 0;
    if (g_app.logger) g_app.logger->log(ILogger::Level::INFO, "NET_SVC", "Started");
}

void NetworkService::poll() {
    uint32_t current_time = g_app.timer ? g_app.timer->millis() : 0;
    static uint32_t lastDiagLog = 0;
    if (current_time - lastDiagLog > 5000) {
        lastDiagLog = current_time;
        if (g_app.logger) g_app.logger->log(ILogger::Level::DEBUG, "NET_SVC", "poll()");
    }
    bool networkConnected = prod::g_networkManager.isConnected();
    auto snap = SystemState::instance().snapshot();

    if (networkConnected) {
        _lastNetworkTime = current_time;
        _commLossTriggered = false;
    }
    else if (snap.transactionActive && !_commLossTriggered) {
        if (current_time - _lastNetworkTime > COMM_LOSS_TIMEOUT_MS) {
            if (g_app.logger) g_app.logger->logf(ILogger::Level::ERROR, "SAFETY", "🚨 COMM LOSS: Emergency Stop after %ds outage",
                          (int)(COMM_LOSS_TIMEOUT_MS / 1000));
            ocpp::sendSystemAlert("COMMUNICATION_LOST",
                                  "Charging stopped due to network outage", "Critical");
            SystemState::instance().setStopReason(StopReason::NETWORK_LOSS);
            ocpp::endTransactionSafe(nullptr, "Local");
            _commLossTriggered = true;
        }
    }

    if (networkConnected != _lastWifiConnected) {
        _lastWifiConnected = networkConnected;
    }
}

} // namespace prod
