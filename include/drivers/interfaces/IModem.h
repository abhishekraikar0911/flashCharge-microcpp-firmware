/**
 * @file IModem.h
 * @brief Device driver interface for Cellular Modems (e.g. SIM800, A7670)
 * @layer Device Driver
 *
 * Implementations: SIM800ModemDriver, A7670ModemDriver
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once

class IModem {
public:
    virtual ~IModem() = default;

    /** Perform hardware reset and software initialization of the modem */
    virtual bool init() = 0;

    /**
     * Connect to the cellular network using the given APN.
     * @param apn Access Point Name
     * @return true if GPRS/LTE data connection is established
     */
    virtual bool connect(const char* apn) = 0;

    /** @return true if the modem has an active data context */
    virtual bool isConnected() = 0;

    /** @return Signal Quality/CSQ (e.g., 0-31, 99 for unknown) */
    virtual int getSignalQuality() = 0;

    /** @return The assigned local IP address in string format */
    virtual const char* getLocalIp() = 0;

    /** Disconnect from the network gracefully */
    virtual void disconnect() = 0;
};
