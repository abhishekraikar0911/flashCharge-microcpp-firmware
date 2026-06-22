// Provisioning Tool
// Secure first-time setup via serial console

#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <Arduino.h>

class Provisioning {
public:
    static void enterProvisioningMode();
    static bool isProvisioningRequired();

private:
    static void autoProvisionDevice();
};

#endif
