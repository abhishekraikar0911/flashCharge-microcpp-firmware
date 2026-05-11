#include "services/network/NetworkService.h"
#include "app/AppContext.h"
#include "config/hardware.h"
#include "services/network/NetworkManager.h"
#include "system/state/SystemState.h"
#include "services/ocpp/OcppClient.h"

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
            // Send alert first (best-effort — may fail if network is truly gone)
            ocpp::sendSystemAlert("COMMUNICATION_LOST",
                                  "Charging stopped due to network outage", "Critical");
            SystemState::instance().setStopReason(StopReason::NETWORK_LOSS);
            // Use "Other" not "Local" — "Local" implies deliberate operator action (button press)
            // The queued StopTransaction will be delivered when connectivity is restored (autoRecover)
            ocpp::endTransactionSafe(nullptr, "Other");
            _commLossTriggered = true;
        }
    }

    if (networkConnected != _lastWifiConnected) {
        _lastWifiConnected = networkConnected;
    }
}

} // namespace prod
