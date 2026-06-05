#include "config/secure_config.h"
#include "system/SafeString.h"
#include <Arduino.h>

namespace SecureConfig
{
    // Migration flag to track if legacy secrets have been migrated
    static const char* MIGRATION_KEY = "migration_done";
    
    // ╔══════════════════════════════════════════════════════════════════╗
    // ║  DEPRECATED — DO NOT CALL THIS FUNCTION ON NEW DEVICES          ║
    // ║                                                                  ║
    // ║  This function was used to migrate hardcoded credentials from    ║
    // ║  the old secrets.h file into NVS during the early development    ║
    // ║  phase. It is now replaced by the interactive serial-wizard      ║
    // ║  provisioning flow (Option B) in ChargePoint::initSecurity().    ║
    // ║                                                                  ║
    // ║  Calling this will auto-fill NVS with a hardcoded Charger ID     ║
    // ║  which breaks fleet management (all chargers get the same ID!).  ║
    // ╚══════════════════════════════════════════════════════════════════╝
    bool migrateFromLegacySecrets()
    {
        Serial.println("[SECURE_CONFIG] ⚠️  migrateFromLegacySecrets() called — this is DEPRECATED.");
        
        // Initialize secure credentials system
        if (!SecureCredentials::g_secureCredentials.init())
        {
            Serial.println("[SECURE_CONFIG] ❌ Failed to initialize secure storage");
            return false;
        }
        
        // Check if migration already completed
        Preferences prefs;
        if (prefs.begin("secure_config", true)) // read-only
        {
            bool migrationDone = prefs.getBool(MIGRATION_KEY, false);
            prefs.end();
            
            if (migrationDone && SecureCredentials::g_secureCredentials.hasCredentials())
            {
                Serial.println("[SECURE_CONFIG] ✅ Migration already completed and credentials exist");
                return true;
            }
        }
        
        Serial.println("[SECURE_CONFIG] 📦 Migrating hardcoded credentials to encrypted storage...");
        
        // Migrate WiFi credentials (Priority 1-3)
        struct WiFiCred {
            const char* ssid;
            const char* pass;
            int priority;
        };
        
        WiFiCred wifiCreds[] = {
            {"NX100", "9448908172", 1},
            {"OnePlus Nord CE5 4AF6", "00000000", 2}, 
            // {"TOVIR", "8988984646", 3}
        };
        
        for (int i = 0; i < 2; i++)
        {
            char keySSID[32], keyPass[32];
            SafeString::format(keySSID, sizeof(keySSID), "wifi_ssid_%d", wifiCreds[i].priority);
            SafeString::format(keyPass, sizeof(keyPass), "wifi_pass_%d", wifiCreds[i].priority);
            
            if (prefs.begin("secure_cred", false)) // read-write
            {
                prefs.putString(keySSID, wifiCreds[i].ssid);
                prefs.putString(keyPass, wifiCreds[i].pass);
                prefs.end();
                
                Serial.printf("[SECURE_CONFIG]   ✓ WiFi Priority %d: %s\n", 
                             wifiCreds[i].priority, wifiCreds[i].ssid);
            }
        }
        
        // Migrate OCPP credentials
        if (!SecureCredentials::g_secureCredentials.storeOCPPCredentials(
            "ocpp.rivotmotors.com", 443, "250822008C06"))
        {
            Serial.println("[SECURE_CONFIG] ❌ Failed to store OCPP credentials");
            return false;
        }
        
        // Migrate GSM/APN credentials
        if (prefs.begin("secure_cred", false))
        {
            prefs.putString("gsm_apn",  "JIOCIOT2");
            prefs.putString("gsm_user", "");
            prefs.putString("gsm_pass", "");
            prefs.end();
            Serial.println("[SECURE_CONFIG]   ✓ GSM APN: JIOCIOT2");
        }
        
        // Store charger identity (non-sensitive, but centralized)
        if (prefs.begin("secure_cred", false))
        {
            prefs.putString("charger_model", DEFAULT_CHARGER_MODEL);
            prefs.putString("charger_vendor", DEFAULT_CHARGER_VENDOR);
            prefs.end();
        }
        
        // Mark migration as completed
        if (prefs.begin("secure_config", false))
        {
            prefs.putBool(MIGRATION_KEY, true);
            prefs.end();
        }
        
        Serial.println("[SECURE_CONFIG] ✅ Migration completed successfully");
        Serial.println("[SECURE_CONFIG] ⚠️  IMPORTANT: Remove secrets.h from version control!");
        
        return true;
    }
    
