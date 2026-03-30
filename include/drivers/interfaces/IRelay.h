/**
 * @file IRelay.h
 * @brief Device driver interface for Relays/Contactors
 * @layer Device Driver
 *
 * Implementations: SingleRelay
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once

class IRelay {
public:
    virtual ~IRelay() = default;

    /** Energize the relay coil (Turn ON) */
    virtual void close() = 0;

    /** De-energize the relay coil (Turn OFF) */
    virtual void open() = 0;

    /** @return true if the relay is commanded to be closed (ON) */
    virtual bool isClosed() = 0;
};
