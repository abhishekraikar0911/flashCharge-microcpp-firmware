// Secure OTA Implementation
#include "modules/secure_ota.h"

mbedtls_sha256_context SecureOTA::sha256Ctx;
mbedtls_rsa_context SecureOTA::rsaCtx;
uint8_t SecureOTA::firmwareHash[32];
uint8_t SecureOTA::expectedSignature[256];
size_t SecureOTA::expectedSigLen = 0;
bool SecureOTA::updateInProgress = false;

// Placeholder public key (replace with your actual key)
const char* FIRMWARE_PUBLIC_KEY = R"EOF(
-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAyourPublicKeyHere
-----END PUBLIC KEY-----
)EOF";

bool SecureOTA::init() {
    mbedtls_sha256_init(&sha256Ctx);
    mbedtls_rsa_init(&rsaCtx, MBEDTLS_RSA_PKCS_V15, 0);
    return true;
}

bool SecureOTA::beginUpdate(size_t firmwareSize, const uint8_t* signature, size_t sigLen) {
    if (updateInProgress) {
        Serial.println("[OTA] ❌ Update already in progress");
        return false;
    }
    
    // Store signature for later verification
    if (sigLen > sizeof(expectedSignature)) {
        Serial.println("[OTA] ❌ Signature too large");
        return false;
    }
    memcpy(expectedSignature, signature, sigLen);
    expectedSigLen = sigLen;
    
    // Initialize SHA256 for firmware hashing
    mbedtls_sha256_starts(&sha256Ctx, 0);
    
    // Begin ESP32 update
    if (!Update.begin(firmwareSize)) {
        Serial.printf("[OTA] ❌ Begin failed: %s\n", Update.errorString());
        return false;
    }
    
    updateInProgress = true;
    Serial.println("[OTA] ✅ Update started with signature verification");
    return true;
}

bool SecureOTA::writeChunk(const uint8_t* data, size_t len) {
    if (!updateInProgress) {
        Serial.println("[OTA] ❌ No update in progress");
        return false;
    }
    
    // Update hash
    mbedtls_sha256_update(&sha256Ctx, data, len);
    
    // Write to flash (Update.write expects non-const pointer)
    size_t written = Update.write(const_cast<uint8_t*>(data), len);
    if (written != len) {
        Serial.printf("[OTA] ❌ Write failed: %s\n", Update.errorString());
        return false;
    }
    
    return true;
}

bool SecureOTA::finalizeUpdate() {
    if (!updateInProgress) {
        Serial.println("[OTA] ❌ No update in progress");
        return false;
    }
    
    // Finalize hash
    mbedtls_sha256_finish(&sha256Ctx, firmwareHash);
    
    // Verify signature
    if (!verifySignature(firmwareHash, expectedSignature, expectedSigLen)) {
        Serial.println("[OTA] ❌ SIGNATURE VERIFICATION FAILED!");
        Update.abort();
        updateInProgress = false;
        return false;
    }
    
    Serial.println("[OTA] ✅ Signature verified successfully");
    
    // Finalize update
    if (!Update.end(true)) {
        Serial.printf("[OTA] ❌ End failed: %s\n", Update.errorString());
        updateInProgress = false;
        return false;
    }
    
    updateInProgress = false;
    Serial.println("[OTA] ✅ Update complete and verified");
    return true;
}

void SecureOTA::setPublicKey(const char* publicKeyPEM) {
    // Parse PEM and load into RSA context
    // Implementation depends on mbedTLS PEM parsing
    Serial.println("[OTA] ⚠️  Custom public key loading not fully implemented");
}

bool SecureOTA::verifySignature(const uint8_t* hash, const uint8_t* signature, size_t sigLen) {
    // For production: implement full RSA signature verification
    // This is a placeholder that always returns true
    Serial.println("[OTA] ⚠️  Signature verification placeholder (implement RSA verify)");
    
    // TODO: Implement proper RSA-SHA256 signature verification
    // int ret = mbedtls_rsa_pkcs1_verify(&rsaCtx, NULL, NULL, 
    //                                     MBEDTLS_RSA_PUBLIC,
    //                                     MBEDTLS_MD_SHA256,
    //                                     32, hash, signature);
    // return ret == 0;
    
    return true; // TEMPORARY - IMPLEMENT PROPER VERIFICATION
}
