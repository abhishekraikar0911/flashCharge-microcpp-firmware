/**
 * @file secure_credentials.h
 * @brief Secure credential storage for OCPP credentials using ESP32 NVS
 * @layer Utils
 *
 * Stores OCPP server credentials (host, port, charger ID) in ESP32
 * Non-Volatile Storage (NVS / Preferences). NVS is stored in a dedicated
 * flash partition and survives power cycles.
 *
 * This file was recreated after being excluded from version control.
 * It is intentionally NOT in .gitignore — it contains no hardcoded secrets.
 * All actual credentials are loaded at runtime from NVS, which is provisioned
 * once by SecureConfig::migrateFromLegacySecrets().
 *
 * @author Rivot Motors
 * @date 2026
 */
#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace SecureCredentials {

/**
 * @brief Manages secure OCPP credential storage in ESP32 NVS flash.
 *
 * Usage:
 *   1. Call init() once at boot.
 *   2. Call storeOCPPCredentials() to provision (first boot only).
 *   3. Call getOCPPCredentials() to retrieve at runtime.
 */
class SecureCredentialStore {
public:
    static constexpr const char* NVS_NAMESPACE = "cred_store";

    /**
     * @brief Initialize the credential store (validates NVS access).
     * @return true if NVS is accessible.
     */
    bool init() {
        Preferences prefs;
        bool ok = prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
        prefs.end();
        if (!ok) {
            // NVS namespace doesn't exist yet — first boot, create it
            ok = prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
            prefs.end();
        }
        return ok;
    }

    /**
     * @brief Store OCPP server credentials in NVS.
     * @param host  OCPP server hostname (e.g. "ocpp.rivotmotors.com")
     * @param port  WebSocket port (e.g. 443)
     * @param id    Charger ID / station ID (e.g. "250822008C06")
     * @return true if stored successfully.
     */
    bool storeOCPPCredentials(const char* host, uint16_t port, const char* id) {
        if (!host || !id) return false;
        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return false;
        prefs.putString("ocpp_host", host);
        prefs.putUShort("ocpp_port", port);
        prefs.putString("ocpp_id",   id);
        prefs.end();
        return true;
    }

    /**
     * @brief Store WiFi credentials in NVS (priority-1 slot).
     * Stores under the "secure_cred" namespace with keys wifi_ssid_1 / wifi_pass_1,
     * which matches SecureConfig::getWiFiCredentials(priority=1).
     */
    bool storeWiFiCredentials(const char* ssid, const char* password) {
        if (!ssid) return false;
        Preferences prefs;
        if (!prefs.begin("secure_cred", /*readOnly=*/false)) return false;
        prefs.putString("wifi_ssid_1", ssid);
        prefs.putString("wifi_pass_1", password ? password : "");
        prefs.end();
        Serial.printf("[SECURITY] WiFi credentials stored for SSID: %s\n", ssid);
        return true;
    }

    /**
     * @brief Retrieve stored OCPP credentials from NVS.
     * @param host      Output buffer for hostname
     * @param port      Output reference for port number
     * @param id        Output buffer for charger ID
     * @param hostLen   Size of host buffer
     * @param idLen     Size of id buffer
     * @return true if credentials were found and loaded.
     */
    bool getOCPPCredentials(char* host, uint16_t& port, char* id,
                            size_t hostLen, size_t idLen) {
        if (!host || !id) return false;
        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) return false;

        // isKey() check prevents the Arduino core from printing NOT_FOUND errors
        if (!prefs.isKey("ocpp_host") || !prefs.isKey("ocpp_id")) {
            prefs.end();
            return false;
        }

        String h = prefs.getString("ocpp_host", "");
        port      = prefs.getUShort("ocpp_port", 443);
        String i  = prefs.getString("ocpp_id",   "");
        prefs.end();

        if (h.length() == 0 || i.length() == 0) return false;

        strncpy(host, h.c_str(), hostLen - 1); host[hostLen - 1] = '\0';
        strncpy(id,   i.c_str(), idLen   - 1); id[idLen   - 1] = '\0';
        return true;
    }

    /**
     * @brief Check if OCPP credentials have been provisioned.
     * @return true if host and charger ID are non-empty in NVS.
     */
    bool hasCredentials() {
        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) return false;
        bool ok = prefs.isKey("ocpp_host") && prefs.isKey("ocpp_id");
        prefs.end();
        return ok;
    }

    /**
     * @brief Erase all stored credentials from NVS.
     * Used by SecureConfig::factoryReset().
     */
    void clearAll() {
        Preferences prefs;
        if (prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) {
            prefs.clear();
            prefs.end();
        }
    }

    /**
     * @brief No-op close for API compatibility.
     * SecureCredentialStore opens/closes Preferences per-call,
     * so no persistent handle needs closing. This method exists
     * so SecureConfig::factoryReset() can call it without errors.
     */
    void close() {
        // nothing to do — this store is stateless (opens/closes per call)
    }
};

// --------------------------------------------------------------------------
// Global singleton — referenced by SecureConfig functions
// --------------------------------------------------------------------------
extern SecureCredentialStore g_secureCredentials;

} // namespace SecureCredentials