    bool getWiFiCredentials(char* ssid, char* password, size_t ssidLen, size_t passLen, int priority)
    {
        if (!ssid || !password || priority < 1 || priority > 3)
            return false;
            
        Preferences prefs;
        if (!prefs.begin("secure_cred", true)) // read-only
            return false;
            
        char keySSID[32], keyPass[32];
        SafeString::format(keySSID, sizeof(keySSID), "wifi_ssid_%d", priority);
        SafeString::format(keyPass, sizeof(keyPass), "wifi_pass_%d", priority);
        
        String s = prefs.getString(keySSID, "");
        String p = prefs.getString(keyPass, "");
        prefs.end();
        
        if (s.length() == 0)
            return false;
            
        SafeString::copy(ssid, s.c_str(), ssidLen);
        SafeString::copy(password, p.c_str(), passLen);
        
        return true;
    }
    
    bool getOCPPConfig(char* host, uint16_t& port, char* chargerId, char* url,
                       size_t hostLen, size_t idLen, size_t urlLen)
    {
        if (!host || !chargerId || !url)
            return false;
            
        // Load from secure credentials
        if (!SecureCredentials::g_secureCredentials.getOCPPCredentials(
            host, port, chargerId, hostLen, idLen))
        {
            return false;
        }
        
        // Construct secure WebSocket URL
        SafeString::format(url, urlLen, "wss://%s%s", host, DEFAULT_CSMS_PATH);
        
        return true;
    }
    
    bool isConfigured()
    {
        Preferences prefs;
        if (!prefs.begin("secure_config", true))
            return false;
            
        bool migrationDone = prefs.getBool(MIGRATION_KEY, false);
        prefs.end();
        
        return migrationDone && SecureCredentials::g_secureCredentials.hasCredentials();
    }
    
    bool getGSMCredentials(char* apn, char* user, char* pass,
                           size_t apnLen, size_t userLen, size_t passLen)
    {
        if (!apn || !user || !pass) return false;

        Preferences prefs;
        if (!prefs.begin("secure_cred", true)) return false;

        // isKey() guard prevents NOT_FOUND error spam in the serial monitor
        if (!prefs.isKey("gsm_apn")) {
            prefs.end();
            return false;
        }

        String a = prefs.getString("gsm_apn",  "");
        String u = prefs.getString("gsm_user", "");
        String p = prefs.getString("gsm_pass", "");
        prefs.end();

        if (a.length() == 0) return false;

        SafeString::copy(apn,  a.c_str(), apnLen);
        SafeString::copy(user, u.c_str(), userLen);
        SafeString::copy(pass, p.c_str(), passLen);
        return true;
    }

    bool storeGSMCredentials(const char* apn, const char* user, const char* pass)
    {
        if (!apn || strlen(apn) == 0) return false;
        Preferences prefs;
        if (!prefs.begin("secure_cred", false)) return false;
        prefs.putString("gsm_apn",  apn);
        prefs.putString("gsm_user", user ? user : "");
        prefs.putString("gsm_pass", pass ? pass : "");
        prefs.end();
        Serial.printf("[SECURE_CONFIG] GSM APN stored: %s\n", apn);
        return true;
    }

    void factoryReset()
    {
        Serial.println("[SECURE_CONFIG] ⚠️  FACTORY RESET: Clearing all credentials");
        
        // Clear all secure credentials through the manager, then close it
        SecureCredentials::g_secureCredentials.clearAll();
        SecureCredentials::g_secureCredentials.close();
        
        // Also wipe the gsm credentials and migration flags
        Preferences prefs;
        if (prefs.begin("secure_cred", false))
        {
            prefs.clear();
            prefs.end();
        }
        if (prefs.begin("secure_config", false))
        {
            prefs.clear();
            prefs.end();
        }
        Serial.println("[SECURE_CONFIG] ⚠️ Factory reset completed. All secure data cleared.");
        Serial.println("[SECURE_CONFIG] 🔄 Device will require re-provisioning");
    }
}