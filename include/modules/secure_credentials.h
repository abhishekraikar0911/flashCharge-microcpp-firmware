// Secure Credentials Manager
// Stores sensitive data in encrypted NVS partition

#ifndef SECURE_CREDENTIALS_H
#define SECURE_CREDENTIALS_H

#include <Arduino.h>
#include <Preferences.h>

class SecureCredentials {
public:
    static bool init();
    static bool setWiFiCredentials(const char* ssid, const char* password);
    static bool getWiFiCredentials(char* ssid, size_t ssidLen, char* password, size_t passLen);
    static bool setOCPPCredentials(const char* chargerId, const char* serverUrl);
    static bool getOCPPCredentials(char* chargerId, size_t idLen, char* serverUrl, size_t urlLen);
    static bool isProvisioned();
    static void clearAll();

private:
    static Preferences prefs;
    static const char* NAMESPACE;
};

#endif
