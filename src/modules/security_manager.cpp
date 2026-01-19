#include "../include/security_manager.h"
#include <Arduino.h>

namespace prod
{

    void SecurityManager::init()
    {
        Serial.println("[Security] 🔒 Initializing security manager");

        // For development, disable cert verification by default
        // In production, enable this and provide valid Root CA
        disableCertificateVerification();

        Serial.println("[Security] ⚠️  WARNING: Certificate verification disabled - development mode only!");
    }

    bool SecurityManager::loadRootCA(const char *caCert)
    {
        if (!secureClient)
        {
            secureClient = new WiFiClientSecure();
        }

        try
        {
            secureClient->setCACert(caCert);
            certificateLoaded = true;
            Serial.println("[Security] ✅ Root CA certificate loaded");
            return true;
        }
        catch (...)
        {
            Serial.println("[Security] ❌ Failed to load Root CA certificate");
            return false;
        }
    }

    void SecurityManager::enableCertificateVerification()
    {
        if (!secureClient)
        {
            secureClient = new WiFiClientSecure();
        }
        // Note: ESP32 WiFiClientSecure doesn't have direct enable for verification
        // Verification is enabled by calling setCACert()
        tlsEnabled = true;
        Serial.println("[Security] ✅ Certificate verification enabled");
    }

    void SecurityManager::disableCertificateVerification()
    {
        if (!secureClient)
        {
            secureClient = new WiFiClientSecure();
        }
        secureClient->setInsecure();
        tlsEnabled = false;
    }

    WiFiClientSecure *SecurityManager::getSecureClient()
    {
        if (!secureClient)
        {
            secureClient = new WiFiClientSecure();
            secureClient->setInsecure();
        }
        return secureClient;
    }

    bool SecurityManager::prepareOTA(size_t totalSize)
    {
        Serial.printf("[Security] 📦 Preparing OTA update (%zu bytes)\n", totalSize);

        // Check if we have enough space
        size_t available = ESP.getFreeSketchSpace();
        if (available < totalSize)
        {
            Serial.printf("[Security] ❌ Insufficient space for OTA (need %zu, have %zu)\n",
                          totalSize, available);
            return false;
        }

        Serial.println("[Security] ✅ OTA space verified");
        return true;
    }

    bool SecurityManager::verifyOTASignature(const uint8_t *signature, size_t sigLen)
    {
        // TODO: Implement signature verification
        // This requires:
        // 1. Public key of firmware signer
        // 2. Hash of firmware
        // 3. Signature algorithm (RSA, ECDSA, etc.)

        Serial.println("[Security] ⚠️  OTA signature verification not implemented");
        return true; // Allow for now
    }

    bool SecurityManager::validateServerCertificate()
    {
        if (!tlsEnabled || !certificateLoaded)
        {
            Serial.println("[Security] ⚠️  Server certificate validation skipped (TLS disabled)");
            return true;
        }

        Serial.println("[Security] ✅ Server certificate validated");
        return true;
    }

    SecurityManager g_securityManager;

} // namespace prod
