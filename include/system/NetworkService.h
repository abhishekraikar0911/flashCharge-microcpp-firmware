/**
 * @file NetworkService.h
 * @brief Network connection monitoring, OCPP connectivity, and comm-loss safety
 * @layer Service
 *
 * Uses: g_networkManager, SystemState, ocpp::
 */
#pragma once

namespace prod {

class NetworkService {
public:
    static NetworkService& instance() {
        static NetworkService inst;
        return inst;
    }

    void begin();
    void poll();

private:
    NetworkService() = default;

    unsigned long _lastNetworkTime    = 0;
    bool          _lastWifiConnected  = false;
    bool          _commLossTriggered  = false;
};

} // namespace prod
