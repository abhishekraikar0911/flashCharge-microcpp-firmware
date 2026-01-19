#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include <Arduino.h>
#include <WiFiClientSecure.h>

/**
 * @file security_manager.h
 * @brief Security and encryption management
 *
 * Handles:
 * - TLS/WSS (secure WebSocket) setup
 * - Root CA certificate validation
 * - OTA firmware updates with signature verification
 * - Secure credential storage
 */

namespace prod
{

    class SecurityManager
    {
    private:
        WiFiClientSecure *secureClient = nullptr;
        bool tlsEnabled = false;
        bool certificateLoaded = false;

    public:
        /**
         * Initialize security (load certificates, etc.)
         */
        void init();

        /**
         * Load Root CA certificate for server validation
         * @param caCert PEM-formatted certificate string
         */
        bool loadRootCA(const char *caCert);

        /**
         * Enable TLS certificate verification
         */
        void enableCertificateVerification();

        /**
         * Disable certificate verification (development only!)
         */
        void disableCertificateVerification();

        /**
         * Get secure WiFi client for OCPP connection
         */
        WiFiClientSecure *getSecureClient();

        /**
         * Check if TLS is properly configured
         */
        bool isTlsEnabled() const { return tlsEnabled; }

        /**
         * Prepare for OTA update
         * Returns true if ready to accept firmware
         */
        bool prepareOTA(size_t totalSize);

        /**
         * Verify OTA signature (placeholder for signature validation)
         */
        bool verifyOTASignature(const uint8_t *signature, size_t sigLen);

        /**
         * Check server certificate validity
         */
        bool validateServerCertificate();
    };

    extern SecurityManager g_securityManager;

} // namespace prod

#endif // SECURITY_MANAGER_H
