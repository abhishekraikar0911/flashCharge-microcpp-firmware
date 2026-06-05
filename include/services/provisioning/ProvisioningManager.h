// Provisioning Tool
// Secure first-time setup via serial console

#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <Arduino.h>

class Provisioning {
public:
    static void enterProvisioningMode();
    static bool isProvisioningRequired();
    static void runProvisioningWizard();
    
private:
    static void promptWiFiCredentials();
    static void promptOCPPCredentials();
    static void promptGSMCredentials();
    static void promptCertificates();
    static String readSerialInput(const char* prompt, bool hideInput = false);
    static String readSerialInputOptional(const char* prompt);
    static void clearSerialBuffer();
};

#endif
