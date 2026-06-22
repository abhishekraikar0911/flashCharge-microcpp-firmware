#include "system/logging/DebugLogger.h"
#include "services/charging/TransactionService.h"
#include "system/state/SystemState.h"
#include "system/SafeSerial.h"
#include "config/secure_config.h"
#include "services/provisioning/ProvisioningManager.h"
#include <Arduino.h>
#include <WiFi.h>

// Define static members
bool DebugLogger::sectionEnabled[6] = {false, false, false, false, false, false};
int DebugLogger::activeSection = -1;

void processDebugCommand(char c) {
    // ── Unified command dispatch (single if/else-if chain) ───────────────
    if (c >= '0' && c <= '6') {
        // Debug section selector: 0 = ALL, 1-6 = specific section
        int section = c - '0';
        DebugLogger::setActiveSection(section);
        if (section == 0) {
            Serial.println("\n✅ Debug ALL sections ENABLED\n");
        } else {
            const char* names[] = {"BMS <--> MCU", "MCU <--> Charger", "OCPP Client",
                                   "WiFi", "State Machine", "System"};
            Serial.printf("\n✅ Debug: %s ENABLED\n\n", names[section - 1]);
        }

    } else if (c == '9') {
        DebugLogger::setActiveSection(-1);
        Serial.println("\n❌ Debug STOPPED\n");

    } else if (c == 'd' || c == 'D') {
        Serial.println("[HAL] MCP2515 diagnostics disabled (Refactoring)");

    } else if (c == 'h' || c == 'H' || c == '?') {
        DebugLogger::printMenu();

    } else if (c == 's' || c == 'S') {
        Serial.println("\n[CONSOLE] 🔌 Local START requested (Unified Trigger)");
        prod::g_transactionManager.startLocalTransaction("LOCAL_ADMIN_1");

    } else if (c == 't' || c == 'T') {
        Serial.println("\n[CONSOLE] 🛑 Local STOP requested (Unified Trigger)");
        prod::g_transactionManager.stopLocalTransaction();

    } else if (c == 'i' || c == 'I') {
        // Flush any leftover \r\n from typing the command
        while(Serial.available()) Serial.read();

        // ── Admin password gate ─────────────────────────────────────────
        Serial.print("\n[NVS] Admin password: ");
        SafeSerial::setSuppressed(true);
        String pwd = "";
        uint32_t pt = millis();
        while (millis() - pt < 15000) {
            if (Serial.available()) {
                char ch = Serial.read();
                if (ch == '\n' || ch == '\r') {
                    Serial.println(); break;
                }
                else if (ch == 127 || ch == 8) {
                    if (pwd.length() > 0) { pwd.remove(pwd.length()-1); Serial.print("\b \b"); }
                } else { pwd += ch; Serial.print('*'); }
            }
            delay(10);
        }
        SafeSerial::setSuppressed(false);
        pwd.trim();
        if (!pwd.equalsIgnoreCase("TOVIR")) {
            Serial.println("[NVS] Access denied.\n");
            return;
        }

        delay(100);
        while(Serial.available()) Serial.read();

        Serial.println("\n+--------------------------------------------------+");
        Serial.println("|         STORED DEVICE CREDENTIALS               |");
        Serial.println("+--------------------------------------------------+");

        // OCPP
        Serial.println("| [OCPP / CSMS]                                    |");
        Serial.println("|--------------------------------------------------|");
        char chargerId[32] = {0}, csmsHost[128] = {0}, csmsUrl[256] = {0};
        uint16_t csmsPort = 0;
        if (SecureConfig::getOCPPConfig(csmsHost, csmsPort, chargerId, csmsUrl,
                                        sizeof(csmsHost), sizeof(chargerId), sizeof(csmsUrl))) {
            Serial.printf("| Charger ID : %-36s|\n", chargerId);
            Serial.printf("| Server     : %-36s|\n", csmsHost);
            Serial.printf("| Port       : %-36u|\n", csmsPort);
            Serial.printf("| Full URL   : %-36s|\n", csmsUrl);
        } else {
            Serial.println("| ** No OCPP credentials stored **                 |");
        }

        // WiFi
        Serial.println("|--------------------------------------------------|");
        Serial.println("| [WiFi]                                           |");
        Serial.println("|--------------------------------------------------|");
        char ssid[64] = {0}, wpass[64] = {0};
        if (SecureConfig::getWiFiCredentials(ssid, wpass, sizeof(ssid), sizeof(wpass))) {
            Serial.printf("| SSID       : %-36s|\n", ssid);
            Serial.printf("| Password   : %-36s|\n", wpass);
        } else {
            Serial.println("| ** No WiFi credentials stored **                 |");
        }

        // GSM
        Serial.println("|--------------------------------------------------|");
        Serial.println("| [GSM / SIM]                                      |");
        Serial.println("|--------------------------------------------------|");
        char apn[32] = {0}, gsmUser[32] = {0}, gsmPass[32] = {0};
        if (SecureConfig::getGSMCredentials(apn, gsmUser, gsmPass,
                                            sizeof(apn), sizeof(gsmUser), sizeof(gsmPass))) {
            Serial.printf("| APN        : %-36s|\n", apn);
            if (strlen(gsmUser) > 0)
                Serial.printf("| User       : %-36s|\n", gsmUser);
        } else {
            Serial.println("| (Using default compile-time APN)                 |");
        }

        Serial.println("+--------------------------------------------------+\n");


    } else if (c == 'r' || c == 'R') {
        // Flush any leftover \r\n from typing the command
        while(Serial.available()) Serial.read();

        // ── Admin password gate ─────────────────────────────────────────
        Serial.print("\n[NVS] Admin password: ");
        SafeSerial::setSuppressed(true);
        String rpwd = "";
        uint32_t rpt = millis();
        while (millis() - rpt < 15000) {
            if (Serial.available()) {
                char ch = Serial.read();
                if (ch == '\n' || ch == '\r') {
                    Serial.println(); break;
                }
                else if (ch == 127 || ch == 8) {
                    if (rpwd.length() > 0) { rpwd.remove(rpwd.length()-1); Serial.print("\b \b"); }
                } else { rpwd += ch; Serial.print('*'); }
            }
            delay(10);
        }
        SafeSerial::setSuppressed(false);
        rpwd.trim();
        if (!rpwd.equalsIgnoreCase("TOVIR")) {
            Serial.println("[NVS] Access denied.\n");
            return;
        }

        // ── Factory reset NVS + re-run provisioning wizard ─────────────
        
        // Wait 100ms and completely flush any leftover characters (like \n from the password Enter key)
        delay(100);
        while(Serial.available()) Serial.read();

        Serial.println("\n[NVS] ⚠️  FACTORY RESET requested!");
        Serial.println("[NVS] Type 'YES' to confirm, or anything else to cancel:");
        
        // Suppress background logs so the user can type 'YES' without interference
        SafeSerial::setSuppressed(true);
        
        String confirm = "";
        uint32_t t = millis();
        Serial.println("[NVS] >> Type YES and press Enter (30 second window):");
        while (millis() - t < 30000) {   // 30 second window
            if (Serial.available()) {
                char ch = Serial.read();
                if (ch == '\n' || ch == '\r') {
                    Serial.println(); break;
                } else if (ch == 127 || ch == 8) { // Backspace
                    if (confirm.length() > 0) {
                        confirm.remove(confirm.length() - 1);
                        Serial.print("\b \b");
                    }
                } else {
                    confirm += ch;
                    Serial.print(ch); // Local echo
                }
            }
            delay(10);
        }
        confirm.trim();
        
        if (confirm.equalsIgnoreCase("yes")) {
            // Keep suppressed, as we're launching the wizard next
            SafeSerial::setSuppressed(false); // Briefly unsuppress for logs
            Serial.println("\n[NVS] ✅ Confirmed — clearing all NVS credentials...");
            SecureConfig::factoryReset();
            Serial.println("[NVS] 🔧 Launching zero-touch auto-provisioner...");
            
            // The auto-provisioner handles its own suppression
            Provisioning::enterProvisioningMode(); // blocks, then restarts ESP32
        } else {
            // Cancelled: Unsuppress logs
            SafeSerial::setSuppressed(false);
            Serial.println("\n[NVS] ❌ Factory reset cancelled.");
        }

    }
    // All other characters (ctrl chars, newlines, etc.) are silently ignored
}
