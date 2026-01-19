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

} // namespace ocpp

#endif // OCPP_CLIENT_H
