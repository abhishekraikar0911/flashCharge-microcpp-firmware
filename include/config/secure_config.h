#pragma once

/**
 * @file secure_config.h
 * @brief Secure configuration replacement for secrets.h
 * 
 * SECURITY FIX: This file replaces the hardcoded credentials in secrets.h
 * with secure, encrypted storage using the SecureCredentials system.
 * 
 * Usage:
 * 1. First boot: Call migrateFromLegacySecrets() to move credentials to encrypted storage
 * 2. Subsequent boots: Credentials are loaded from encrypted NVS
 * 3. Remove secrets.h from version control after migration
 */

#include "utils/secure_credentials.h"

namespace SecureConfig
{
    // Default configuration (non-sensitive)
    #define DEFAULT_CHARGER_MODEL "flashCharger"
    #define DEFAULT_CHARGER_VENDOR "Rivot Motors"
    #define DEFAULT_CSMS_PATH "/ocpp16"

    /**
     * @brief One-time migration from hardcoded secrets to encrypted storage
     * @return true if migration successful or already completed
     */
    bool migrateFromLegacySecrets();

    /**
     * @brief Load WiFi credentials from secure storage
     * @param ssid Buffer for SSID (min 33 bytes)
     * @param password Buffer for password (min 64 bytes)
     * @param priority Priority level (1-3, 1=highest)
     * @return true if credentials loaded successfully
     */
    bool getWiFiCredentials(char* ssid, char* password, size_t ssidLen, size_t passLen, int priority = 1);

    /**
     * @brief Load OCPP server configuration from secure storage
     * @param host Buffer for hostname (min 128 bytes)
     * @param port Output port number
     * @param chargerId Buffer for charger ID (min 32 bytes)
     * @param url Buffer for complete URL (min 256 bytes)
     * @return true if configuration loaded successfully
     */
    bool getOCPPConfig(char* host, uint16_t& port, char* chargerId, char* url, 
                       size_t hostLen, size_t idLen, size_t urlLen);

    /**
     * @brief Check if secure credentials are configured
     * @return true if migration completed and credentials available
     */
    bool isConfigured();

    /**
     * @brief Factory reset - clear all stored credentials
     * WARNING: This will require re-provisioning
     */
    void factoryReset();
}