#ifndef MICROOCPP_C_H
#define MICROOCPP_C_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>

    // Initialize MicroOCPP
    bool mocpp_initialize(const char *backendUrl, const char *chargeBoxId);

    // Main loop - call this frequently
    void mocpp_loop(void);

    // Check if transaction is active
    bool ocpp_isTransactionActive(void);

    // Start a transaction
    bool ocpp_startTransaction(const char *idTag, void *onConf, void *onAbort, void *onTimeout, void *onError);

    // Stop a transaction
    bool ocpp_stopTransaction(void *onConf, void *onAbort, void *onTimeout, void *onError);

    // Set metering inputs
    void setEnergyMeterInput(float (*energyInput)(void));
    void setPowerMeterInput(float (*powerInput)(void));

    // Set connector state inputs
    void setConnectorPluggedInput(bool (*pluggedInput)(void));
    void setEvseReadyInput(bool (*evseReadyInput)(void));

    // Deinitialize
    void mocpp_deinitialize(void);

#ifdef __cplusplus
}
#endif

#endif // MICROOCPP_C_H
