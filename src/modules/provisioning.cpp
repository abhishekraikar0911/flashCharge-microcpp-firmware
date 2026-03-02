// Provisioning Tool Implementation
#include "modules/provisioning.h"
#include "modules/secure_credentials.h"
#include "modules/wss_config.h"

void Provisioning::enterProvisioningMode() {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   PROVISIONING MODE ACTIVATED          ║");
    Serial.println("║   Secure Credential Setup              ║");
    Serial.println("╚════════════════════════════════════════╝\n");
    runProvisioningWizard();
}

bool Provisioning::isProvisioningRequired() {
    return !SecureCredentials::isProvisioned();
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
    
    if (SecureCredentials::setWiFiCredentials(ssid.c_str(), password.c_str())) {
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
    String serverUrl = readSerialInput("Enter OCPP Server URL (wss://...): ", false);
    
    if (SecureCredentials::setOCPPCredentials(chargerId.c_str(), serverUrl.c_str())) {
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
