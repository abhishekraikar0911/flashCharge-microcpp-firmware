// Provisioning Tool Implementation
#include "services/provisioning/ProvisioningManager.h"
#include "utils/secure_credentials.h"
#include "config/WssConfig.h"
#include "config/secure_config.h"
#include "system/SafeSerial.h"
#include <WiFi.h>

void Provisioning::enterProvisioningMode() {
    // Silence all background SafeSerial logging so the summary is readable
    SafeSerial::setSuppressed(true);
    delay(50); // let any in-flight prints finish

    Serial.println("\n\n╔════════════════════════════════════════╗");
    Serial.println("║   ZERO-TOUCH PROVISIONING RUNNING      ║");
    Serial.println("║   Auto-configuring MAC ID & Defaults   ║");
    Serial.println("╚════════════════════════════════════════╝\n");
    
    autoProvisionDevice();
}

bool Provisioning::isProvisioningRequired() {
    // Check 1: No credentials at all → definitely need provisioning
    if (!SecureCredentials::g_secureCredentials.hasCredentials()) {
        return true;
    }

    // Check 2: Credentials exist but Charger ID doesn't match hardware MAC
    // This handles the migration from old manually-set IDs to new MAC-based IDs.
    char storedId[48] = "";
    char host[128]    = "";
    char url[256]     = "";
    uint16_t port     = 0;
    SecureConfig::getOCPPConfig(host, port, storedId, url,
                                sizeof(host), sizeof(storedId), sizeof(url));

    // Generate the expected MAC-based ID
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toUpperCase();

    if (mac != String(storedId)) {
        Serial.printf("[NVS] ⚠️  Charger ID mismatch!\n");
        Serial.printf("[NVS]  Stored : %s\n", storedId);
        Serial.printf("[NVS]  MAC ID : %s\n", mac.c_str());
        Serial.println("[NVS] Triggering re-provisioning to update to MAC-based ID...");
        return true;
    }

    return false;
}

void Provisioning::autoProvisionDevice() {
    Serial.println("[NVS] No credentials found. Initiating Auto-Provisioning...");

    // 1. Generate MAC-based Charger ID
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toUpperCase();
    String chargerId = mac;
    
    // 2. Set Default CSMS Host
    String serverHost = "ocpp.rivotmotors.com";
    uint16_t port = 443;
    
    // 3. Set Universal Fallback WiFi
    String fallbackSsid = "NX100";
    String fallbackPass = ""; // Open network
    
    // 4. Set Default GSM APN
    String defaultApn = "";   // Auto-negotiate for most Indian SIMs
    
    // --- Store in NVS ---
    bool success = true;
    
    if (!SecureCredentials::g_secureCredentials.storeWiFiCredentials(fallbackSsid.c_str(), fallbackPass.c_str())) success = false;
    if (!SecureCredentials::g_secureCredentials.storeOCPPCredentials(serverHost.c_str(), port, chargerId.c_str())) success = false;
    SecureConfig::storeGSMCredentials(defaultApn.c_str(), "", "");
    
    WSSConfig::init(); // Initialize default certs

    if (success) {
        Serial.println("\n✅ Auto-Provisioning Complete! Device is ready.");
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Serial.printf (" Charger ID : %s\n", chargerId.c_str());
        Serial.printf (" CSMS URL   : wss://%s:%d\n", serverHost.c_str(), port);
        Serial.printf (" Fallback AP: %s (Open)\n", fallbackSsid.c_str());
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    } else {
        Serial.println("\n❌ FAILED to write to NVS. Hardware issue?");
    }
    
    Serial.println("🔄 Restarting in 3 seconds to apply configuration...\n");
    
    // Safely close the NVS journal then restore logging before restart
    SecureCredentials::g_secureCredentials.close();
    SafeSerial::setSuppressed(false);
    
    delay(3000);
    ESP.restart();
}
