#ifndef OCPP_CLIENT_H
#define OCPP_CLIENT_H

#include <stdint.h>
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
     * Thread-safety: MicroOcpp is NOT thread-safe.
     * Use these helpers to serialize all MicroOcpp calls.
     */
    bool lock(uint32_t timeout_ms = 1000);
    void unlock();

    class OcppLock
    {
    public:
        explicit OcppLock(uint32_t timeout_ms = 1000) : locked(ocpp::lock(timeout_ms)) {}
        ~OcppLock() { if (locked) ocpp::unlock(); }
        bool ok() const { return locked; }

    private:
        bool locked;
    };


    /**
     * Initialize OCPP client
     * @return true if initialization successful, false otherwise
     */
    bool init();

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
    void sendVehicleInfo(float soc, float maxCurrent, float voltage, float current, float temperature, uint8_t model, float range, const char* vin = "ME9NP1411H2172005");

    /**
     * Send session summary via DataTransfer (after transaction ends)
     */
    void sendSessionSummary(float finalSoc, float energyDelivered, float duration);

    /**
     * Send BMS alert to server
     */
    void sendBMSAlert(const char* alertType, const char* message);

    /**
     * Send system alert to server (WiFi, charger, temperature, etc.)
     * @param alertType Alert identifier (e.g., "WIFI_DISCONNECTED")
     * @param message Human-readable message
     * @param severity "Info", "Warning", or "Critical" (default: "Warning")
     */
    void sendSystemAlert(const char* alertType, const char* message, const char* severity = "Warning");

    /**
     * Send charger readiness status to user (before RemoteStart)
     * Shows if charger is ready or has any blocking issues
     */
    void sendChargerStatus(bool ready, const char* reason);

    /**
     * Thread-safe wrappers for MicroOcpp globals
     */
    bool beginTransactionSafe(const char *idTag = nullptr, unsigned int connectorId = 1);
    bool endTransactionSafe(const char *idTag = nullptr, const char *reason = nullptr, unsigned int connectorId = 1);
    bool isTransactionActiveSafe(unsigned int connectorId = 1);
    bool isTransactionRunningSafe(unsigned int connectorId = 1);
    int getTransactionIdSafe(unsigned int connectorId = 1);
    bool ocppPermitsChargeSafe(unsigned int connectorId = 1);

} // namespace ocpp

#endif // OCPP_CLIENT_H
