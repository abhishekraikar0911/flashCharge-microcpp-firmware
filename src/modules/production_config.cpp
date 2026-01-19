#include "../include/production_config.h"
#include <Arduino.h>

namespace prod
{

    const char *PersistenceManager::NAMESPACE = "ocpp_prod";

    PersistenceManager::PersistenceManager()
    {
        prefs.begin(NAMESPACE, false);
    }

    void PersistenceManager::saveTransaction(const char *transactionId, const char *idTag)
    {
        prefs.putString("txnId", transactionId);
        prefs.putString("idTag", idTag);
        prefs.putULong("txnTime", millis());
        Serial.printf("[PERSIST] Saved transaction: %s (tag: %s)\n", transactionId, idTag);
    }

    bool PersistenceManager::restoreTransaction(char *transactionId, char *idTag, size_t idLen)
    {
        if (!hasActiveTransaction())
        {
            return false;
        }

        String txnId = prefs.getString("txnId", "");
        String tag = prefs.getString("idTag", "");

        if (txnId.length() == 0)
        {
            return false;
        }

        strncpy(transactionId, txnId.c_str(), idLen - 1);
        transactionId[idLen - 1] = '\0';
        strncpy(idTag, tag.c_str(), idLen - 1);
        idTag[idLen - 1] = '\0';

        Serial.printf("[PERSIST] Restored transaction: %s\n", transactionId);
        return true;
    }

    void PersistenceManager::clearTransaction()
    {
        prefs.remove("txnId");
        prefs.remove("idTag");
        prefs.remove("txnTime");
        Serial.println("[PERSIST] Cleared transaction state");
    }

    bool PersistenceManager::hasActiveTransaction()
    {
        return prefs.isKey("txnId");
    }

    void PersistenceManager::recordRebootCount()
    {
        uint32_t count = prefs.getUInt("rebootCount", 0);
        prefs.putUInt("rebootCount", count + 1);
        prefs.putULong("lastRebootTime", millis());
        Serial.printf("[PERSIST] Reboot count: %u\n", count + 1);
    }

    uint32_t PersistenceManager::getRebootCount()
    {
        return prefs.getUInt("rebootCount", 0);
    }

    void PersistenceManager::recordLastError(const char *error)
    {
        prefs.putString("lastError", error);
        prefs.putULong("lastErrorTime", millis());
        Serial.printf("[PERSIST] Recorded error: %s\n", error);
    }

    const char *PersistenceManager::getLastError()
    {
        static String lastError;
        lastError = prefs.getString("lastError", "No error");
        return lastError.c_str();
    }

    void PersistenceManager::recordWiFiFailures(uint32_t count)
    {
        prefs.putUInt("wifiFailures", count);
    }

    uint32_t PersistenceManager::getWiFiFailures()
    {
        return prefs.getUInt("wifiFailures", 0);
    }

    void PersistenceManager::resetWiFiFailures()
    {
        prefs.putUInt("wifiFailures", 0);
    }

    void PersistenceManager::saveCentral(const char *host, uint16_t port)
    {
        prefs.putString("centralHost", host);
        prefs.putUShort("centralPort", port);
        Serial.printf("[PERSIST] Saved central: %s:%d\n", host, port);
    }

    bool PersistenceManager::getCentral(char *host, size_t hostLen, uint16_t &port)
    {
        if (!prefs.isKey("centralHost"))
        {
            return false;
        }

        String h = prefs.getString("centralHost", "");
        strncpy(host, h.c_str(), hostLen - 1);
        host[hostLen - 1] = '\0';
        port = prefs.getUShort("centralPort", 8080);
        return true;
    }

    PersistenceManager g_persistence;

} // namespace prod
