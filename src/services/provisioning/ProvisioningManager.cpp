// Provisioning Tool Implementation
#include "services/provisioning/ProvisioningManager.h"
#include "utils/secure_credentials.h"
#include "config/WssConfig.h"
#include "config/secure_config.h"
#include "system/SafeSerial.h"

void Provisioning::enterProvisioningMode() {
    // Silence all background SafeSerial logging so the wizard prompt is readable
    SafeSerial::setSuppressed(true);
    delay(50); // let any in-flight prints finish

    Serial.println("\n\n╔════════════════════════════════════════╗");
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
    promptGSMCredentials();
    promptCertificates();
    
    Serial.println("\n✅ Provisioning complete! Credentials stored securely.");
    Serial.println("⚠️  Remove any hardcoded credentials from secrets.h");
    Serial.println("🔄 Restarting in 3 seconds...\n");
    
    // Safely close the NVS journal then restore logging before restart
    SecureCredentials::g_secureCredentials.close();
    SafeSerial::setSuppressed(false);
    
    delay(3000);
    ESP.restart();
}

void Provisioning::promptWiFiCredentials() {
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("📶 WiFi Configuration");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    String ssid = readSerialInput("Enter WiFi SSID: ", false);
    String password = readSerialInput("Enter WiFi Password: ", false);
    
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

void Provisioning::promptGSMCredentials() {
    clearSerialBuffer();  // flush any leftover \n from previous step
    delay(100);

    Serial.println("----------------------------------------");
    Serial.println("[GSM / SIM Configuration]");
    Serial.println("----------------------------------------");
    Serial.println("Common APNs: airtelgprs.com | jionet | vi.gprs | bsnlnet");
    Serial.println("(Leave Username/Password blank for most Indian SIMs)");
    Serial.println();

    String apn  = readSerialInput("Enter SIM APN          : ", false);
    String user = readSerialInputOptional("Enter APN Username     : ");
    String pass = readSerialInputOptional("Enter APN Password     : ");

    if (apn.length() > 0) {
        SecureConfig::storeGSMCredentials(apn.c_str(), user.c_str(), pass.c_str());
        Serial.println("[OK] GSM credentials stored\n");
    } else {
        Serial.println("[SKIP] No APN entered — using compile-time default\n");
    }
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

// Accepts empty input (user presses Enter immediately) — used for optional fields
String Provisioning::readSerialInputOptional(const char* prompt) {
    Serial.print(prompt);
    clearSerialBuffer();
    String input = "";
    uint32_t start = millis();
    while (millis() - start < 30000) {  // 30s timeout
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                Serial.println();
                break;  // accept empty
            } else if (c == 127 || c == 8) {
                if (input.length() > 0) {
                    input.remove(input.length() - 1);
                    Serial.print("\b \b");
                }
            } else {
                input += c;
                Serial.print(c);
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
