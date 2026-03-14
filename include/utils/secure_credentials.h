#ifndef SECURE_CREDENTIALS_H
#define SECURE_CREDENTIALS_H

#include <Arduino.h>
#include <Preferences.h>

/**
 * @file secure_credentials.h
 * @brief Secure credential storage using encrypted NVS
 * 
 * CRITICAL SECURITY FIX:
 * - Replaces hardcoded credentials in secrets.h
 * - Uses ESP32 encrypted NVS partition
 * - Credentials never logged to serial
 */

namespace SecureCredentials
{
    class Manager
    {
    private:
        Preferences prefs;
        bool initialized = false;
        static const char *NAMESPACE;

        // Prevent credential logging
        void sanitizeSerialOutput(const char *credential)
        {
            // Never log actual credentials
            Serial.println("[SECURITY] Credential operation (value hidden)");
        }

    public:
        Manager() {}

        /**
         * @brief Initialize secure storage
         * @return true if successful
         */
        bool init()
        {
            if (initialized)
                return true;

            // Open encrypted NVS partition
            if (!prefs.begin(NAMESPACE, false))
            {
                Serial.println("[SECURITY] ❌ Failed to open secure storage");
                return false;
            }

            initialized = true;
            Serial.println("[SECURITY] ✅ Secure credential storage initialized");
            return true;
        }

        /**
         * @brief Store WiFi credentials securely
         * @param ssid WiFi SSID
         * @param password WiFi password
         * @return true if successful
         */
        bool storeWiFiCredentials(const char *ssid, const char *password)
        {
            if (!initialized && !init())
                return false;

            prefs.putString("wifi_ssid", ssid);
            prefs.putString("wifi_pass", password);
            Serial.printf("[SECURITY] WiFi credentials stored for SSID: %s\n", ssid);
            return true;
        }

        /**
         * @brief Retrieve WiFi credentials
         * @param ssid Buffer for SSID (min 33 bytes)
         * @param password Buffer for password (min 64 bytes)
         * @return true if credentials exist
         */
        bool getWiFiCredentials(char *ssid, char *password, size_t ssidLen, size_t passLen)
        {
            if (!initialized && !init())
                return false;

            String s = prefs.getString("wifi_ssid", "");
            String p = prefs.getString("wifi_pass", "");

            if (s.length() == 0)
                return false;

            strncpy(ssid, s.c_str(), ssidLen - 1);
            ssid[ssidLen - 1] = '\0';
            strncpy(password, p.c_str(), passLen - 1);
            password[passLen - 1] = '\0';

            return true;
        }

        /**
         * @brief Store OCPP server credentials
         * @param host Server hostname
         * @param port Server port
         * @param chargerId Charger ID
         * @return true if successful
         */
        bool storeOCPPCredentials(const char *host, uint16_t port, const char *chargerId)
        {
            if (!initialized && !init())
                return false;

            prefs.putString("ocpp_host", host);
            prefs.putUShort("ocpp_port", port);
            prefs.putString("charger_id", chargerId);
            Serial.println("[SECURITY] OCPP credentials stored");
            return true;
        }

        /**
         * @brief Retrieve OCPP credentials
         * @param host Buffer for hostname (min 128 bytes)
         * @param port Output port number
         * @param chargerId Buffer for charger ID (min 32 bytes)
         * @return true if credentials exist
         */
        bool getOCPPCredentials(char *host, uint16_t &port, char *chargerId, 
                                size_t hostLen, size_t idLen)
        {
            if (!initialized && !init())
                return false;

            String h = prefs.getString("ocpp_host", "");
            if (h.length() == 0)
                return false;

            strncpy(host, h.c_str(), hostLen - 1);
            host[hostLen - 1] = '\0';
            port = prefs.getUShort("ocpp_port", 443);

            String id = prefs.getString("charger_id", "");
            strncpy(chargerId, id.c_str(), idLen - 1);
            chargerId[idLen - 1] = '\0';

            return true;
        }

        /**
         * @brief Clear all stored credentials (factory reset)
         */
        void clearAll()
        {
            if (!initialized && !init())
                return;

            prefs.clear();
            Serial.println("[SECURITY] ⚠️  All credentials cleared");
        }

        /**
         * @brief Check if credentials are configured
         * @return true if WiFi and OCPP credentials exist
         */
        bool hasCredentials()
        {
            if (!initialized && !init())
                return false;

            return prefs.isKey("wifi_ssid") && prefs.isKey("ocpp_host");
        }
    };

    const char *Manager::NAMESPACE = "secure_cred";

    // Global instance
    extern Manager g_secureCredentials;

} // namespace SecureCredentials

#endif // SECURE_CREDENTIALS_H
