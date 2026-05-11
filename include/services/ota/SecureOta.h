// Secure OTA Updates with Signature Verification
#ifndef SECURE_OTA_H
#define SECURE_OTA_H

#include <Arduino.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include <mbedtls/rsa.h>

class SecureOTA {
public:
    static bool init();
    static bool beginUpdate(size_t firmwareSize, const uint8_t* signature, size_t sigLen);
    static bool writeChunk(const uint8_t* data, size_t len);
    static bool finalizeUpdate();
    static void setPublicKey(const char* publicKeyPEM);
    static bool verifySignature(const uint8_t* hash, const uint8_t* signature, size_t sigLen);
    
private:
    static mbedtls_sha256_context sha256Ctx;
    static mbedtls_rsa_context rsaCtx;
    static uint8_t firmwareHash[32];
    static uint8_t expectedSignature[256];
    static size_t expectedSigLen;
    static bool updateInProgress;
};

// RSA Public Key for firmware verification (2048-bit)
// Generate with: openssl genrsa -out private.pem 2048
//                openssl rsa -in private.pem -pubout -out public.pem
extern const char* FIRMWARE_PUBLIC_KEY;

#endif
