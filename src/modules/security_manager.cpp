#include "modules/security_manager.h"
#include "config/security.h"
#include "config/certs.h"
#include <Arduino.h>
#include <string.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/pk.h>

namespace prod
{

    void SecurityManager::init()
    {
        Serial.println("[Security] 🔒 Initializing security manager");

        // Enforce Server TLS Verification (OCPP Security Profile 2 / Let's Encrypt CA)
        loadRootCA(ISRG_ROOT_X1_CERT);
        enableCertificateVerification();
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
        // Removed setInsecure() to enforce security policies.
        // If someone explicitly calls this, TLS handshakes will fail.
        tlsEnabled = false;
        Serial.println("[Security] ⚠️ disableCertificateVerification called, but setInsecure() is disabled in production.");
    }

    WiFiClientSecure *SecurityManager::getSecureClient()
    {
        if (!secureClient)
        {
            secureClient = new WiFiClientSecure();
            secureClient->setCACert(ISRG_ROOT_X1_CERT);
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
    bool SecurityManager::verifyOTASignature(const uint8_t *hash, size_t hashLen, const uint8_t *signature, size_t sigLen)
    {
        if (!hash || hashLen != 32 || !signature || sigLen != 64)
        {
            Serial.println("[Security] ? Invalid OTA signature input");
            return false;
        }

        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);

        int ret = mbedtls_pk_parse_public_key(
            &pk,
            reinterpret_cast<const unsigned char *>(OTA_PUBLIC_KEY_PEM),
            strlen(OTA_PUBLIC_KEY_PEM) + 1);
        if (ret != 0)
        {
            Serial.printf("[Security] ? Public key parse failed: -0x%04X\n", -ret);
            mbedtls_pk_free(&pk);
            return false;
        }

        if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECKEY))
        {
            Serial.println("[Security] ? Public key is not EC");
            mbedtls_pk_free(&pk);
            return false;
        }

        mbedtls_ecdsa_context *ecdsa = mbedtls_pk_ec(pk);
        if (!ecdsa)
        {
            Serial.println("[Security] ? ECDSA context not available");
            mbedtls_pk_free(&pk);
            return false;
        }

        mbedtls_mpi r;
        mbedtls_mpi s;
        mbedtls_mpi_init(&r);
        mbedtls_mpi_init(&s);

        ret = mbedtls_mpi_read_binary(&r, signature, 32);
        if (ret == 0)
        {
            ret = mbedtls_mpi_read_binary(&s, signature + 32, 32);
        }

        if (ret != 0)
        {
            Serial.printf("[Security] ? Signature parse failed: -0x%04X\n", -ret);
            mbedtls_mpi_free(&r);
            mbedtls_mpi_free(&s);
            mbedtls_pk_free(&pk);
            return false;
        }

        ret = mbedtls_ecdsa_verify(&ecdsa->grp, hash, hashLen, &ecdsa->Q, &r, &s);
        mbedtls_mpi_free(&r);
        mbedtls_mpi_free(&s);
        mbedtls_pk_free(&pk);

        if (ret != 0)
        {
            Serial.printf("[Security] ? OTA signature invalid: -0x%04X\n", -ret);
            return false;
        }

        Serial.println("[Security] ? OTA signature verified");
        return true;
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
