// Provisioning Tool Implementation
#include "system/ProvisioningManager.h"
#include "config/secure_credentials.h"
#include "system/WssConfig.h"

void Provisioning::enterProvisioningMode() {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   PROVISIONING MODE ACTIVATED          ║");
    Serial.println("║   Secure Credential Setup              ║");
    Serial.println("╚════════════════════════════════════════╝\n");
    runProvisioningWizard();
}

bool Provisioning::isProvisioningRequired() {
    return !SecureCredentials::g_secureCredentials.hasCredentials();
}

void Provisioning::runProvisioningWizard() {
    Serial.println("This wizard will securely store your credentials in encrypted NVS.\n");
    
    promptWiFiCredentials();
    promptOCPPCredentials();
    promptCertificates();
    
    Serial.println("\n✅ Provisioning complete! Credentials stored securely.");
    Serial.println("⚠️  Remove any hardcoded credentials from secrets.h");
    Serial.println("🔄 Restarting in 3 seconds...\n");
    delay(3000);
    ESP.restart();
}

void Provisioning::promptWiFiCredentials() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("📶 WiFi Configuration");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    String ssid = readSerialInput("Enter WiFi SSID: ", false);
    String password = readSerialInput("Enter WiFi Password: ", true);
    
    if (SecureCredentials::g_secureCredentials.storeWiFiCredentials(ssid.c_str(), password.c_str())) {
        Serial.println("✅ WiFi credentials stored\n");
    } else {
        Serial.println("❌ Failed to store WiFi credentials\n");
    }
}

void Provisioning::promptOCPPCredentials() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("🔌 OCPP Configuration");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    String chargerId = readSerialInput("Enter Charger ID: ", false);
    String serverHost = readSerialInput("Enter OCPP Server Host (e.g. ocpp.rivotmotors.com): ", false);
    String serverPortStr = readSerialInput("Enter OCPP Port (default 443): ", false);
    uint16_t port = serverPortStr.length() > 0 ? serverPortStr.toInt() : 443;
    
    if (SecureCredentials::g_secureCredentials.storeOCPPCredentials(serverHost.c_str(), port, chargerId.c_str())) {
        Serial.println("✅ OCPP credentials stored\n");
    } else {
        Serial.println("❌ Failed to store OCPP credentials\n");
    }
}

void Provisioning::promptCertificates() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("🔐 TLS Certificate Configuration");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("Use default CA certificate? (y/n): ");
    
    String useDefault = readSerialInput("", false);
    if (useDefault.equalsIgnoreCase("n")) {
        Serial.println("⚠️  Custom certificate upload not implemented in this version");
        Serial.println("   Use WSSConfig::setCACertificate() in code for now");
    }
    
    WSSConfig::init();
    Serial.println("✅ Using default CA certificate\n");
}

String Provisioning::readSerialInput(const char* prompt, bool hideInput) {
    Serial.print(prompt);
    clearSerialBuffer();
    
    String input = "";
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (input.length() > 0) {
                    Serial.println();
                    break;
                }
            } else if (c == 127 || c == 8) { // Backspace
                if (input.length() > 0) {
                    input.remove(input.length() - 1);
                    Serial.print("\b \b");
                }
            } else {
                input += c;
                Serial.print(hideInput ? '*' : c);
            }
        }
        delay(10);
    }
    return input;
}

void Provisioning::clearSerialBuffer() {
    while (Serial.available()) {
        Serial.read();
    }
}
