#ifndef OCPP_CLIENT_H
#define OCPP_CLIENT_H

#include <MicroOcpp.h>

/**
 * @file ocpp_client.h
 * @brief OCPP Client wrapper for MicroOcpp library integration
 *
 * This header provides convenience functions and declarations for
 * OCPP client functionality using the MicroOcpp library.
 */

namespace ocpp
{

    /**
     * Initialize OCPP client
     */
    void init();

    /**
     * Poll OCPP client
     */
    void poll();

    /**
     * Check if OCPP is connected
     */
    bool isConnected();

    /**
     * Send vehicle info via DataTransfer (before transaction starts)
     */
    void sendVehicleInfo(float soc, float maxCurrent, float voltage, float current, float temperature, uint8_t model, float range);

    /**
     * Send session summary via DataTransfer (after transaction ends)
     */
    void sendSessionSummary(float finalSoc, float energyDelivered, float duration);

    /**
     * Send BMS alert to server
     */
    void sendBMSAlert(const char* alertType, const char* message);

} // namespace ocpp

#endif // OCPP_CLIENT_H
