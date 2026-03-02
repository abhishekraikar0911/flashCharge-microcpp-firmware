// Secure Credentials Manager Implementation
#include "modules/secure_credentials.h"
#include <nvs_flash.h>

Preferences SecureCredentials::prefs;
const char* SecureCredentials::NAMESPACE = "secure_creds";

bool SecureCredentials::init() {
    // Initialize NVS with encryption
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

bool SecureCredentials::setWiFiCredentials(const char* ssid, const char* password) {
    if (!prefs.begin(NAMESPACE, false)) return false;
    bool success = prefs.putString("wifi_ssid", ssid) && 
                   prefs.putString("wifi_pass", password);
    prefs.end();
    return success;
}

bool SecureCredentials::getWiFiCredentials(char* ssid, size_t ssidLen, char* password, size_t passLen) {
    if (!prefs.begin(NAMESPACE, true)) return false;
    String ssidStr = prefs.getString("wifi_ssid", "");
    String passStr = prefs.getString("wifi_pass", "");
    prefs.end();
    
    if (ssidStr.isEmpty() || passStr.isEmpty()) return false;
    
    strncpy(ssid, ssidStr.c_str(), ssidLen - 1);
    strncpy(password, passStr.c_str(), passLen - 1);
    ssid[ssidLen - 1] = '\0';
    password[passLen - 1] = '\0';
    return true;
}

bool SecureCredentials::setOCPPCredentials(const char* chargerId, const char* serverUrl) {
    if (!prefs.begin(NAMESPACE, false)) return false;
    bool success = prefs.putString("charger_id", chargerId) && 
                   prefs.putString("server_url", serverUrl);
    prefs.end();
    return success;
}

bool SecureCredentials::getOCPPCredentials(char* chargerId, size_t idLen, char* serverUrl, size_t urlLen) {
    if (!prefs.begin(NAMESPACE, true)) return false;
    String idStr = prefs.getString("charger_id", "");
    String urlStr = prefs.getString("server_url", "");
    prefs.end();
    
    if (idStr.isEmpty() || urlStr.isEmpty()) return false;
    
    strncpy(chargerId, idStr.c_str(), idLen - 1);
    strncpy(serverUrl, urlStr.c_str(), urlLen - 1);
    chargerId[idLen - 1] = '\0';
    serverUrl[urlLen - 1] = '\0';
    return true;
}

bool SecureCredentials::isProvisioned() {
    if (!prefs.begin(NAMESPACE, true)) return false;
    bool provisioned = prefs.isKey("wifi_ssid") && prefs.isKey("charger_id");
    prefs.end();
    return provisioned;
}

void SecureCredentials::clearAll() {
    prefs.begin(NAMESPACE, false);
    prefs.clear();
    prefs.end();
}
