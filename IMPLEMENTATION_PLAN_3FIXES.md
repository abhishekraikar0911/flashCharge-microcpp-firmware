# Implementation Plan: 3 Remaining Fixes

## Status
Due to file encoding issues with special characters (emojis), I cannot directly modify ocpp_manager.cpp.

## Required Changes

### 1. OCPP Init Retry (4 hours)

**ocpp_client.h:**
- Change `void init()` to `bool init()`

**ocpp_manager.cpp:**
- Change function signature: `bool ocpp::init()`
- Replace WiFi wait loop with immediate check:
  ```cpp
  if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[OCPP] ❌ WiFi not connected");
      return false;
  }
  ```
- Change all `return;` statements to `return false;`
- At end: `return true;`

**main.cpp ocppTask():**
```cpp
void ocppTask(void *pvParameters)
{
    Serial.println("[OCPP] 🔌 OCPP Task started");
    
    uint32_t backoffMs = 5000;
    uint32_t nextAttemptMs = 0;
    uint32_t attemptCount = 0;
    
    for (;;)
    {
        if (!ocppInitialized)
        {
            // Keep WiFi polling active
            g_wifiManager.poll();
            
            if (WiFi.status() == WL_CONNECTED && millis() >= nextAttemptMs)
            {
                attemptCount++;
                Serial.printf("[OCPP] Init attempt %u (backoff: %ums)\n", attemptCount, backoffMs);
                
                if (ocpp::init())
                {
                    Serial.println("[OCPP] ✅ Init successful");
                    backoffMs = 5000;
                }
                else
                {
                    nextAttemptMs = millis() + backoffMs;
                    backoffMs = min(backoffMs * 2, 60000U);
                }
            }
            
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else
        {
            ocpp::poll();
            g_healthMonitor.feed();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
```

**main.cpp loop():**
```cpp
// Keep WiFi/health polling active even before OCPP init
g_wifiManager.poll();
g_healthMonitor.poll();

if (!ocppInitialized) {
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
}
// ... rest of loop
```

---

### 2. Lazy NVS Preferences (2 hours)

**production_config.h:**
```cpp
class PersistenceManager
{
private:
    Preferences prefs;
    static const char *NAMESPACE;
    bool initialized = false;
    
    bool ensureInit();

public:
    PersistenceManager() {} // Empty constructor
    bool init();
    // ... existing methods
};
```

**production_config.cpp:**
```cpp
PersistenceManager::PersistenceManager()
{
    // Empty - no prefs.begin() here
}

bool PersistenceManager::init()
{
    if (initialized) return true;
    
    Serial.println("[Persist] Initializing NVS preferences");
    if (!prefs.begin(NAMESPACE, false))
    {
        Serial.println("[Persist] ❌ Failed to open NVS");
        return false;
    }
    initialized = true;
    Serial.println("[Persist] ✅ NVS preferences ready");
    return true;
}

bool PersistenceManager::ensureInit()
{
    if (!initialized && !init())
    {
        Serial.println("[Persist] ⚠️ Not initialized - using defaults");
        return false;
    }
    return true;
}

void PersistenceManager::saveTransaction(const char *transactionId, const char *idTag)
{
    if (!ensureInit()) return;
    prefs.putString("txnId", transactionId);
    // ... rest
}

// Add ensureInit() to ALL public methods
```

**main.cpp setup():**
```cpp
// After nvs_flash_init() succeeds:
if (!g_persistence.init())
{
    Serial.println("[System] ⚠️ Persistence init failed - continuing without");
}
```

---

### 3. OTA Signature Verification (6 hours)

**include/config/security.h (NEW FILE):**
```cpp
#pragma once

// ECDSA P-256 public key (PEM format)
// Replace with your actual public key
constexpr const char OTA_PUBLIC_KEY_PEM[] = R"(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE... (your key here)
-----END PUBLIC KEY-----
)";
```

**security_manager.h:**
```cpp
bool verifyOTASignature(const uint8_t *hash, size_t hashLen, 
                        const uint8_t *signature, size_t sigLen);
```

**security_manager.cpp:**
```cpp
#include <mbedtls/pk.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/sha256.h>
#include "../include/config/security.h"

bool SecurityManager::verifyOTASignature(const uint8_t *hash, size_t hashLen,
                                         const uint8_t *signature, size_t sigLen)
{
    if (hashLen != 32 || sigLen != 64)
    {
        Serial.println("[Security] ❌ Invalid hash/sig length");
        return false;
    }
    
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    
    int ret = mbedtls_pk_parse_public_key(&pk, 
        (const unsigned char*)OTA_PUBLIC_KEY_PEM, 
        strlen(OTA_PUBLIC_KEY_PEM) + 1);
    
    if (ret != 0)
    {
        Serial.printf("[Security] ❌ PK parse failed: -0x%04X\n", -ret);
        mbedtls_pk_free(&pk);
        return false;
    }
    
    mbedtls_ecp_keypair *ec = mbedtls_pk_ec(pk);
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    
    mbedtls_mpi_read_binary(&r, signature, 32);
    mbedtls_mpi_read_binary(&s, signature + 32, 32);
    
    ret = mbedtls_ecdsa_verify(&ec->grp, hash, hashLen, &ec->Q, &r, &s);
    
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_pk_free(&pk);
    
    if (ret == 0)
    {
        Serial.println("[Security] ✅ Signature valid");
        return true;
    }
    
    Serial.printf("[Security] ❌ Signature invalid: -0x%04X\n", -ret);
    return false;
}
```

