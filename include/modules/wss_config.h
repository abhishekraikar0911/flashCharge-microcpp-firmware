// Secure WebSocket Configuration
// Enables WSS with certificate validation

#ifndef WSS_CONFIG_H
#define WSS_CONFIG_H

#include <Arduino.h>
#include <WiFiClientSecure.h>

class WSSConfig {
public:
    static bool init();
    static WiFiClientSecure* createSecureClient();
    static bool setCACertificate(const char* caCert);
    static bool setClientCertificate(const char* clientCert, const char* clientKey);
    static bool validateCertificate(const char* fingerprint);
    
private:
    static String caCertificate;
    static String clientCertificate;
    static String clientKey;
    static String certFingerprint;
};

// Root CA Certificate for OCPP Server
// Replace with your actual server's CA certificate
extern const char* OCPP_SERVER_CA_CERT;

#endif
