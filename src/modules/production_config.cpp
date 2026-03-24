#include "config/production_config.h"
#include "utils/safe_string.h"
#include <Arduino.h>

namespace prod
{

    const char *PersistenceManager::NAMESPACE = "ocpp_prod";

    PersistenceManager::PersistenceManager()
    {
    }

    bool PersistenceManager::init()
    {
        if (initialized)
        {
            return true;
        }

        if (!prefs.begin(NAMESPACE, false))
        {
            Serial.println("[PERSIST] ❌ prefs.begin failed");
            return false;
        }

        initialized = true;
        return true;
    }

    bool PersistenceManager::ensureInit()
    {
        if (initialized)
        {
            return true;
        }
        return init();
    }

    void PersistenceManager::saveTransaction(const char *transactionId, const char *idTag)
    {
        if (!ensureInit())
        {
            return;
        }
        prefs.putString("txnId", transactionId);
        prefs.putString("idTag", idTag);
        prefs.putULong("txnTime", millis());
        Serial.printf("[PERSIST] Saved transaction: %s (tag: %s)\n", transactionId, idTag);
    }

    bool PersistenceManager::restoreTransaction(char *transactionId, char *idTag, size_t idLen)
    {
        if (!ensureInit())
        {
            return false;
        }
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

        // CRITICAL FIX: Ignore invalid transaction IDs (negative or zero)
        int txnIdInt = txnId.toInt();
        if (txnIdInt <= 0)
        {
            Serial.printf("[PERSIST] ⚠️  Invalid persisted txId=%d, clearing\n", txnIdInt);
            clearTransaction();
            return false;
        }

        // FIX: Use safe string copy to prevent buffer overflow
        SafeString::copy(transactionId, txnId.c_str(), idLen);
        SafeString::copy(idTag, tag.c_str(), idLen);

        Serial.printf("[PERSIST] Restored transaction: %s\n", transactionId);
        return true;
    }

    void PersistenceManager::clearTransaction()
    {
        if (!ensureInit())
        {
            return;
        }
        if (prefs.isKey("txnId")) prefs.remove("txnId");
        if (prefs.isKey("idTag")) prefs.remove("idTag");
        if (prefs.isKey("txnTime")) prefs.remove("txnTime");
        Serial.println("[PERSIST] Cleared transaction state");
    }

    bool PersistenceManager::hasActiveTransaction()
    {
        if (!ensureInit())
        {
            return false;
        }
        return prefs.isKey("txnId");
    }

    void PersistenceManager::recordRebootCount()
    {
        if (!ensureInit())
        {
            return;
        }
        uint32_t count = prefs.getUInt("rebootCount", 0);
        prefs.putUInt("rebootCount", count + 1);
        prefs.putULong("lastRebootTime", millis());
        Serial.printf("[PERSIST] Reboot count: %u\n", count + 1);
    }

    uint32_t PersistenceManager::getRebootCount()
    {
        if (!ensureInit())
        {
            return 0;
        }
        return prefs.getUInt("rebootCount", 0);
    }

    void PersistenceManager::resetRebootCount()
    {
        if (!ensureInit())
        {
            return;
        }
        prefs.putUInt("rebootCount", 0);
        Serial.println("[PERSIST] ✅ Reboot counter reset to 0");
    }

    void PersistenceManager::recordLastError(const char *error)
    {
        if (!ensureInit())
        {
            return;
        }
        prefs.putString("lastError", error);
        prefs.putULong("lastErrorTime", millis());
        Serial.printf("[PERSIST] Recorded error: %s\n", error);
    }

    const char *PersistenceManager::getLastError()
    {
        static String lastError;
        if (!ensureInit())
        {
            lastError = "No error";
            return lastError.c_str();
        }
        lastError = prefs.getString("lastError", "No error");
        return lastError.c_str();
    }

    void PersistenceManager::recordWiFiFailures(uint32_t count)
    {
        if (!ensureInit())
        {
            return;
        }
        prefs.putUInt("wifiFailures", count);
    }

    uint32_t PersistenceManager::getWiFiFailures()
    {
        if (!ensureInit())
        {
            return 0;
        }
        return prefs.getUInt("wifiFailures", 0);
    }

    void PersistenceManager::resetWiFiFailures()
    {
        if (!ensureInit())
        {
            return;
        }
        prefs.putUInt("wifiFailures", 0);
    }

    void PersistenceManager::saveCentral(const char *host, uint16_t port)
    {
        if (!ensureInit())
        {
            return;
        }
        prefs.putString("centralHost", host);
        prefs.putUShort("centralPort", port);
        Serial.printf("[PERSIST] Saved central: %s:%d\n", host, port);
    }

    bool PersistenceManager::getCentral(char *host, size_t hostLen, uint16_t &port)
    {
        if (!ensureInit())
        {
            return false;
        }
        if (!prefs.isKey("centralHost"))
        {
            return false;
        }

        String h = prefs.getString("centralHost", "");
        // FIX: Use safe string copy
        SafeString::copy(host, h.c_str(), hostLen);
        port = prefs.getUShort("centralPort", 8080);
        return true;
    }

    PersistenceManager g_persistence;

} // namespace prod
