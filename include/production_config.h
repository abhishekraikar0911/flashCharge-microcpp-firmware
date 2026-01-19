#ifndef PRODUCTION_CONFIG_H
#define PRODUCTION_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

/**
 * @file production_config.h
 * @brief Production-ready configuration and persistence layer
 *
 * Handles NVS storage for transaction state, WiFi credentials,
 * and system health metrics.
 */

namespace prod
{

    class PersistenceManager
    {
    private:
        Preferences prefs;
        static const char *NAMESPACE;

    public:
        PersistenceManager();

        // Transaction persistence
        void saveTransaction(const char *transactionId, const char *idTag);
        bool restoreTransaction(char *transactionId, char *idTag, size_t idLen);
        void clearTransaction();
        bool hasActiveTransaction();

        // System health
        void recordRebootCount();
        uint32_t getRebootCount();
        void recordLastError(const char *error);
        const char *getLastError();

        // WiFi health
        void recordWiFiFailures(uint32_t count);
        uint32_t getWiFiFailures();
        void resetWiFiFailures();

        // Configuration
        void saveCentral(const char *host, uint16_t port);
        bool getCentral(char *host, size_t hostLen, uint16_t &port);
    };

    // Global instance
    extern PersistenceManager g_persistence;

} // namespace prod

#endif // PRODUCTION_CONFIG_H