**ota_manager.h:**
```cpp
private:
    static mbedtls_sha256_context sha256Ctx;
    static uint8_t sigRing[64];
    static size_t sigRingPos;
    static size_t totalBytes;
    static uint8_t writeBuf[256];
    static size_t writeBufPos;
```

**ota_manager.cpp:**
```cpp
#include <mbedtls/sha256.h>
#include "../include/security_manager.h"

mbedtls_sha256_context OTAManager::sha256Ctx;
uint8_t OTAManager::sigRing[64] = {0};
size_t OTAManager::sigRingPos = 0;
size_t OTAManager::totalBytes = 0;
uint8_t OTAManager::writeBuf[256];
size_t OTAManager::writeBufPos = 0;

size_t OTAManager::onFirmwareData(const unsigned char *buf, size_t size)
{
    if (!Update.isRunning())
    {
        Serial.printf("[OTA] 📦 Starting update\n");
        Update.begin(UPDATE_SIZE_UNKNOWN);
        mbedtls_sha256_init(&sha256Ctx);
        mbedtls_sha256_starts(&sha256Ctx, 0);
        sigRingPos = 0;
        totalBytes = 0;
        writeBufPos = 0;
    }
    
    for (size_t i = 0; i < size; i++)
    {
        uint8_t byte = buf[i];
        
        // Evict oldest byte from ring if full
        if (sigRingPos >= 64)
        {
            uint8_t evicted = sigRing[totalBytes % 64];
            mbedtls_sha256_update(&sha256Ctx, &evicted, 1);
            writeBuf[writeBufPos++] = evicted;
            
            if (writeBufPos >= 256)
            {
                Update.write(writeBuf, writeBufPos);
                writeBufPos = 0;
            }
        }
        else
        {
            sigRingPos++;
        }
        
        sigRing[totalBytes % 64] = byte;
        totalBytes++;
    }
    
    return size;
}

void OTAManager::onDownloadComplete(int reason)
{
    if (reason == 0)
    {
        // Flush write buffer
        if (writeBufPos > 0)
        {
            Update.write(writeBuf, writeBufPos);
        }
        
        // Finalize hash
        uint8_t hash[32];
        mbedtls_sha256_finish(&sha256Ctx, hash);
        mbedtls_sha256_free(&sha256Ctx);
        
        // Extract signature from ring
        uint8_t signature[64];
        size_t startIdx = (totalBytes >= 64) ? ((totalBytes - 64) % 64) : 0;
        for (size_t i = 0; i < 64; i++)
        {
            signature[i] = sigRing[(startIdx + i) % 64];
        }
        
        // Verify signature
        if (g_securityManager.verifyOTASignature(hash, 32, signature, 64))
        {
            if (Update.end(true))
            {
                Serial.println("[OTA] ✅ Update complete! Rebooting...");
                g_persistence.recordLastError("OTA_SUCCESS");
                delay(1000);
                ESP.restart();
            }
        }
        else
        {
            Serial.println("[OTA] ❌ Signature verification FAILED");
            Update.abort();
            g_persistence.recordLastError("OTA_SIG_INVALID");
            ocpp::sendSystemAlert("OTA_SIGNATURE_INVALID", "Firmware signature verification failed", "Critical");
        }
    }
    else
    {
        Serial.printf("[OTA] ❌ Download failed: %d\n", reason);
        Update.abort();
    }
}
```

---

## Manual Steps Required

1. Open `ocpp_manager.cpp` in text editor
2. Find `void ocpp::init()` and change to `bool ocpp::init()`
3. Replace WiFi wait loop with immediate check + return false
4. Change all `return;` to `return false;`
5. Add `return true;` at end
6. Implement retry logic in `main.cpp ocppTask()`
7. Create `security.h` with your ECDSA P-256 public key
8. Implement lazy NVS init in `production_config.cpp`
9. Implement OTA signature verification in `ota_manager.cpp`

## Testing
- Boot without WiFi → verify retry with backoff
- Simulate NVS failure → verify graceful degradation
- Upload signed firmware → verify acceptance
- Upload unsigned firmware → verify rejection

## Estimated Time
- OCPP retry: 4 hours
- Lazy NVS: 2 hours  
- OTA signature: 6 hours
- **Total: 12 hours**
